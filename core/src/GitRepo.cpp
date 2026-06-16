#include "gitgui/GitRepo.hpp"
#include "gitgui/PathUtil.hpp"
#include <git2.h>
#include <utility>
#include <vector>

namespace {
gitgui::StatusFlag map_status(unsigned int s) {
    using gitgui::StatusFlag;
    StatusFlag f = StatusFlag::None;
    if (s & GIT_STATUS_INDEX_NEW)      f |= StatusFlag::IndexNew;
    if (s & GIT_STATUS_INDEX_MODIFIED) f |= StatusFlag::IndexModified;
    if (s & GIT_STATUS_INDEX_DELETED)  f |= StatusFlag::IndexDeleted;
    if (s & GIT_STATUS_WT_NEW)         f |= StatusFlag::WtNew;
    if (s & GIT_STATUS_WT_MODIFIED)    f |= StatusFlag::WtModified;
    if (s & GIT_STATUS_WT_DELETED)     f |= StatusFlag::WtDeleted;
    return f;
}
}  // anonymous namespace

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

Expected<std::vector<FileStatus>> GitRepo::status() const {
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list* list = nullptr;
    int rc = git_status_list_new(&list, repo_, &opts);
    if (rc < 0) return std::unexpected(last_git_error(rc));

    std::vector<FileStatus> result;
    size_t n = git_status_list_entrycount(list);
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const git_status_entry* e = git_status_byindex(list, i);
        const char* raw =
            e->head_to_index    ? e->head_to_index->new_file.path
          : e->index_to_workdir ? e->index_to_workdir->new_file.path
          : nullptr;
        if (!raw) continue;
        result.push_back(FileStatus{from_git_path(raw), map_status(e->status)});
    }
    git_status_list_free(list);
    return result;
}

}  // namespace gitgui
