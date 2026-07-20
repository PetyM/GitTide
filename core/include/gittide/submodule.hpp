#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

// Working state of a submodule relative to what the superproject pins.
enum class SubmoduleStatus
{
    Clean,         // initialised; working tree matches the pinned commit, no local changes
    Dirty,         // initialised; working-tree change, or checked-out commit != pinned
    Uninitialized, // listed in .gitmodules but not checked out (no working dir)
};

// One node in a repository's recursive submodule tree.
struct SubmoduleNode
{
    std::filesystem::path      path;                          // absolute working-dir path
    std::string                name;                          // .gitmodules name (UTF-8)
    std::string                shortOid;                      // pinned gitlink commit, 7 hex; "" if Uninitialized
    SubmoduleStatus            status = SubmoduleStatus::Clean;
    std::string                branch;                        // current branch; "" when detached or uninitialised
    bool                       detached = false;              // submodule HEAD is detached
    std::string                headShortOid;                  // current checked-out commit, 7 hex; "" if uninitialised
    int                        dirtyCount = 0;                // working-tree changed files
    int                        ahead      = 0;                // commits current HEAD is ahead of the pinned commit
    int                        behind     = 0;                // commits current HEAD is behind the pinned commit
    std::vector<SubmoduleNode> children;                      // recursive; empty if Uninitialized or leaf
};

} // namespace gittide
