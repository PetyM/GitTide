#include "support/TempRepo.hpp"
#include "gitgui/PathUtil.hpp"
#include <git2.h>
#include <fstream>
#include <random>
#include <stdexcept>

namespace gitgui::test {
namespace {
std::filesystem::path unique_dir() {
    std::random_device rd;
    auto base = std::filesystem::temp_directory_path();
    return base / ("gitgui_test_" + std::to_string(rd()));
}
void check(int rc, const char* what) {
    if (rc < 0) throw std::runtime_error(what);
}
}  // namespace

TempRepo::TempRepo() : dir_(unique_dir()) {
    std::filesystem::create_directories(dir_);
    check(git_repository_init(&repo_, to_git_path(dir_).c_str(), /*is_bare=*/0),
          "git_repository_init failed");
}

TempRepo::~TempRepo() {
    if (repo_) git_repository_free(repo_);
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
}

void TempRepo::write_file(std::string_view rel_path, std::string_view contents) {
    std::filesystem::path file = dir_ / std::filesystem::path(rel_path);
    std::ofstream out(file, std::ios::binary);
    if (!out) throw std::runtime_error("write_file: could not open " + file.string());
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

void TempRepo::commit_all(std::string_view message) {
    // NOTE: leaks index/tree/sig if a check() throws mid-function. Test-only
    // throwaway code on a short-lived binary — intentionally not RAII-wrapped.
    git_index* index = nullptr;
    check(git_repository_index(&index, repo_), "git_repository_index failed");
    check(git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr),
          "git_index_add_all failed");
    check(git_index_write(index), "git_index_write failed");

    git_oid tree_oid;
    check(git_index_write_tree(&tree_oid, index), "git_index_write_tree failed");
    git_tree* tree = nullptr;
    check(git_tree_lookup(&tree, repo_, &tree_oid), "git_tree_lookup failed");

    git_signature* sig = nullptr;
    check(git_signature_now(&sig, "Test", "test@example.com"),
          "git_signature_now failed");

    // Parent = current HEAD commit, if any.
    git_oid parent_oid;
    git_commit* parent = nullptr;
    git_commit* parents[1] = {nullptr};
    size_t parent_count = 0;
    if (git_reference_name_to_id(&parent_oid, repo_, "HEAD") == 0) {
        git_commit_lookup(&parent, repo_, &parent_oid);
        parents[0] = parent;
        parent_count = 1;
    }

    git_oid commit_oid;
    check(git_commit_create(&commit_oid, repo_, "HEAD", sig, sig,
                            nullptr, std::string(message).c_str(), tree,
                            parent_count, parents),
          "git_commit_create failed");

    if (parent) git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
}

}  // namespace gitgui::test
