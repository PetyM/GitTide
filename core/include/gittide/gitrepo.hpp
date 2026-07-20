#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/giterror.hpp"
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"
#include "gittide/rebase.hpp"
#include "gittide/submodule.hpp"
#include "gittide/sync.hpp"
#include "gittide/watch.hpp"

struct git_repository;
struct git_oid;
struct git_rebase;
struct git_signature;
struct git_tree;

namespace gittide {

// Callback invoked during a clone transfer: (received_objects, total_objects).
// Return true to continue, false to cancel (clone returns an error).
using ProgressCallback = std::function<bool(unsigned received, unsigned total)>;

/// One entry on the stash stack. `index` is the stash@{index} slot (0 = newest),
/// `message` is git's stash message, `oid` is the 40-char hex of the stash commit
/// (a commit whose diff against its first parent is the stashed change set).
struct StashEntry
{
    std::size_t index;
    std::string message;
    std::string oid;
};

/// A git author/committer identity (name + email) as read from merged git config
/// — what `git_signature_default` would use. Pure std; no store id, no libgit2 type.
struct ConfigIdentity
{
    std::string name;
    std::string email;
};

/// The repo's LOCAL-level (`.git/config`) identity, plus whether GitTide wrote it.
/// `managed` is true when our `gittide.identity` marker is present, meaning the
/// local `user.*` keys are GitTide-owned and safe to overwrite/clear; when false
/// the local identity (if any) was set by the user/CLI and must be left untouched.
struct LocalIdentityInfo
{
    bool        hasName  = false;
    bool        hasEmail = false;
    std::string name;
    std::string email;
    bool        managed = false; // gittide.identity marker present
    std::string marker;          // the identity id we recorded, if managed
};

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

    // Clone the repository at url into dest. Calls cb during the transfer. cred is
    // supplied by the caller (ssh-agent / keyfile / https token) and drives the
    // same credential callback as fetch/pull/push, so private clones authenticate.
    // dest must not exist (libgit2 creates it). Returns error on failure or cancel.
    static Expected<GitRepo> clone(const std::string& url, const std::filesystem::path& dest, Credentials cred,
                                   ProgressCallback cb);

    // Working-tree + index status. A changed submodule gitlink carries
    // StatusFlag::Submodule, plus StatusFlag::SubmoduleDirty when the submodule's
    // own working tree has uncommitted work (staging a submodule always records its
    // HEAD, so dirtiness is surfaced, never pinned).
    Expected<std::vector<FileStatus>> status() const;

    /// Directories to watch to keep this repository's view current (D35): every
    /// non-ignored working-tree directory plus every directory inside the git
    /// dir, with the working-tree and git-dir roots for classification. See
    /// gittide::WatchTargets. Read-only; safe to call repeatedly to re-arm a
    /// watcher after the tree's directory layout changes.
    Expected<WatchTargets> watchTargets() const;

    /// Diff a single file against the chosen target. Untracked files are included
    /// with their full content (rendered as all-added), recursing into brand-new
    /// untracked directories so a file inside one still diffs individually rather
    /// than collapsing to a single directory entry.
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
    // A whole-file selection on a file with no committed (HEAD) version — i.e. a
    // new untracked or staged file — has nothing to restore, so it is removed:
    // dropped from the index (if staged) and deleted from the worktree.
    Expected<void> discard(const StageSelection& sel);

    /// Discard *all* working-tree changes: hard-reset tracked files (staged and
    /// unstaged) to HEAD, then delete every untracked file. Leaves ignored files
    /// alone. On an unborn HEAD there is no commit to reset to, so the index is
    /// cleared instead. Returns an error only on a libgit2 failure.
    Expected<void> discardAll();

    // Walk commits reachable from HEAD, newest first (topological + time).
    // Returns empty vector if repo has no commits. limit=0 means unlimited.
    Expected<std::vector<CommitNode>> log(unsigned limit = 1000) const;

    // Walk every ref (refs/heads/*, refs/remotes/*, refs/tags/*) topologically
    // and by time, newest first. For the full-graph view. limit 0 = unbounded.
    // Same CommitNode shape as log(), so GraphBuilder::build() consumes it.
    Expected<std::vector<CommitNode>> logAllRefs(unsigned limit = 0) const;

