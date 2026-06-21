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
    std::vector<SubmoduleNode> children;                      // recursive; empty if Uninitialized or leaf
};

} // namespace gittide
