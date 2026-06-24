#include "gittide/ui/repocontroller.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include <QPointer>

#include "gittide/graphbuilder.hpp"
#include "gittide/log.hpp"
#include "gittide/ui/autherror.hpp"
#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

RepoController::RepoController(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<std::vector<gittide::FileStatus>>();
    qRegisterMetaType<gittide::DiffResult>();
    qRegisterMetaType<gittide::StageSelection>();
    qRegisterMetaType<gittide::CommitRequest>();
    qRegisterMetaType<gittide::GraphLayout>();
    qRegisterMetaType<gittide::BranchInfo>();
    qRegisterMetaType<std::vector<gittide::BranchInfo>>();
    qRegisterMetaType<gittide::HeadState>();
    qRegisterMetaType<gittide::SyncStatus>();
    qRegisterMetaType<gittide::PullStrategy>();
    qRegisterMetaType<gittide::Credentials>();
    qRegisterMetaType<gittide::MergeState>();
    qRegisterMetaType<gittide::RebaseState>();
}

void RepoController::open(const QString& path)
{
    auto result = AsyncRepo::open(qstringToPath(path));
    if (!result)
    {
        m_repo.reset();
        m_path.clear();
        logf(LogLevel::Warning, logcat::UI, "open repo '{}' failed: {}", path.toStdString(), result.error().message);
        emit repoFailed(path, QString::fromStdString(result.error().message));
        return;
    }
    m_repo.emplace(std::move(*result));
    m_path = path;
    emit repoOpened(path);
}

QCoro::Task<void> RepoController::refreshStatus()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->status();
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit statusChanged(*result);

    // D30: merge state is derived from disk on every status refresh, never cached.
    auto ms = co_await m_repo->mergeState();
    if (!self)
        co_return;
    // Intentional silent skip: a missing MERGE_HEAD is the normal not-merging
    // case and not an error worth surfacing. The next refresh re-reports truth.
    if (ms)
        emit mergeStateChanged(*ms);

    // D30: rebase state is derived from disk on every status refresh, never cached.
    auto rs = co_await m_repo->rebaseState();
    if (!self)
        co_return;
    if (rs)
        emit rebaseStateChanged(*rs);
}

QCoro::Task<void> RepoController::refreshDiff(QString path, gittide::DiffTarget target)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->diff(target, qstringToPath(path));
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit diffReady(path, *result);
}

QCoro::Task<void> RepoController::stage(gittide::StageSelection sel)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->stage(sel);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::unstage(gittide::StageSelection sel)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->unstage(sel);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::discard(gittide::StageSelection sel)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->discard(sel);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::commit(gittide::CommitRequest req)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->commit(req);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit committed(QString::fromStdString(*result));
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshSyncStatus(); // ahead count grew — refresh so Push reflects it
}

QCoro::Task<void> RepoController::refreshHistory(unsigned limit)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->log(limit);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit historyReady(gittide::GraphBuilder::build(std::move(*result)));
}

QCoro::Task<void> RepoController::refreshBranches()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto list = co_await m_repo->branches();
    if (!self)
        co_return;
    if (!list)
    {
        emit operationFailed(QString::fromStdString(list.error().message));
        co_return;
    }
    emit branchesChanged(*list);
    auto h = co_await m_repo->head();
    if (!self)
        co_return;
    if (!h)
    {
        emit operationFailed(QString::fromStdString(h.error().message));
        co_return;
    }
    m_lastHead = *h; // cache for currentBranchName()
    emit headChanged(*h);
}