    // Files changed by the commit identified by the 40-char hex oid, relative to its
    // first parent (root commit: relative to an empty tree). Flags use Index* to mean
    // added / modified / deleted, matching the working-changes display model.
    Expected<std::vector<FileStatus>> commitFiles(std::string oid) const;

    // Diff one file inside the commit identified by the 40-char hex oid against its
    // first parent (root commit: against an empty tree). Mirrors diff()'s DiffResult.
    Expected<DiffResult> commitDiff(std::string oid, const std::filesystem::path& file) const;

    // Files changed across the inclusive range oldOid..newOid:
    // tree(parent(oldOid)) vs tree(newOid). Flags use Index* (added/modified/
    // deleted), matching commitFiles(). Caller guarantees a contiguous range.
    Expected<std::vector<FileStatus>> rangeFiles(std::string oldOid, std::string newOid) const;

    // Diff one file across the inclusive range oldOid..newOid (same tree pair as
    // rangeFiles). Mirrors commitDiff()'s DiffResult.
    Expected<DiffResult> rangeDiff(std::string oldOid, std::string newOid,
                                   const std::filesystem::path& file) const;

    // Enumerate ref tips (local + remote-tracking branches + tags) with short
    // names, each resolved to the commit oid it points at. For graph chips.
    Expected<std::vector<RefTip>> refTips() const;

    // List all local branches. BranchInfo::isHead is true for the current branch.
    Expected<std::vector<BranchInfo>> branches() const;

    // Resolve the current HEAD state (branch name, commit SHA, detached/unborn).
    Expected<HeadState> head() const;

    // Read merge-in-progress state from the repository (state == MERGE, MERGE_MSG,
    // and the index conflict iterator). Derived every call — never cached (D30).
    Expected<MergeState> mergeState() const;

    /// Rebase-in-progress state, derived from disk every call (D30). Never errors:
    /// a not-rebasing repo returns a default (inProgress == false).
    RebaseState rebaseState() const;

    // Ahead/behind of the current branch versus its upstream remote-tracking
    // ref. hasUpstream is false (ahead/behind 0) when the branch has no upstream
    // or HEAD is unborn/detached. See SyncStatus.
    Expected<SyncStatus> syncStatus() const;

    /// Commits `localOid` is ahead of / behind `baseOid`, via git_graph_ahead_behind
    /// on this repository. Both OIDs must be reachable here (else an error). Returns
    /// {ahead, behind}. Used for a submodule's current HEAD vs its pinned commit.
    Expected<std::pair<int, int>> aheadBehind(std::string localOid, std::string baseOid) const;

    /// Full OIDs of commits reachable from HEAD but from no remote-tracking ref
    /// (`refs/remotes/*`) — i.e. the local-only, not-yet-pushed commits. Drives the
    /// History "local only" cue. Empty when HEAD is fully pushed or unborn; every
    /// HEAD commit when there is no remote at all.
    Expected<std::vector<std::string>> localOnlyOids() const;

    // Read/write the pull reconciliation strategy, persisted in git config
    // (pull.rebase: true => Rebase, absent/false => FastForwardOnly).
    Expected<PullStrategy> pullStrategy() const;
    Expected<void>         setPullStrategy(PullStrategy strategy);

    // --- Identity (user.name / user.email) management ---
    // GitTide materializes the resolved identity into git config so every path
    // that reads git_signature_default (commit, reword, rebase, merge) picks it up
    // and the CLI stays consistent. Local writes carry a gittide.identity marker
    // so GitTide only ever overwrites/clears identity it set itself.

    // Write user.name / user.email into the repo's LOCAL config and record the
    // ownership marker gittide.identity = <marker> (the assigning identity's id).
    Expected<void> setLocalIdentity(std::string name, std::string email, std::string marker);

    // Delete user.name / user.email / gittide.identity from the repo's LOCAL
    // config so it falls back to global. Missing keys are tolerated (no error).
    Expected<void> clearLocalIdentity();

    // Write user.name / user.email into the GLOBAL config (~/.gitconfig), creating
    // the file if it does not exist yet. Static: the global config is not tied to
    // any repository (it only needs libgit2 initialised).
    static Expected<void> setGlobalIdentity(std::string name, std::string email);

