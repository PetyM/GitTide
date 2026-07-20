#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <filesystem>
#include <optional>
#include <qcorotask.h>
#include <vector>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"
#include "gittide/rebase.hpp"
#include "gittide/sync.hpp"
#include "gittide/ui/asyncrepo.hpp"

Q_DECLARE_METATYPE(gittide::RebaseState)
Q_DECLARE_METATYPE(gittide::StashEntry)
Q_DECLARE_METATYPE(std::vector<gittide::StashEntry>)

namespace gittide::ui {

class RepoWatcher;

// Holds the active repository for a window and drives it asynchronously. open()
// is synchronous (cheap); all git work runs through AsyncRepo on the thread pool.
// Coroutine slots take args BY VALUE so they survive a co_await suspension.
//
// Live refresh (D35): the controller owns a RepoWatcher pointed at the open repo;
// a working-tree change re-runs refreshStatus, a git-dir change re-runs the full
// cascade (refreshAll), and the watch set is re-armed after each batch.
class RepoController : public QObject
{
    Q_OBJECT
public:
    /// @param watchDebounceMs coalescing window for the live-refresh watcher
    /// (injectable so tests can run fast).
    explicit RepoController(QObject* parent = nullptr, int watchDebounceMs = 300);

    bool isOpen() const
    {
        return m_repo.has_value();
    }
    QString path() const
    {
        return m_path;
    }

public slots:
    void open(const QString& path);
    QCoro::Task<void> refreshStatus();
    QCoro::Task<void> refreshDiff(QString path, gittide::DiffTarget target);
    QCoro::Task<void> stage(gittide::StageSelection sel);
    QCoro::Task<void> unstage(gittide::StageSelection sel);
    QCoro::Task<void> discard(gittide::StageSelection sel);
    QCoro::Task<void> discardAll();
    /// Stash the working tree (no message); refreshes status + stash count.
    QCoro::Task<void> stashChanges();
    /// Pop the most-recent stash; refreshes status + stash count.
    QCoro::Task<void> popStash();
    /// Apply stash@{index}, keeping it; refreshes status + stash list. Conflict →
    /// operationFailed, stash preserved.
    QCoro::Task<void> applyStashAt(int index);
    /// Pop stash@{index} (apply + drop); refreshes status + stash list. Conflict →
    /// operationFailed, stash preserved.
    QCoro::Task<void> popStashAt(int index);
    /// Drop stash@{index}; refreshes the stash list.
    QCoro::Task<void> dropStash(int index);
    /// Clear the whole stack; refreshes the stash list.
    QCoro::Task<void> clearStashes();
    /// Re-read the stash count and list from disk; emits stashCountChanged and
    /// stashListReady. Called on open() and after every stash operation.
    QCoro::Task<void> refreshStashState();
    QCoro::Task<void> commit(gittide::CommitRequest req);
    QCoro::Task<void> refreshHistory(unsigned limit = 1000);
    QCoro::Task<void> refreshGraph(unsigned limit = 1000);
    QCoro::Task<void> refreshBranches();
    QCoro::Task<void> createBranch(QString name, QString fromOid, bool checkout);
    QCoro::Task<void> switchBranch(QString name);
    QCoro::Task<void> checkoutRemoteBranch(QString remoteShorthand);
    QCoro::Task<void> checkoutCommit(QString oid);
    QCoro::Task<void> deleteBranch(QString name, bool force);
    QCoro::Task<void> renameBranch(QString oldName, QString newName);
    // Stage-on-commit (D23): reset index to HEAD, stage each selection, commit,
    // then refresh status + history. Empty selections => no-op + operationFailed.
    QCoro::Task<void> commitSelection(gittide::CommitRequest req,
                                      std::vector<gittide::StageSelection> selections);
    // Read-only history diff:
    QCoro::Task<void> refreshCommitFiles(QString oid);
    QCoro::Task<void> refreshCommitDiff(QString oid, QString path);
    /// Load a stash commit's files (tracked + untracked) for preview; emits the
    /// same commitFilesReady signal so the ViewModel's commit models are reused.
    QCoro::Task<void> refreshStashPreviewFiles(QString oid);
    /// Load one file's diff within a stash commit (tracked or untracked); emits
    /// commitDiffReady. Untracked files show as fully added.
    QCoro::Task<void> refreshStashPreviewDiff(QString oid, QString path);
    // Read-only range diff (combined across a commit span):
    QCoro::Task<void> refreshRangeFiles(QString oldOid, QString newOid);
    QCoro::Task<void> refreshRangeDiff(QString oldOid, QString newOid, QString path);
    // Reword HEAD commit message; emits committed then refreshes status/history/branches.
    QCoro::Task<void> rewordHead(QString message);
    // Undo the last commit (soft reset, keep changes staged); refreshes status/history/branches.
    QCoro::Task<void> undoLastCommit();
    // Fetch the commit message for a given OID; emits commitMessageReady on success.
    QCoro::Task<void> requestCommitMessage(QString oid);

