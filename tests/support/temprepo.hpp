#pragma once
#include <filesystem>
#include <string_view>
#include <vector>

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

    // Create a bare repo at <tmp>/<name>.git and register it as remote `name`
    // (file:// url). Returns the bare repo path.
    std::filesystem::path addBareRemote(std::string_view name);

    // Push refs/heads/<branch> to the remote (no auth) and set the branch's
    // upstream to <remote>/<branch>.
    void pushBranch(std::string_view remote, std::string_view branch);

    // Move the branch ref to oidHex and hard-reset the working tree to it.
    void resetBranchTo(std::string_view branch, std::string_view oidHex);

    // Replace this TempRepo's repository with a clone of the bare repo at
    // barePath (file://). Registers origin automatically (libgit2 clone does).
    void cloneFrom(const std::filesystem::path& barePath);

    // Create a lightweight tag named `name` pointing at current HEAD.
    void tagHead(std::string_view name);

private:
    LibGit2Context m_ctx;
    std::filesystem::path m_dir;
    git_repository* m_repo = nullptr;
    std::vector<std::filesystem::path> m_bareDirs; // extra dirs to remove on destruction
};

} // namespace gittide::test