    // The effective identity from merged config (what git_signature_default uses),
    // for display. name/email may be empty if unset at every level.
    Expected<ConfigIdentity> effectiveIdentity() const;

    // The LOCAL-level identity plus whether GitTide's ownership marker is present,
    // so the UI can distinguish GitTide-managed from CLI-set local config.
    Expected<LocalIdentityInfo> localIdentity() const;

    // The configured URL of the named remote (e.g. "origin"), so the ui can match
    // a remote's host to a stored credential without libgit2 leaking into ui/.
    Expected<std::string> remoteUrl(std::string remoteName) const;

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

    /// Initialise/update every DIRECT submodule of this repo to its pinned
    /// commit (one level — does not recurse into nested submodules). Mirrors
    /// `git submodule update --init` without `--recursive`. Returns the first
    /// failure encountered; submodules processed before it stay updated.
    Expected<void> updateSubmodules();

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

    /// Number of entries currently on the stash stack (stash@{0}, {1}, …).
    /// Zero when the stack is empty. Errors only on a libgit2 failure.
    Expected<int> stashCount() const;

    /// Enumerate the stash stack, newest first (index 0..n-1). Empty when no stashes.
    /// Errors only on a libgit2 failure.
    Expected<std::vector<StashEntry>> stashList() const;

    /// Apply stash@{index} onto the working tree, keeping it on the stack. Errors
    /// (and preserves the stash) on conflict.
    Expected<void> stashApplyAt(std::size_t index);
    /// Apply stash@{index} and drop it on success. Errors (and preserves the stash)
    /// on conflict — never drops a stash it could not cleanly apply.
    Expected<void> stashPopAt(std::size_t index);
    /// Drop stash@{index} without applying it.
    Expected<void> stashDrop(std::size_t index);
    /// Drop every entry on the stack (high index → low so indices stay valid).
    Expected<void> stashClear();

    /// Files contained in the stash commit @p oid (a StashEntry::oid), matching
    /// `git stash show -u`: the tracked changes (base tree → stash tree) UNION the
    /// untracked files captured in the stash's untracked parent (present only when
    /// the stash was taken with --include-untracked). Untracked files appear as
    /// added. Used to populate the stash-preview file list.
    Expected<std::vector<FileStatus>> stashFiles(std::string oid) const;
    /// Diff of one file within stash commit @p oid. A tracked file diffs its base
    /// tree against the stash tree; an untracked file (only in the untracked
    /// parent) diffs against an empty tree, so it shows as fully added. Returns an
    /// empty DiffResult if @p file is in neither.
    Expected<DiffResult> stashDiff(std::string oid, const std::filesystem::path& file) const;

    /// Analyse and perform a merge of local branch `name` into current HEAD.
    /// FF when possible (moves HEAD, no merge commit); otherwise a normal merge,
    /// writing conflict markers into the worktree on conflict. Caller handles a
    /// dirty tree (stash) and, on conflict, drives resolution + commitMerge.
    /// See MergeOutcome.
    Expected<MergeOutcome> mergeBranch(std::string name);

    /// Abort an in-progress merge: reset --hard to HEAD and clear MERGE_HEAD/MERGE_MSG,
    /// returning the worktree to its pre-merge state.
    Expected<void> abortMerge();

    /// Rebase the current branch onto local branch `ontoRef`'s tip.
    /// Assumes a clean worktree (controller auto-stashes, D31). Drives every step;
    /// returns conflicted==true paused on the first conflicting step (state left on
    /// disk), else finishes. Errors: unborn/detached HEAD, ontoRef missing, a rebase
    /// or merge already in progress.
    Expected<RebaseOutcome> startRebase(std::string ontoRef);

    /// Begin an interactive rebase of the current branch per `todo` (base + ordered
    /// entries, oldest first). Assumes a clean worktree (controller auto-stashes,
    /// D31). Detaches HEAD at todo.base, then drives until the first pause (conflict
    /// or message) or a clean finish. Errors: unborn/detached HEAD, base not an
    /// ancestor of HEAD, first entry squash/fixup, all-drop plan, a rebase/merge
    /// already in progress.
    Expected<RebaseOutcome> startInteractiveRebase(RebaseTodo todo);

