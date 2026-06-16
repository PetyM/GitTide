#pragma once
#include <filesystem>
#include <vector>
#include "gitgui/GitError.hpp"
#include "gitgui/FileStatus.hpp"

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

private:
    explicit GitRepo(git_repository* repo) : repo_(repo) {}
    git_repository* repo_ = nullptr;
};

}  // namespace gitgui
