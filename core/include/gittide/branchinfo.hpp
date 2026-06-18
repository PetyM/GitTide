#pragma once
#include <string>

namespace gittide {

// Describes a single local branch.
struct BranchInfo
{
    std::string name;       // Short branch name (e.g. "main")
    bool        isHead = false; // True when this branch is the current HEAD
};

// Describes the current HEAD state of the repository.
struct HeadState
{
    std::string branch;          // Short branch name when not detached
    std::string oid;             // Full 40-char hex commit SHA (empty when unborn)
    bool        detached = false; // True when HEAD points directly to a commit
    bool        unborn   = false; // True when branch exists but has no commits yet
};

} // namespace gittide