    /// Continue an in-progress rebase. For an interactive Message pause, `message`
    /// MUST be supplied (the reword/combined-squash text). For a Conflict pause the
    /// resolved index is committed; pass nullopt. Errors if not rebasing, conflicts
    /// remain, or a Message pause is continued without a message.
    Expected<RebaseOutcome> continueRebase(std::optional<std::string> message = std::nullopt);

    /// Skip the current rebase step without committing it, then advance.
    Expected<RebaseOutcome> skipRebase();
    /// Abort an in-progress rebase: restore the exact pre-rebase HEAD and worktree.
    Expected<void> abortRebase();

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

    // Rewrite HEAD's commit message via git_commit_amend, keeping the tree and
    // parents exactly (working tree/index untouched, submodule pointers preserved).
    // Errors on an unborn or detached HEAD. Returns the new commit's hex oid.
    Expected<std::string> rewordHead(std::string newMessage);

    // Undo the most recent commit (git reset --soft HEAD~1): move the current
    // branch to HEAD's first parent, leaving the index and working tree intact so
    // the undone commit's changes stay staged. Errors on an unborn branch, a
    // detached HEAD, a root commit (no parent), or while another operation
    // (merge / rebase / cherry-pick) is in progress.
    Expected<void> undoLastCommit();

    // Full commit message (summary + body) of the 40-char hex oid. Used to
    // pre-fill the reword dialog. Errors on a bad oid.
    Expected<std::string> commitMessage(std::string oid) const;

    /// First-parent oid (40-char hex) of `oid`. Errors if `oid` is a root commit
    /// (no parent) — this is what makes "Edit history from here…" on the initial
    /// commit fail cleanly (spec §2.6).
    Expected<std::string> firstParent(std::string oid) const;

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

    // Resolve a stash commit's three trees: its own tree, its base (parent 0), and
    // the untracked-files tree (parent 2, present only for --include-untracked
    // stashes — nullptr otherwise). Each non-null out-tree is owned by the caller.
    Expected<void> stashTrees(const std::string& oid, git_tree** outStash,
                              git_tree** outBase, git_tree** outUntracked) const;

    // Best-effort read of rebase-merge/onto_name (the target's label). Empty if absent.
    std::string rebaseOntoName() const;

    // Advance an open rebase: next→(conflict? pause : commit) until GIT_ITEROVER
    // (then git_rebase_finish). GIT_EAPPLIED steps are skipped. Does not free rebase/sig.
    Expected<RebaseOutcome> driveRebase(git_rebase* rebase, git_signature* sig);

    // Interactive (manual cherry-pick) engine state, persisted under
    // interactiveRebaseDir(). See rebase-interactive.md §2.3.
    struct InteractiveState
    {
        RebaseTodo  todo;
        int         done    = 0;     ///< fully-committed entries
        bool        applied = false; ///< current entry cherry-picked, awaiting commit/message
        std::string branch;          ///< branch shorthand being rewritten
        std::string origHead;        ///< pre-rebase branch tip (for abort)
    };

    std::filesystem::path interactiveRebaseDir() const;   // <gitdir>/gittide-rebase
    bool interactiveRebaseInProgress() const;             // the dir exists
    Expected<void> initInteractiveState(const RebaseTodo& todo, const std::string& branch,
                                        const std::string& origHead);
    Expected<InteractiveState> loadInteractiveState() const;
    Expected<void> setInteractiveProgress(int done, bool applied) const;
    void clearInteractiveState() const;
    // The manual driver: apply entries from the cursor until a pause or finish.
    Expected<RebaseOutcome> driveInteractive(std::optional<std::string> message);
    // All entries applied → move the branch to detached HEAD and reattach.
    Expected<RebaseOutcome> finishInteractive(const InteractiveState& st);
    // Build a RebaseState from the interactive state dir (D30).
    RebaseState interactiveRebaseState() const;

    // Low-level: checkout the commit identified by targetCommit, then update
    // HEAD to refToSet (or detach if refToSet is empty). Auto-stashes dirty
    // working tree and pops afterwards. On pop conflict the stash is preserved
    // and an error is returned.
    Expected<void> safeSwitch(const git_oid& targetCommit, const std::string& refToSet);
};

} // namespace gittide
