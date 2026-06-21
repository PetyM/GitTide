#include "support/temprepo.hpp"

#include <fstream>
#include <git2.h>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "gittide/pathutil.hpp"

namespace gittide::test {
namespace {
std::filesystem::path unique_dir()
{
    std::random_device rd;
    auto base = std::filesystem::temp_directory_path();
    return base / ("gittide_test_" + std::to_string(rd()));
}
void check(int rc, const char* what)
{
    if (rc < 0)
        throw std::runtime_error(what);
}
} // namespace

TempRepo::TempRepo()
    : m_dir(unique_dir())
{
    std::filesystem::create_directories(m_dir);
    check(git_repository_init(&m_repo, toGitPath(m_dir).c_str(), /*is_bare=*/0), "git_repository_init failed");

    // CI's Windows runners ship a global core.autocrlf=true, which rewrites
    // committed "\n" to "\r\n" on checkout and breaks byte-exact content asserts
    // (e.g. discard). Pin line endings off in the repo-local config so tests are
    // deterministic regardless of the host's global git configuration.
    git_config* cfg = nullptr;
    check(git_repository_config(&cfg, m_repo), "git_repository_config failed");
    git_config_set_string(cfg, "core.autocrlf", "false");
    git_config_set_string(cfg, "core.eol", "lf");
    git_config_free(cfg);
}

TempRepo::~TempRepo()
{
    if (m_repo)
        git_repository_free(m_repo);
    std::error_code ec;
    std::filesystem::remove_all(m_dir, ec);
}

void TempRepo::writeFile(std::string_view rel_path, std::string_view contents)
{
    std::filesystem::path file = m_dir / std::filesystem::path(rel_path);
    std::ofstream out(file, std::ios::binary);
    if (!out)
        throw std::runtime_error("writeFile: could not open " + file.string());
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

void TempRepo::commitAll(std::string_view message)
{
    // NOTE: leaks index/tree/sig if a check() throws mid-function. Test-only
    // throwaway code on a short-lived binary — intentionally not RAII-wrapped.
    git_index* index = nullptr;
    check(git_repository_index(&index, m_repo), "git_repository_index failed");
    check(git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr), "git_index_add_all failed");
    check(git_index_write(index), "git_index_write failed");

    git_oid tree_oid;
    check(git_index_write_tree(&tree_oid, index), "git_index_write_tree failed");
    git_tree* tree = nullptr;
    check(git_tree_lookup(&tree, m_repo, &tree_oid), "git_tree_lookup failed");

    git_signature* sig = nullptr;
    check(git_signature_now(&sig, "Test", "test@example.com"), "git_signature_now failed");

    // Parent = current HEAD commit, if any.
    git_oid parent_oid;
    git_commit* parent     = nullptr;
    git_commit* parents[1] = {nullptr};
    size_t parent_count    = 0;
    if (git_reference_name_to_id(&parent_oid, m_repo, "HEAD") == 0)
    {
        git_commit_lookup(&parent, m_repo, &parent_oid);
        parents[0]   = parent;
        parent_count = 1;
    }

    git_oid commit_oid;
    check(git_commit_create(
              &commit_oid, m_repo, "HEAD", sig, sig, nullptr, std::string(message).c_str(), tree, parent_count, parents),
          "git_commit_create failed");

    if (parent)
        git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
}

void TempRepo::setIdentity(std::string_view name, std::string_view email)
{
    git_config* cfg = nullptr;
    check(git_repository_config(&cfg, m_repo), "git_repository_config failed");
    check(git_config_set_string(cfg, "user.name", std::string(name).c_str()), "set user.name failed");
    check(git_config_set_string(cfg, "user.email", std::string(email).c_str()), "set user.email failed");
    git_config_free(cfg);
}

void TempRepo::addSubmodule(std::string_view name, const std::filesystem::path& childRepoPath)
{
    // libgit2 clones local paths via a file:// URL.
    const std::string url     = "file://" + childRepoPath.generic_string();
    const std::string subName = std::string(name);

    git_submodule* sm = nullptr;
    check(git_submodule_add_setup(&sm, m_repo, url.c_str(), subName.c_str(), /*use_gitlink=*/1),
          "git_submodule_add_setup failed");

    git_repository* subRepo = nullptr;
    check(git_submodule_clone(&subRepo, sm, nullptr), "git_submodule_clone failed");
    git_repository_free(subRepo);

    check(git_submodule_add_finalize(sm), "git_submodule_add_finalize failed");
    git_submodule_free(sm);
}

namespace {
// Clone+checkout uninitialised submodules of `repo`, then recurse into each.
void updateRecursive(git_repository* repo)
{
    struct Payload
    {
        std::vector<std::string> names;
    } payload;

    git_submodule_foreach(
        repo,
        [](git_submodule* /*sm*/, const char* name, void* pl) -> int
        {
            static_cast<Payload*>(pl)->names.emplace_back(name);
            return 0;
        },
        &payload);

    for (const auto& n : payload.names)
    {
        git_submodule* sm = nullptr;
        if (git_submodule_lookup(&sm, repo, n.c_str()) != 0)
            continue;
        git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
        git_submodule_update(sm, /*init=*/1, &opts); // best-effort
        git_repository* sub = nullptr;
        if (git_submodule_open(&sub, sm) == 0)
        {
            updateRecursive(sub);
            git_repository_free(sub);
        }
        git_submodule_free(sm);
    }
}
} // namespace

void TempRepo::updateSubmodulesRecursive()
{
    updateRecursive(m_repo);
}

} // namespace gittide::test
