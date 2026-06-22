#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/giterror.hpp"
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"
#include "gittide/submodule.hpp"
#include "gittide/sync.hpp"

struct git_repository;
struct git_oid;
struct git_tree;

namespace gittide {

// Callback invoked during a clone transfer: (received_objects, total_objects).
// Return true to continue, false to cancel (clone returns an error).
using ProgressCallback = std::function<bool(unsigned received, unsigned total)>;

// RAII wrapper around a single libgit2 git_repository.
// Move-only. Not safe to share across threads; one owner per repo.
class GitRepo
{
public:
    GitRepo(GitRepo&&) noexcept;
    GitRepo& operator=(GitRepo&&) noexcept;
    GitRepo(const GitRepo&)            = delete;
    GitRepo& operator=(const GitRepo&) = delete;
    ~GitRepo();

    // Open an existing repository at (or above) the given path.
    static Expected<GitRepo> open(const std::filesystem::path& path);

    // Initialise a new non-bare repository at path. Creates path if absent.
    // Errors if a .git directory already exists at path.
    static Expected<GitRepo> init(const std::filesystem::path& path);

    // Clone the repository at url into dest. Calls cb during the transfer.
    // dest must not exist (libgit2 creates it). Returns error on failure or cancel.
    static Expected<GitRepo> clone(const std::string& url, const std::filesystem::path& dest, ProgressCallback cb);

    // Working-tree + index status (DEFINED in Task 7).
    Expected<std::vector<FileStatus>> status() const;

    // Diff a single file against the chosen target.
    Expected<DiffResult> diff(DiffTarget target, const std::filesystem::path& file) const;

    // Stage / unstage the selection (whole file, hunk, or specific lines).
    Expected<void> stage(const StageSelection& sel);
    Expected<void> unstage(const StageSelection& sel);

    // Reset the index to HEAD (git reset --mixed HEAD): unstage everything, leave the
    // working tree untouched. On an unborn branch the index is cleared. Used to
    // rebuild the index from a checked selection before committing.
    Expected<void> resetIndexToHead();

    // Commit the current index. Author/committer come from git config
    // (user.name/user.email). Returns the new commit's hex oid.
    Expected<std::string> commit(const CommitRequest& req);

    // Create the merge commit from the current index (parents HEAD + MERGE_HEAD),
    // then clear merge state. Errors if unresolved conflict entries remain.
    // req.message typically defaults at the UI layer to "Merge branch '<x>' into <current>".
    // Returns the commit's hex oid.
    Expected<std::string> commitMerge(CommitRequest req);

    // Revert worktree changes for the selection (whole file or hunk/lines).
    Expected<void> discard(const StageSelection& sel);

    // Walk commits reachable from HEAD, newest first (topological + time).
    // Returns empty vector if repo has no commits. limit=0 means unlimited.
    Expected<std::vector<CommitNode>> log(unsigned limit = 1000) const;

    // Files changed by the commit identified by the 40-char hex oid, relative to its
    // first parent (root commit: relative to an empty tree). Flags use Index* to mean
    // added / modified / deleted, matching the working-changes display model.
    Expected<std::vector<FileStatus>> commitFiles(std::string oid) const;

    // Diff one file inside the commit identified by the 40-char hex oid against its
    // first parent (root commit: against an empty tree). Mirrors diff()'s DiffResult.
    Expected<DiffResult> commitDiff(std::string oid, const std::filesystem::path& file) const;

    // List all local branches. BranchInfo::isHead is true for the current branch.
    Expected<std::vector<BranchInfo>> branches() const;

    // Resolve the current HEAD state (branch name, commit SHA, detached/unborn).
    Expected<HeadState> head() const;

    // Read merge-in-progress state from the repository (state == MERGE, MERGE_MSG,
    // and the index conflict iterator). Derived every call — never cached (D30).
    Expected<MergeState> mergeState() const;

    // Ahead/behind of the current branch versus its upstream remote-tracking
    // ref. hasUpstream is false (ahead/behind 0) when the branch has no upstream
    // or HEAD is unborn/detached. See SyncStatus.
    Expected<SyncStatus> syncStatus() const;

    // Read/write the pull reconciliation strategy, persisted in git config
    // (pull.rebase: true => Rebase, absent/false => FastForwardOnly).
    Expected<PullStrategy> pullStrategy() const;
    Expected<void>         setPullStrategy(PullStrategy strategy);

    // Fetch the named remote, updating remote-tracking refs. cred is supplied by
    // the caller (ssh-agent / https token); cb reports transfer progress. The
    // credential callback selects ssh-agent vs userpass by URL scheme.
    Expected<void> fetch(std::string remoteName, Credentials cred, ProgressCallback cb);

