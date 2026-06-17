#include "gitgui/GitRepo.hpp"
#include "gitgui/DiffEngine.hpp"
#include "gitgui/PathUtil.hpp"
#include <git2.h>
#include <memory>
#include <utility>
#include <vector>

namespace {
// True when the selection targets the whole file (no hunk chosen).
bool is_whole_file(const gitgui::StageSelection& sel) { return !sel.hunkIndex.has_value(); }

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

std::filesystem::path GitRepo::workdir() const {
    const char* wd = git_repository_workdir(repo_);
    return wd ? from_git_path(wd) : std::filesystem::path{};
}

Expected<void> GitRepo::stage(const StageSelection& sel) {
    git_index* index = nullptr;
    int rc = git_repository_index(&index, repo_);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)>
        idx_guard(index, git_index_free);

    if (is_whole_file(sel)) {
        std::string p = to_git_path(sel.path);
        // If the file is gone from the worktree, stage its deletion.
        std::filesystem::path abs =
            sel.path.is_absolute() ? sel.path : workdir() / sel.path;
        if (!std::filesystem::exists(abs)) {
            rc = git_index_remove_bypath(index, p.c_str());
        } else {
            rc = git_index_add_bypath(index, p.c_str());
        }
        if (rc < 0) return std::unexpected(last_git_error(rc));
        rc = git_index_write(index);
        if (rc < 0) return std::unexpected(last_git_error(rc));
        return {};
    }
    return apply_partial(sel, /*stage=*/true);
}

Expected<void> GitRepo::unstage(const StageSelection& sel) {
    if (is_whole_file(sel)) {
        // Reset the index entry for this path back to HEAD.
        git_object* head = nullptr;
        int rc = git_revparse_single(&head, repo_, "HEAD");
        if (rc < 0) {
            // Unborn branch: no HEAD, so unstaging == removing from index.
            git_index* index = nullptr;
            rc = git_repository_index(&index, repo_);
            if (rc < 0) return std::unexpected(last_git_error(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)>
                idx_guard(index, git_index_free);
            std::string p = to_git_path(sel.path);
            rc = git_index_remove_bypath(index, p.c_str());
            if (rc < 0) return std::unexpected(last_git_error(rc));
            rc = git_index_write(index);
            if (rc < 0) return std::unexpected(last_git_error(rc));
            return {};
        }
        std::unique_ptr<git_object, decltype(&git_object_free)>
            head_guard(head, git_object_free);

        std::string p = to_git_path(sel.path);
        char* paths[] = {p.data()};
        git_strarray pathspec = {paths, 1};
        rc = git_reset_default(repo_, head, &pathspec);
        if (rc < 0) return std::unexpected(last_git_error(rc));
        return {};
    }
    return apply_partial(sel, /*stage=*/false);
}

// TEMPORARY stub — a later task replaces this with the real partial-apply logic.
Expected<void> GitRepo::apply_partial(const StageSelection&, bool) {
    return std::unexpected(GitError{-1, "partial staging not yet implemented"});
}

}  // namespace gitgui
