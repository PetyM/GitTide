#include "gitgui/GitRepo.hpp"
#include "gitgui/PathUtil.hpp"
#include <git2.h>
#include <utility>

namespace gitgui {

GitRepo::GitRepo(GitRepo&& o) noexcept : repo_(std::exchange(o.repo_, nullptr)) {}

GitRepo& GitRepo::operator=(GitRepo&& o) noexcept {
    if (this != &o) {
        if (repo_) git_repository_free(repo_);
        repo_ = std::exchange(o.repo_, nullptr);
    }
    return *this;
}

GitRepo::~GitRepo() {
    if (repo_) git_repository_free(repo_);
}

Expected<GitRepo> GitRepo::open(const std::filesystem::path& path) {
    git_repository* repo = nullptr;
    int rc = git_repository_open(&repo, to_git_path(path).c_str());
    if (rc < 0) return std::unexpected(last_git_error(rc));
    return GitRepo(repo);
}

}  // namespace gitgui
