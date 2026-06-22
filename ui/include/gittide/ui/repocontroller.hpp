#pragma once
#include <QObject>
#include <QString>
#include <filesystem>
#include <optional>
#include <qcorotask.h>
#include <vector>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/graph.hpp"
#include "gittide/merge.hpp"
#include "gittide/sync.hpp"
#include "gittide/ui/asyncrepo.hpp"

namespace gittide::ui {

// Holds the active repository for a window and drives it asynchronously. open()
// is synchronous (cheap); all git work runs through AsyncRepo on the thread pool.
// Coroutine slots take args BY VALUE so they survive a co_await suspension.
class RepoController : public QObject
{
    Q_OBJECT
public:
    explicit RepoController(QObject* parent = nullptr);

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
    QCoro::Task<void> commit(gittide::CommitRequest req);
    QCoro::Task<void> refreshHistory(unsigned limit = 1000);
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

    QCoro::Task<void> refreshSyncStatus();
    QCoro::Task<void> fetch(gittide::Credentials cred);
    QCoro::Task<void> pull(gittide::Credentials cred);
    QCoro::Task<void> push(QString branch, bool setUpstream, gittide::Credentials cred);
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

signals:
    void repoOpened(const QString& path);
    void repoFailed(const QString& path, const QString& message);
    void statusChanged(const std::vector<gittide::FileStatus>& files);
    void diffReady(const QString& path, const gittide::DiffResult& result);
    void committed(const QString& oid);
    void historyReady(gittide::GraphLayout layout);
    void operationFailed(const QString& message);
    void deleteFailedUnmerged(const QString& name);
    void branchesChanged(std::vector<gittide::BranchInfo>);
    void headChanged(gittide::HeadState);
    void commitFilesReady(QString oid, std::vector<gittide::FileStatus> files);
    void commitDiffReady(QString oid, QString path, gittide::DiffResult result);

    void syncStatusChanged(gittide::SyncStatus status);
    void pullStrategyChanged(gittide::PullStrategy strategy);

    /// Emitted whenever the merge-in-progress state is refreshed (D30).
    /// Always reflects disk truth — never a cached/in-memory flag.
    void mergeStateChanged(gittide::MergeState state);

    /// Emitted when a merge finishes successfully (FF, clean Normal, or
    /// commitMerge). headOid is the new HEAD commit OID.
    void mergeFinished(QString headOid);
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

    // Re-init every path in m_pendingSubmoduleReinit, then clear the list.
    QCoro::Task<void> reinitPendingSubmodules();

    // Return the short HEAD branch name, or "HEAD" when detached.
    std::string currentBranchName();

    std::optional<AsyncRepo> m_repo;
    QString m_path;

    // Orchestration bookkeeping (D31) — NOT merge-state; D30 governs that.
    bool m_pendingStashPop = false;
    std::vector<std::filesystem::path> m_pendingSubmoduleReinit;

    // Last-known HEAD state, updated by refreshBranches() so currentBranchName()
    // can return a real branch name without an extra async round-trip.
    gittide::HeadState m_lastHead;
};

} // namespace gittide::ui
