#pragma once
#include <filesystem>
#include <vector>
#include "gitgui/GitError.hpp"
#include "gitgui/FileStatus.hpp"
#include "gitgui/Diff.hpp"

struct git_repository;

namespace gitgui {

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

private:
    explicit GitRepo(git_repository* repo) : repo_(repo) {}
    git_repository* repo_ = nullptr;

    std::filesystem::path workdir() const;            // repo working directory
    Expected<void> apply_partial(const StageSelection& sel, bool stage);  // filled by a later task
};

}  // namespace gitgui