QCoro::Task<void> RepoController::switchBranch(QString name)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->checkoutBranch(name);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::checkoutRemoteBranch(QString remoteShorthand)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->checkoutRemoteBranch(remoteShorthand);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
    // The new local branch tracks the remote ref, so ahead/behind is meaningful.
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::createBranch(QString name, QString fromOid, bool checkout)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->createBranch(name, fromOid);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    if (checkout)
    {
        auto sw = co_await m_repo->checkoutBranch(name);
        if (!self)
            co_return;
        if (!sw)
        {
            emit operationFailed(QString::fromStdString(sw.error().message));
            co_await refreshBranches(); // branch exists even if the switch failed
            co_return;
        }
        co_await refreshStatus();
        co_await refreshHistory();
    }
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::checkoutCommit(QString oid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->checkoutCommit(oid);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::deleteBranch(QString name, bool force)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->deleteBranch(name, force);
    if (!self)
        co_return;
    if (!r)
    {
        const QString msg = QString::fromStdString(r.error().message);
        if (msg.contains(QStringLiteral("not fully merged")))
            emit deleteFailedUnmerged(name);
        else
            emit operationFailed(msg);
        co_return;
    }
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::renameBranch(QString oldName, QString newName)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->renameBranch(oldName, newName, /*force=*/false);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::commitSelection(gittide::CommitRequest req,
                                                  std::vector<gittide::StageSelection> selections)
{
    if (!m_repo)
        co_return;
    if (selections.empty())
    {
        emit operationFailed(QStringLiteral("Nothing selected to commit"));
        co_return;
    }
    QPointer<RepoController> self = this;
    auto reset = co_await m_repo->resetIndexToHead();
    if (!self)
        co_return;
    if (!reset)
    {
        emit operationFailed(QString::fromStdString(reset.error().message));
        co_return;
    }
    for (const auto& sel : selections)
    {
        auto s = co_await m_repo->stage(sel);
        if (!self)
            co_return;
        if (!s)
        {
            emit operationFailed(QString::fromStdString(s.error().message));
            co_await refreshStatus(); // leave the user in a consistent state
            co_return;
        }
    }
    auto oid = co_await m_repo->commit(req);
    if (!self)
        co_return;
    if (!oid)
    {
        emit operationFailed(QString::fromStdString(oid.error().message));
        co_await refreshStatus();
        co_return;
    }
    emit committed(QString::fromStdString(*oid));
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshSyncStatus(); // ahead count grew — refresh so Push reflects it
}

QCoro::Task<void> RepoController::refreshCommitFiles(QString oid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto files = co_await m_repo->commitFiles(oid);
    if (!self)
        co_return;
    if (!files)
    {
        emit operationFailed(QString::fromStdString(files.error().message));
        co_return;
    }
    emit commitFilesReady(oid, *files);
}

QCoro::Task<void> RepoController::refreshCommitDiff(QString oid, QString path)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto d = co_await m_repo->commitDiff(oid, qstringToPath(path));
    if (!self)
        co_return;
    if (!d)
    {
        emit operationFailed(QString::fromStdString(d.error().message));
        co_return;
    }
    emit commitDiffReady(oid, path, *d);
}

QCoro::Task<void> RepoController::refreshRangeFiles(QString oldOid, QString newOid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto files = co_await m_repo->rangeFiles(oldOid, newOid);
    if (!self)
        co_return;
    if (!files)
    {
        emit operationFailed(QString::fromStdString(files.error().message));
        co_return;
    }
    emit rangeFilesReady(oldOid, newOid, *files);
}

QCoro::Task<void> RepoController::refreshRangeDiff(QString oldOid, QString newOid, QString path)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto d = co_await m_repo->rangeDiff(oldOid, newOid, qstringToPath(path));
    if (!self)
        co_return;
    if (!d)
    {
        emit operationFailed(QString::fromStdString(d.error().message));
        co_return;
    }
    emit rangeDiffReady(oldOid, newOid, path, *d);
}

QCoro::Task<void> RepoController::rewordHead(QString message)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->rewordHead(message);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    emit committed(QString::fromStdString(*r));
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::requestCommitMessage(QString oid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->commitMessage(oid);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    emit commitMessageReady(oid, QString::fromStdString(*r));
}

QCoro::Task<void> RepoController::refreshSyncStatus()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->syncStatus();
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    emit syncStatusChanged(*r);
}

