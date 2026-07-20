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
// Build a file:// URL libgit2 accepts from a local absolute path. The authority
// component is empty, so the path must be absolute *after* the "//" — i.e. begin
// with a slash. Unix paths already do (/tmp/x -> file:///tmp/x); Windows paths
// start with a drive letter (C:/x), so without a leading slash we'd emit
// file://C:/x, where "C:" is misread as the host and the clone/fetch fails.
std::string file_url(const std::filesystem::path& p)
{
    std::string gp = p.generic_string();
    if (gp.empty() || gp.front() != '/')
        gp.insert(gp.begin(), '/');
    return "file://" + gp;
}
} // namespace

TempRepo::TempRepo()
    : m_dir(unique_dir())
{
    std::filesystem::create_directories(m_dir);
    // Resolve symlinks in the temp path. On macOS $TMPDIR lives under /var, itself
    // a symlink to /private/var, and libgit2 reports the *realpath* as the repo
    // workdir. Canonicalising here keeps path() byte-equal to what the git engine
    // returns, so absolute-path assertions (e.g. submodule paths) hold on every
    // platform instead of only where temp dirs aren't symlinked.
    m_dir = std::filesystem::canonical(m_dir);
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
    for (const auto& bare : m_bareDirs)
        std::filesystem::remove_all(bare, ec);
}

void TempRepo::writeFile(std::string_view rel_path, std::string_view contents)
{
    std::filesystem::path file = m_dir / std::filesystem::path(rel_path);
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    if (!out)
        throw std::runtime_error("writeFile: could not open " + file.string());
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::string TempRepo::readFile(std::string_view rel_path)
{
    std::filesystem::path file = m_dir / std::filesystem::path(rel_path);
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw std::runtime_error("readFile: could not open " + file.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
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
    const std::string url     = file_url(childRepoPath);
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

std::filesystem::path TempRepo::addBareRemote(std::string_view name)
{
    // Derive a unique bare-repo path from m_dir so multiple TempRepo instances
    // running in the same process don't share a bare name (e.g. "/tmp/origin.git").
    std::filesystem::path bare = m_dir.parent_path() / (m_dir.filename().string() + "_" + std::string(name) + ".git");
    git_repository* bare_repo  = nullptr;
    check(git_repository_init(&bare_repo, toGitPath(bare).c_str(), /*is_bare=*/1), "git_repository_init (bare) failed");
    git_repository_free(bare_repo);

    std::string url    = file_url(bare);
    git_remote* remote = nullptr;
    check(git_remote_create(&remote, m_repo, std::string(name).c_str(), url.c_str()), "git_remote_create failed");
    git_remote_free(remote);
    m_bareDirs.push_back(bare);
    return bare;
}

void TempRepo::pushBranch(std::string_view remote, std::string_view branch)
{
    git_remote* r = nullptr;
    check(git_remote_lookup(&r, m_repo, std::string(remote).c_str()), "git_remote_lookup failed");

    std::string ref     = "refs/heads/" + std::string(branch);
    std::string refspec = ref + ":" + ref;
    char* specs[]       = {refspec.data()};
    git_strarray arr    = {specs, 1};

    git_push_options opts = GIT_PUSH_OPTIONS_INIT;
    check(git_remote_push(r, &arr, &opts), "git_remote_push failed");
    git_remote_free(r);

    // Set upstream so syncStatus has an upstream to compare against.
    git_reference* branch_ref = nullptr;
    check(git_branch_lookup(&branch_ref, m_repo, std::string(branch).c_str(), GIT_BRANCH_LOCAL),
          "git_branch_lookup failed");
    std::string upstream = std::string(remote) + "/" + std::string(branch);
    check(git_branch_set_upstream(branch_ref, upstream.c_str()), "git_branch_set_upstream failed");
    git_reference_free(branch_ref);
}

void TempRepo::resetBranchTo(std::string_view branch, std::string_view oidHex)
{
    git_oid oid;
    check(git_oid_fromstr(&oid, std::string(oidHex).c_str()), "git_oid_fromstr failed");
    git_object* obj = nullptr;
    check(git_object_lookup(&obj, m_repo, &oid, GIT_OBJECT_COMMIT), "git_object_lookup failed");
    check(git_reset(m_repo, obj, GIT_RESET_HARD, nullptr), "git_reset failed");
    git_object_free(obj);
    (void)branch; // branch identity resolved via HEAD; parameter reserved for future use
}

void TempRepo::cloneFrom(const std::filesystem::path& barePath)
{
    if (m_repo)
    {
        git_repository_free(m_repo);
        m_repo = nullptr;
    }
    std::filesystem::remove_all(m_dir);
    std::string url = "file://" + barePath.generic_string();
    check(git_clone(&m_repo, url.c_str(), toGitPath(m_dir).c_str(), nullptr), "git_clone failed");
}

void TempRepo::tagHead(std::string_view name)
{
    git_object* head = nullptr;
    if (git_revparse_single(&head, m_repo, "HEAD") != 0)
        return;
    git_oid out;
    git_tag_create_lightweight(&out, m_repo, std::string(name).c_str(), head, 0);
    git_object_free(head);
}

} // namespace gittide::test
