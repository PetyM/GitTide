#pragma once
#include <filesystem>
#include <vector>
#include <functional>
#include <string>
#include "gittide/GitError.hpp"
#include "gittide/FileStatus.hpp"
#include "gittide/Diff.hpp"
#include "gittide/Graph.hpp"

struct git_repository;

namespace gittide {

// Callback invoked during a clone transfer: (received_objects, total_objects).
// Return true to continue, false to cancel (clone returns an error).
using ProgressCallback = std::function<bool(unsigned received, unsigned total)>;

// RAII wrapper around a single libgit2 git_repository.
// Move-only. Not safe to share across threads; one owner per repo.
class GitRepo {
public:
    GitRepo(GitRepo&&) noexcept;
    GitRepo& operator=(GitRepo&&) noexcept;
    GitRepo(const GitRepo&) = delete;
    GitRepo& operator=(const GitRepo&) = delete;
    ~GitRepo();

    // Open an existing repository at (or above) the given path.
    static Expected<GitRepo> open(const std::filesystem::path& path);

    // Initialise a new non-bare repository at path. Creates path if absent.
    // Errors if a .git directory already exists at path.
    static Expected<GitRepo> init(const std::filesystem::path& path);

    // Clone the repository at url into dest. Calls cb during the transfer.
    // dest must not exist (libgit2 creates it). Returns error on failure or cancel.
    static Expected<GitRepo> clone(const std::string& url,
                                   const std::filesystem::path& dest,
                                   ProgressCallback cb);

    // Working-tree + index status (DEFINED in Task 7).
    Expected<std::vector<FileStatus>> status() const;

    // Diff a single file against the chosen target.
    Expected<DiffResult> diff(DiffTarget target,
                              const std::filesystem::path& file) const;

    // Stage / unstage the selection (whole file, hunk, or specific lines).
    Expected<void> stage(const StageSelection& sel);
    Expected<void> unstage(const StageSelection& sel);

    // Commit the current index. Author/committer come from git config
    // (user.name/user.email). Returns the new commit's hex oid.
    Expected<std::string> commit(const CommitRequest& req);

    // Revert worktree changes for the selection (whole file or hunk/lines).
    Expected<void> discard(const StageSelection& sel);

    // Walk commits reachable from HEAD, newest first (topological + time).
    // Returns empty vector if repo has no commits. limit=0 means unlimited.
    Expected<std::vector<CommitNode>> log(unsigned limit = 1000) const;

private:
    explicit GitRepo(git_repository* repo) : repo_(repo) {}
    git_repository* repo_ = nullptr;

    std::filesystem::path workdir() const;            // repo working directory
    Expected<void> apply_partial(const StageSelection& sel, bool stage);  // filled by a later task
};

}  // namespace gittide