gittide::ProgressCallback RepoController::progressSink()
{
    QPointer<RepoController> self = this;
    return [self](unsigned received, unsigned total) -> bool
    {
        if (auto* p = self.data())
            QMetaObject::invokeMethod(
                p, [p, received, total] { emit p->syncProgressChanged(received, total); },
                Qt::QueuedConnection);
        return true;
    };
}

QCoro::Task<void> RepoController::fetch(gittide::Credentials cred)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    emit syncBusyChanged(true);
    logf(LogLevel::Info, logcat::ASYNC, "fetch from 'origin' started");
    auto r = co_await m_repo->fetch(QStringLiteral("origin"), cred, progressSink());
    if (!self)
        co_return;
    emit syncBusyChanged(false);
    if (!r)
    {
        logf(LogLevel::Warning, logcat::ASYNC, "fetch failed: {}", r.error().message);
        if (isAuthError(r.error()))
            emit authFailed(QString());
        else
            emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::pull(gittide::Credentials cred)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    emit syncBusyChanged(true);
    auto r = co_await m_repo->pull(cred, progressSink());
    if (!self)
        co_return;
    emit syncBusyChanged(false);
    if (!r)
    {
        if (isAuthError(r.error()))
            emit authFailed(QString());
        else
            emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::push(QString branch, bool setUpstream, gittide::Credentials cred)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    emit syncBusyChanged(true);
    auto r = co_await m_repo->push(QStringLiteral("origin"), branch, setUpstream, cred, progressSink());
    if (!self)
        co_return;
    emit syncBusyChanged(false);
    if (!r)
    {
        if (isAuthError(r.error()))
            emit authFailed(QString());
        else
            emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::loadPullStrategy()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->pullStrategy();
    if (!self || !r)
        co_return;
    emit pullStrategyChanged(*r);
}

QCoro::Task<void> RepoController::setPullStrategy(gittide::PullStrategy strategy)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->setPullStrategy(strategy);
    if (!self || !r)
        co_return;
    emit pullStrategyChanged(strategy);
}

// ---------------------------------------------------------------------------
// Merge orchestration (D31)
// ---------------------------------------------------------------------------

QCoro::Task<void> RepoController::merge(QString name)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    // Auto-stash a dirty tree (D31). Remember whether we owe a pop.
    auto saved = co_await m_repo->stashSave("gittide: auto-stash before merge");
    if (!self)
        co_return;
    if (!saved)
    {
        emit operationFailed(QString::fromStdString(saved.error().message));
        co_return;
    }
    m_pendingStashPop = *saved;

    auto out = co_await m_repo->mergeBranch(name);
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await refreshAfterMerge(); // still report true (disk) state, D30
        co_return;
    }

    using gittide::MergeAnalysis;
    if (out->analysis == MergeAnalysis::UpToDate)
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        emit operationFailed(tr("Already up to date.")); // informational, non-fatal
    }
    else if (out->analysis == MergeAnalysis::FastForward)
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        emit mergeFinished(QString::fromStdString(out->newOid));
    }
    else if (!out->conflicted) // clean normal merge → finish immediately
    {
        const std::string msg = "Merge branch '" + name.toStdString() + "' into "
                              + currentBranchName();
        auto oid = co_await m_repo->commitMerge(gittide::CommitRequest{msg});
        if (!self)
            co_return;
        if (!oid)
        {
            emit operationFailed(QString::fromStdString(oid.error().message));
        }
        else
        {
            co_await popPendingStash();
            if (!self)
                co_return;
            co_await reinitPendingSubmodules();
            if (!self)
                co_return;
            emit mergeFinished(QString::fromStdString(*oid));
        }
    }
    // else: conflicted → leave mid-merge; the pending pop waits for commitMerge.

    co_await refreshAfterMerge(); // status(+mergeState) + history + branches + sync
}