    QCoro::Task<void> refreshSyncStatus();
    /// Full refresh cascade: status + branches + history + sync. Used on open, on
    /// a watcher-detected git-dir change, and on window-focus resync (D35).
    QCoro::Task<void> refreshAll();
    QCoro::Task<void> fetch(gittide::Credentials cred);
    QCoro::Task<void> pull(gittide::Credentials cred);
    QCoro::Task<void> push(QString branch, bool setUpstream, gittide::Credentials cred);
    /// The URL of the "origin" remote for the active repo (empty if none / on
    /// error). Lets the ViewModel look up a keychain credential for it.
    QCoro::Task<QString> currentRemoteUrl();
    QCoro::Task<void> loadPullStrategy();
    QCoro::Task<void> setPullStrategy(gittide::PullStrategy strategy);

    /// Merge the named branch into HEAD with auto-stash (D31). Handles
    /// UpToDate, FastForward, clean Normal, and conflicted Normal cases.
    /// Emits mergeFinished on success; leaves repo mid-merge on conflict.
    QCoro::Task<void> merge(QString name);

    /// Create the merge commit from the current (partially-resolved) index.
    /// Pops the deferred auto-stash and emits mergeFinished on success.
    QCoro::Task<void> commitMerge(gittide::CommitRequest req);

    /// Abort an in-progress merge: reset working tree to HEAD, pop the
    /// deferred auto-stash, and re-init any deinited submodules.
    QCoro::Task<void> abortMerge();

    /// Abort the current conflicted merge, deinit each conflicted submodule
    /// so the gitlink can merge as a plain pointer, then re-run merge(name).
    /// Re-init is deferred to the eventual commitMerge or abortMerge.
    QCoro::Task<void> retryMergeDeinitSubmodules(QString name);

    /// Rebase the current branch onto ontoRef. Auto-stashes a dirty tree (D31),
    /// drives the first run; on a clean finish emits rebaseFinished + pops the stash,
    /// on conflict leaves the repo mid-rebase (pop deferred to continue/abort).
    QCoro::Task<void> startRebase(QString ontoRef);
    /// Continue after resolving the current step's conflicts, optionally supplying
    /// a commit message for a Message pause (reword/squash). Empty string → nullopt.
    QCoro::Task<void> continueRebase(QString message = QString());
    /// Skip the current step.
    QCoro::Task<void> skipRebase();
    /// Abort the rebase, restoring the pre-rebase state, and pop the auto-stash.
    QCoro::Task<void> abortRebase();

    /// Build the interactive-rebase todo for fromOid..HEAD (oldest first) and emit
    /// rebaseTodoReady with the detach base (fromOid's first parent).
    QCoro::Task<void> buildRebaseTodo(QString fromOid);

    /// Squash a set of selected commit oids. The oids must form a contiguous range
    /// in the current history (else operationFailed). Builds the plan with the oldest
    /// selected commit as `pick` and the rest as `squash` (oldest-first), base =
    /// parent of the oldest selected, then starts the interactive rebase directly via
    /// startInteractiveRebase — no todo editor; the engine pauses on the combined
    /// message (RebasePause::Message).
    QCoro::Task<void> buildSquashTodo(QStringList oids);

    /// Start an interactive rebase from a seed plan. Auto-stashes (D31), drives the
    /// first run; clean finish emits rebaseFinished + pops the stash; a pause leaves
    /// the repo mid-rebase. `actions[i]` is one of pick/reword/squash/fixup/drop.
    QCoro::Task<void> startInteractiveRebase(QString base, QStringList actions, QStringList oids);

    /// Read the UTF-8 content of a working-tree file at @p relPath (relative to
    /// the repository root). Returns an empty string if the file cannot be read.
    /// Keeps filesystem access inside the controller; the VM must not touch the FS.
    QString readWorkingFile(const QString& relPath) const;

