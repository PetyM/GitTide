#pragma once
#include <string>

namespace gittide {

// Distinguishes how a branch reference is held in the repository.
enum class BranchKind
{
    Local,          // A local branch (refs/heads/*).
    RemoteTracking, // A remote-tracking branch (refs/remotes/*), e.g. "origin/main".
};

// Describes a single branch reference.
struct BranchInfo
{
    std::string name;                    // Short name: "main" or "origin/main".
    bool        isHead = false;          // True when this branch is the current HEAD (local only).
    BranchKind  kind   = BranchKind::Local; // Local vs remote-tracking.
    std::string upstream;                // A local branch's upstream short name, else empty.
    std::string worktreePath;            // Path of the linked worktree holding this local branch, else empty.
    /// Full 40-char hex OID of the branch tip commit, resolved by peeling the
    /// branch reference to a commit object. Empty when resolution fails.
    /// Populated for both local and remote-tracking branches (the peel is cheap
    /// and keeping it uniform avoids a special case in the collection loop in
    /// gitrepo.cpp). Only local branches' tipOids are currently consumed by the
    /// ViewModel layer (for mapping history rows to branch names).
    std::string tipOid;
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