    // Fetch the current branch's upstream remote, then reconcile per
    // pullStrategy(): fast-forward (error if not fast-forwardable) or rebase
    // local commits onto the upstream (abort + error on conflict). HEAD must be
    // on a branch with an upstream.
    Expected<void> pull(Credentials cred, ProgressCallback cb);

    // Push refs/heads/<branch> to remoteName. When setUpstream is true, set the
    // branch's upstream to <remoteName>/<branch> after a successful push
    // ("publish"). cred supplied by the caller; cb reports progress.
    Expected<void> push(std::string remoteName, std::string branch, bool setUpstream,
                        Credentials cred, ProgressCallback cb);

    // Create a new local branch pointing at fromOid (40-char hex SHA).
    // Pass an empty fromOid to branch from current HEAD.
    // Does NOT switch HEAD — creation only.
    Expected<void> createBranch(std::string name, std::string fromOid);

    // Recursively enumerates submodules (depth-first), opening each initialised
    // submodule as its own repository to descend. Each node carries the pinned
    // short OID and a clean/dirty/uninitialised status; uninitialised nodes have
    // no children. See SubmoduleStatus / SubmoduleNode.
    Expected<std::vector<SubmoduleNode>> submoduleTree() const;

    /// De-initialise a submodule: remove its working-tree source files while
    /// preserving the `.git` gitlink file. This allows reinitSubmodule to
    /// re-checkout rather than re-clone. path is repo-relative.
    Expected<void> deinitSubmodule(std::filesystem::path path);

    /// Re-initialise and update a submodule to its pinned commit
    /// (`git_submodule_update` with init enabled). path is repo-relative.
    Expected<void> reinitSubmodule(std::filesystem::path path);

    // Switch HEAD to the named local branch. If the working tree is dirty the
    // changes are auto-stashed before the switch and re-applied afterwards.
    // Returns an error if the branch does not exist or the stash-pop conflicts.
    Expected<void> checkoutBranch(std::string name);

    /// Stash the working tree (including untracked) if it is dirty. Returns true
    /// if a stash was created, false if the tree was already clean (nothing stashed).
    Expected<bool> stashSave(std::string message);

    /// Pop the most-recent stash onto the working tree. Errors (and preserves the
    /// stash) if the pop conflicts.
    Expected<void> stashPop();

    /// Analyse and perform a merge of local branch `name` into current HEAD.
    /// FF when possible (moves HEAD, no merge commit); otherwise a normal merge,
    /// writing conflict markers into the worktree on conflict. Caller handles a
    /// dirty tree (stash) and, on conflict, drives resolution + commitMerge.
    /// See MergeOutcome.
    Expected<MergeOutcome> mergeBranch(std::string name);

    /// Abort an in-progress merge: reset --hard to HEAD and clear MERGE_HEAD/MERGE_MSG,
    /// returning the worktree to its pre-merge state.
    Expected<void> abortMerge();

    // Check out a remote-tracking branch (e.g. "origin/feature"). DWIM, à la
    // GitHub Desktop: if a local branch of the trailing name already exists it is
    // simply switched to; otherwise a local branch is created from the remote ref,
    // its upstream set to that remote ref, and HEAD switched onto it.
    // remoteShorthand is "<remote>/<branch>" (branch may contain '/').
    // Returns an error if the remote-tracking ref does not exist.
    Expected<void> checkoutRemoteBranch(std::string remoteShorthand);

    // Detach HEAD at the commit identified by the 40-char hex oid.
    // If the working tree is dirty the changes are auto-stashed and re-applied
    // afterwards. Returns an error if oid is malformed or the stash-pop conflicts.
    Expected<void> checkoutCommit(std::string oid);

    // Delete the named local branch. Blocks if it is the current branch.
    // Without force, also blocks if the branch is not fully merged into HEAD.
    Expected<void> deleteBranch(std::string name, bool force);

    // Rename a local branch from oldName to newName.
    // Validates newName; with force=true overwrites an existing branch of that name.
    Expected<void> renameBranch(std::string oldName, std::string newName, bool force);

private:
    explicit GitRepo(git_repository* repo)
        : m_repo(repo)
    {
    }
    git_repository* m_repo = nullptr;

    std::filesystem::path workdir() const;                              // repo working directory
    Expected<void> applyPartial(const StageSelection& sel, bool stage); // filled by a later task

    // Resolve a commit's tree and its first-parent tree (parentTree == nullptr for a
    // root commit). Both out-trees are owned by the caller (git_tree_free).
    Expected<void> commitTrees(const std::string& oid, git_tree** outTree, git_tree** outParentTree) const;

    // Low-level: checkout the commit identified by targetCommit, then update
    // HEAD to refToSet (or detach if refToSet is empty). Auto-stashes dirty
    // working tree and pops afterwards. On pop conflict the stash is preserved
    // and an error is returned.
    Expected<void> safeSwitch(const git_oid& targetCommit, const std::string& refToSet);
};

} // namespace gittide
