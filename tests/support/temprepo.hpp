#pragma once
#include <filesystem>
#include <string_view>

#include "gittide/libgit2context.hpp"

struct git_repository;

namespace gittide::test {

// Creates a unique temporary git repository under temp_directory_path().
// Removes the directory on destruction. Owns a LibGit2Context for its lifetime.
class TempRepo
{
public:
    TempRepo();
    ~TempRepo();
    TempRepo(const TempRepo&)            = delete;
    TempRepo& operator=(const TempRepo&) = delete;

    const std::filesystem::path& path() const
    {
        return m_dir;
    }

    // Write (or overwrite) a file at a repo-relative path.
    void writeFile(std::string_view rel_path, std::string_view contents);

    // Stage all changes and create a commit with a fixed test author.
    void commitAll(std::string_view message);

    // Write user.name / user.email into the repo's git config.
    void setIdentity(std::string_view name, std::string_view email);

    // Add childRepoPath as a submodule at repo-relative path `name`, cloning it
    // into the working tree (initialised). Does not commit; caller commits.
    void addSubmodule(std::string_view name, const std::filesystem::path& childRepoPath);

    // Clone+checkout every uninitialised submodule, depth-first, so nested
    // submodules a non-recursive clone left bare become real working trees.
    void updateSubmodulesRecursive();

private:
    LibGit2Context m_ctx;
    std::filesystem::path m_dir;
    git_repository* m_repo = nullptr;
};

} // namespace gittide::test
