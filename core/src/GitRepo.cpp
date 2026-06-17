#include "gitgui/GitRepo.hpp"
#include "gitgui/DiffEngine.hpp"
#include "gitgui/PathUtil.hpp"
#include <git2.h>
#include <memory>
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

    git_status_list* raw_list = nullptr;
    int rc = git_status_list_new(&raw_list, repo_, &opts);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    // RAII so the list is freed even if a vector allocation below throws.
    std::unique_ptr<git_status_list, decltype(&git_status_list_free)> list(
        raw_list, git_status_list_free);

    std::vector<FileStatus> result;
    size_t n = git_status_list_entrycount(list.get());
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const git_status_entry* e = git_status_byindex(list.get(), i);
        const char* raw =
            e->head_to_index    ? e->head_to_index->new_file.path
          : e->index_to_workdir ? e->index_to_workdir->new_file.path
          : nullptr;
        if (!raw) continue;
        StatusFlag flags = map_status(e->status);
        // Skip statuses this milestone does not model (rename, typechange,
        // conflict, ignored) — emitting them with flags==None would mislead
        // callers. Add the corresponding StatusFlag values to represent them.
        if (flags == StatusFlag::None) continue;
        result.push_back(FileStatus{from_git_path(raw), flags});
    }
    return result;
}

Expected<DiffResult> GitRepo::diff(DiffTarget target,
                                   const std::filesystem::path& file) const {
    std::string git_file = to_git_path(file);
    char* paths[] = {git_file.data()};

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count = 1;
    opts.flags = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT;

    git_diff* raw = nullptr;
    int rc;
    if (target == DiffTarget::WorktreeVsIndex) {
        rc = git_diff_index_to_workdir(&raw, repo_, nullptr, &opts);
    } else {
        // IndexVsHead: compare HEAD's tree to the index. Unborn HEAD -> null tree.
        git_object* head_obj = nullptr;
        git_tree* head_tree = nullptr;
        if (git_revparse_single(&head_obj, repo_, "HEAD^{tree}") == 0) {
            head_tree = reinterpret_cast<git_tree*>(head_obj);
        }
        rc = git_diff_tree_to_index(&raw, repo_, head_tree, nullptr, &opts);
        if (head_tree) git_tree_free(head_tree);
    }
    if (rc < 0) return std::unexpected(last_git_error(rc));

    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff(raw, git_diff_free);
    return DiffEngine::parse(diff.get());
}

}  // namespace gitgui