QCoro::Task<void> RepoController::popPendingStash()
{
    if (!m_pendingStashPop)
        co_return;
    // Flip before the await so a double-call can never pop twice.
    m_pendingStashPop = false;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->stashPop();
    if (!self)
        co_return;
    if (!r)
        emit operationFailed(QString::fromStdString(r.error().message)); // stash preserved
}

QCoro::Task<void> RepoController::commitMerge(gittide::CommitRequest req)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto oid = co_await m_repo->commitMerge(req);
    if (!self)
        co_return;
    if (!oid)
    {
        emit operationFailed(QString::fromStdString(oid.error().message));
        co_await refreshAfterMerge();
        co_return;
    }
    co_await popPendingStash();         // deferred pop now safe
    if (!self)
        co_return;
    co_await reinitPendingSubmodules(); // restore any deinited submodules
    if (!self)
        co_return;
    emit mergeFinished(QString::fromStdString(*oid));
    co_await refreshAfterMerge();
}

QCoro::Task<void> RepoController::abortMerge()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->abortMerge();
    if (!self)
        co_return;
    if (!r)
    {
        // Abort failed: repo is still mid-merge/conflicted. Popping the stash
        // onto a conflicted tree would corrupt it — re-report disk truth and bail.
        emit operationFailed(QString::fromStdString(r.error().message));
        co_await refreshAfterMerge();
        co_return;
    }
    co_await popPendingStash();         // restore the user's pre-merge work
    if (!self)
        co_return;
    co_await reinitPendingSubmodules(); // if we had deinited any (Task 7)
    if (!self)
        co_return;
    co_await refreshAfterMerge();
}