    /// Write @p content (UTF-8) to the working-tree file at @p relPath (relative
    /// to the repository root). Synchronous and cheap; does not need a coroutine.
    void writeWorkingFile(const QString& relPath, const QString& content);

signals:
    void repoOpened(const QString& path);
    void repoFailed(const QString& path, const QString& message);
    void statusChanged(const std::vector<gittide::FileStatus>& files);
    void diffReady(const QString& path, const gittide::DiffResult& result);
    void committed(const QString& oid);
    void historyReady(gittide::GraphLayout layout);
    void graphReady(gittide::GraphLayout layout);
    void refTipsReady(QHash<QString, QStringList> oidToLabels);
    void operationFailed(const QString& message);
    void deleteFailedUnmerged(const QString& name);
    void branchesChanged(std::vector<gittide::BranchInfo>);
    void headChanged(gittide::HeadState);
    void commitFilesReady(QString oid, std::vector<gittide::FileStatus> files);
    void commitDiffReady(QString oid, QString path, gittide::DiffResult result);
    void commitMessageReady(QString oid, QString message);
    void rangeFilesReady(QString oldOid, QString newOid, std::vector<gittide::FileStatus> files);
    void rangeDiffReady(QString oldOid, QString newOid, QString path, gittide::DiffResult result);

    void syncStatusChanged(gittide::SyncStatus status);
    void pullStrategyChanged(gittide::PullStrategy strategy);

    /// Emitted whenever the stash count is refreshed (on open, after a git-dir
    /// change, and after stashChanges/popStash). Feeds the VM's stashAvailable.
    void stashCountChanged(int count);

    /// Emitted whenever the stash list is refreshed (on open, after a git-dir change,
    /// and after any stash op). Feeds the VM's StashListModel. Newest first.
    void stashListReady(std::vector<gittide::StashEntry> entries);

    /// Emitted whenever the merge-in-progress state is refreshed (D30).
    /// Always reflects disk truth — never a cached/in-memory flag.
    void mergeStateChanged(gittide::MergeState state);

    /// Emitted when a merge finishes successfully (FF, clean Normal, or
    /// commitMerge). headOid is the new HEAD commit OID.
    void mergeFinished(QString headOid);

    /// Emitted whenever rebase-in-progress state is refreshed (D30).
    void rebaseStateChanged(gittide::RebaseState state);
    /// Emitted when a rebase finishes cleanly. headOid is the new HEAD commit OID.
    void rebaseFinished(QString headOid);

    /// Emitted with the seed plan for the interactive editor. `entries` is a list of
    /// QVariantMap{oid, summary}, oldest first; `base` is the detach commit oid.
    void rebaseTodoReady(QString base, QVariantList entries);

    /// Emitted at the end of a git-dir-watch refresh cycle (D35). Allows
    /// upstream listeners to react to structural changes (e.g. submodule updates)
    /// detected via the .git/modules/... watcher.
    void gitDirRefreshed();

    void syncBusyChanged(bool busy);
    // Transfer progress for the in-flight fetch/pull/push: objects received of
    // total. total == 0 means the count is not yet known (indeterminate).
    void syncProgressChanged(unsigned received, unsigned total);
    void authFailed(QString remoteUrl);

private:
    // Builds a ProgressCallback that marshals worker-thread transfer counts onto
    // this object's thread as syncProgressChanged. Safe if the controller dies
    // mid-transfer: the queued call is dropped with the object.
    gittide::ProgressCallback progressSink();

    // Pop the pending auto-stash if one was saved.
    QCoro::Task<void> popPendingStash();

    // Refresh status (including mergeState → mergeStateChanged) + history +
    // branches + sync. Used as the tail of every merge operation.
    QCoro::Task<void> refreshAfterMerge();

    // Status(+rebaseState) + history + branches + sync. Tail of every rebase op.
    QCoro::Task<void> refreshAfterRebase();

    // Re-init every path in m_pendingSubmoduleReinit, then clear the list.
    QCoro::Task<void> reinitPendingSubmodules();

    // Re-fetch the watch set from the repo and re-arm the RepoWatcher (D35).
    QCoro::Task<void> rearmWatch();
    // Watcher-driven handlers: worktree change → status + re-arm; git-dir change
    // → full cascade + re-arm.
    QCoro::Task<void> onWatchWorktree();
    QCoro::Task<void> onWatchGitDir();

    // Return the short HEAD branch name, or "HEAD" when detached.
    std::string currentBranchName();

    std::optional<AsyncRepo> m_repo;
    QString m_path;

    // Live-refresh watcher for the open repo (D35); child QObject, owns its own
    // QFileSystemWatcher + debounce timer.
    RepoWatcher* m_watcher = nullptr;

    // Orchestration bookkeeping (D31) — NOT merge-state; D30 governs that.
    bool m_pendingStashPop = false;
    std::vector<std::filesystem::path> m_pendingSubmoduleReinit;

    // Last-known HEAD state, updated by refreshBranches() so currentBranchName()
    // can return a real branch name without an extra async round-trip.
    gittide::HeadState m_lastHead;
};

} // namespace gittide::ui