QCoro::Task<void> RepoController::retryMergeDeinitSubmodules(QString name)
{
    if (!m_repo)
        co_return;
    // I-2 fix: Guard against an empty name BEFORE aborting anything. An empty name
    // means we cannot determine what to re-merge after the abort, which would leave
    // the repo in a clean state with the merge silently destroyed. Surface the error
    // instead so the user can resolve or abort manually.
    if (name.isEmpty())
    {
        emit operationFailed(
            tr("Cannot determine which branch to merge; resolve or abort this merge manually."));
        co_return;
    }
    QPointer<RepoController> self = this;
    auto ms = co_await m_repo->mergeState();
    if (!self)
        co_return;
    if (!ms)
    {
        emit operationFailed(QString::fromStdString(ms.error().message));
        co_await refreshAfterMerge(); // re-report disk truth, D30
        co_return;
    }
    // 1. Abort the conflicted merge
    if (auto r = co_await m_repo->abortMerge(); !r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    if (!self)
        co_return;
    // 2. Deinit the conflicted submodules, remembering them for re-init
    m_pendingSubmoduleReinit = ms->conflictedSubmodules;
    for (const auto& p : ms->conflictedSubmodules)
    {
        if (auto r = co_await m_repo->deinitSubmodule(p); !r)
            emit operationFailed(QString::fromStdString(r.error().message));
        if (!self)
            co_return;
    }
    // 3. After abort the working tree is clean; the user's original dirty work
    // sits in the stash (m_pendingStashPop == true from the first merge). Pop it
    // now so the inner merge() re-stashes it with a fresh m_pendingStashPop.
    co_await popPendingStash();
    if (!self)
        co_return;
    // 4. Re-run the merge; now the gitlinks merge as plain pointers.
    // re-init happens on the next commitMerge/abort via reinitPendingSubmodules()
    co_await merge(name);
}

// ---------------------------------------------------------------------------
// Rebase orchestration (D31)
// ---------------------------------------------------------------------------

QCoro::Task<void> RepoController::startRebase(QString ontoRef)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    // Auto-stash a dirty tree (D31); remember the deferred pop.
    auto saved = co_await m_repo->stashSave("gittide: auto-stash before rebase");
    if (!self)
        co_return;
    if (!saved)
    {
        emit operationFailed(QString::fromStdString(saved.error().message));
        co_return;
    }
    m_pendingStashPop = *saved;

    auto out = co_await m_repo->startRebase(ontoRef);
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await popPendingStash(); // start never began → restore the user's work
        if (!self)
            co_return;
        co_await refreshAfterRebase();
        co_return;
    }

    if (!out->conflicted) // finished in one run
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    // else: conflicted → leave mid-rebase; the deferred pop waits for finish/abort.

    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::continueRebase()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto out = co_await m_repo->continueRebase();
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await refreshAfterRebase();
        co_return;
    }
    if (!out->conflicted)
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::skipRebase()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto out = co_await m_repo->skipRebase();
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await refreshAfterRebase();
        co_return;
    }
    if (!out->conflicted)
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::abortRebase()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->abortRebase();
    if (!self)
        co_return;
    if (!r)
    {
        // Abort failed: repo is still mid-rebase/conflicted. Popping the stash
        // onto a conflicted tree would corrupt it — re-report disk truth and bail.
        emit operationFailed(QString::fromStdString(r.error().message));
        co_await refreshAfterRebase();
        co_return;
    }
    co_await popPendingStash(); // restore the user's pre-rebase work
    if (!self)
        co_return;
    co_await refreshAfterRebase();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QCoro::Task<void> RepoController::refreshAfterMerge()
{
    QPointer<RepoController> self = this;
    co_await refreshStatus(); // also fetches mergeState and emits mergeStateChanged
    if (!self)
        co_return;
    co_await refreshHistory();
    if (!self)
        co_return;
    co_await refreshBranches();
    if (!self)
        co_return;
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::refreshAfterRebase()
{
    QPointer<RepoController> self = this;
    co_await refreshStatus(); // also fetches rebaseState → rebaseStateChanged
    if (!self)
        co_return;
    co_await refreshHistory();
    if (!self)
        co_return;
    co_await refreshBranches();
    if (!self)
        co_return;
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::reinitPendingSubmodules()
{
    if (m_pendingSubmoduleReinit.empty())
        co_return;
    QPointer<RepoController> self = this;
    for (const auto& p : m_pendingSubmoduleReinit)
    {
        if (auto r = co_await m_repo->reinitSubmodule(p); !r)
            emit operationFailed(QString::fromStdString(r.error().message));
        if (!self)
            co_return;
    }
    m_pendingSubmoduleReinit.clear();
}

QString RepoController::readWorkingFile(const QString& relPath) const
{
    if (m_path.isEmpty())
        return {};
    const std::filesystem::path full = qstringToPath(m_path) / qstringToPath(relPath);
    std::ifstream ifs(full, std::ios::binary);
    if (!ifs.is_open())
        return {};
    const std::string bytes((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
    return QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
}

void RepoController::writeWorkingFile(const QString& relPath, const QString& content)
{
    if (m_path.isEmpty())
        return;
    const std::filesystem::path full = qstringToPath(m_path) / qstringToPath(relPath);
    std::ofstream ofs(full, std::ios::binary | std::ios::trunc);
    if (!ofs)
    {
        emit operationFailed(tr("Could not open %1 for writing").arg(relPath));
        return;
    }
    const QByteArray bytes = content.toUtf8();
    ofs.write(bytes.constData(), bytes.size());
    ofs.flush();
    if (!ofs)
    {
        emit operationFailed(tr("Failed to write %1").arg(relPath));
        return;
    }
}

std::string RepoController::currentBranchName()
{
    // Use the last-known HEAD state cached by refreshBranches(). Returns the real
    // short branch name when on a branch, or "HEAD" when detached or not yet known.
    // In practice merge() is always called after open() + an initial refreshBranches(),
    // so m_lastHead.branch is populated for the normal on-branch case.
    if (!m_lastHead.detached && !m_lastHead.branch.empty())
        return m_lastHead.branch;
    return "HEAD";
}

} // namespace gittide::ui
