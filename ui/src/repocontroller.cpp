#include "gittide/ui/repocontroller.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

#include <QDir>
#include <QPointer>
#include <QVariant>

#include "gittide/graphbuilder.hpp"
#include "gittide/log.hpp"
#include "gittide/ui/autherror.hpp"
#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/repowatcher.hpp"

namespace gittide::ui {

namespace {
// RAII: mute the live-refresh watcher for the duration of a controller mutation
// so our own disk writes do not trigger a redundant refresh (D35). Holds a
// QPointer so a controller destroyed mid-coroutine never dereferences a freed
// watcher when the guard unwinds.
struct WatchMute
{
    explicit WatchMute(RepoWatcher* w)
        : m_w(w)
    {
        if (m_w)
            m_w->mute();
    }
    ~WatchMute()
    {
        if (m_w)
            m_w->unmute();
    }
    WatchMute(const WatchMute&)            = delete;
    WatchMute& operator=(const WatchMute&) = delete;

private:
    QPointer<RepoWatcher> m_w;
};
} // namespace

RepoController::RepoController(QObject* parent, int watchDebounceMs)
    : QObject(parent)
    , m_watcher(new RepoWatcher(watchDebounceMs, this))
{
    connect(m_watcher, &RepoWatcher::worktreeChanged, this, [this]() { QCoro::connect(onWatchWorktree(), this, []() {}); });
    connect(m_watcher, &RepoWatcher::gitDirChanged, this, [this]() { QCoro::connect(onWatchGitDir(), this, []() {}); });

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
        m_watcher->setActiveFile(QString());
        m_watcher->clear();
        logf(LogLevel::Warning, logcat::UI, "open repo '{}' failed: {}", path.toStdString(), result.error().message);
        emit repoFailed(path, QString::fromStdString(result.error().message));
        return;
    }
    m_repo.emplace(std::move(*result));
    m_path = path;
    // New repo: no file on screen yet — drop the previous repo's per-file watch.
    m_watcher->setActiveFile(QString());
    emit repoOpened(path);
    // Arm the live-refresh watcher for the newly-opened repo (D35).
    QCoro::connect(rearmWatch(), this, []() {});
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
    // Watch the on-screen file so in-place content edits (which directory watches
    // miss) refresh its diff. Absolute path = workdir + repo-relative path.
    if (m_watcher && !path.isEmpty())
        m_watcher->setActiveFile(QDir(m_path).filePath(path));
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
    WatchMute                mute(m_watcher);
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
    WatchMute                mute(m_watcher);
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
    WatchMute                mute(m_watcher);
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
    WatchMute                mute(m_watcher);
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

QCoro::Task<void> RepoController::refreshGraph(unsigned limit)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    auto result = co_await m_repo->logAllRefs(limit);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit graphReady(gittide::GraphBuilder::build(std::move(*result)));

    auto tips = co_await m_repo->refTips();
    if (!self || !tips)
        co_return;
    QHash<QString, QStringList> map;
    for (const auto& t : *tips)
        map[QString::fromStdString(t.oid)] << QString::fromStdString(t.name);
    emit refTipsReady(std::move(map));
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
    WatchMute                mute(m_watcher);
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

QCoro::Task<void> RepoController::undoLastCommit()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->undoLastCommit();
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    // The undone commit's changes are now staged: refresh the working-tree status,
    // the history (tip removed), and branch tips.
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

QCoro::Task<void> RepoController::refreshAll()
{
    QPointer<RepoController> self = this;
    co_await refreshStatus();
    if (!self)
        co_return;
    co_await refreshBranches();
    if (!self)
        co_return;
    co_await refreshHistory();
    if (!self)
        co_return;
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::rearmWatch()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto t = co_await m_repo->watchTargets();
    if (!self || !m_watcher)
        co_return;
    if (t)
        m_watcher->watch(*t);
}

QCoro::Task<void> RepoController::onWatchWorktree()
{
    QPointer<RepoController> self = this;
    // Mute while we refresh: libgit2 reads (status, ref lookups) can touch on-disk
    // caches under .git, which would otherwise re-trigger the watcher in a loop.
    WatchMute mute(m_watcher);
    co_await refreshStatus();
    if (!self)
        co_return;
    co_await rearmWatch();
}

QCoro::Task<void> RepoController::onWatchGitDir()
{
    QPointer<RepoController> self = this;
    WatchMute                mute(m_watcher);
    co_await refreshAll();
    if (!self)
        co_return;
    co_await rearmWatch();
    if (self)
        emit gitDirRefreshed();
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

    if (out->pause == gittide::RebasePause::None) // finished in one run
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    // else: conflicted or message pause → leave mid-rebase; deferred pop waits.

    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::continueRebase(QString message)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto out = co_await m_repo->continueRebase(message);
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await refreshAfterRebase();
        co_return;
    }
    if (out->pause == gittide::RebasePause::None) // clean finish only
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
    if (out->pause == gittide::RebasePause::None) // clean finish only
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
// Interactive rebase seed (Task 8)
// ---------------------------------------------------------------------------

QCoro::Task<void> RepoController::buildRebaseTodo(QString fromOid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    // Fetch history newest-first (limit 1000 — same as refreshHistory default).
    auto hist = co_await m_repo->log(1000);
    if (!self)
        co_return;
    if (!hist)
    {
        emit operationFailed(QString::fromStdString(hist.error().message));
        co_return;
    }

    // Walk from HEAD down until we find fromOid; collect those rows newest-first.
    const std::string from = fromOid.toStdString();
    std::vector<std::pair<QString, QString>> picked; // (oid, summary), newest-first
    bool found = false;
    for (const auto& node : *hist)
    {
        picked.emplace_back(QString::fromStdString(node.oid),
                            QString::fromStdString(node.summary));
        if (node.oid == from)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        emit operationFailed(QStringLiteral("commit is not on the current branch history"));
        co_return;
    }

    // Resolve base = first parent of fromOid.
    auto baseOid = co_await m_repo->firstParent(fromOid);
    if (!self)
        co_return;
    if (!baseOid)
    {
        emit operationFailed(QString::fromStdString(baseOid.error().message));
        co_return;
    }
    const QString base = QString::fromStdString(*baseOid);

    // Reverse to oldest-first.
    QVariantList entries;
    for (auto it = picked.rbegin(); it != picked.rend(); ++it)
    {
        QVariantMap m;
        m.insert(QStringLiteral("oid"), it->first);
        m.insert(QStringLiteral("summary"), it->second);
        entries.push_back(m);
    }
    emit rebaseTodoReady(base, entries);
}

QCoro::Task<void> RepoController::buildSquashTodo(QStringList oids)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    if (oids.size() < 2)
    {
        emit operationFailed(QStringLiteral("select at least two commits to squash"));
        co_return;
    }

    auto hist = co_await m_repo->log(1000);
    if (!self)
        co_return;
    if (!hist)
    {
        emit operationFailed(QString::fromStdString(hist.error().message));
        co_return;
    }

    // Locate each selected oid in the history (newest-first). The selection must
    // be a single contiguous block (indices lo..hi with no gaps) — squash folds
    // each commit into the previous one, which only makes sense for a run.
    std::set<std::string> want;
    for (const auto& o : oids)
        want.insert(o.toStdString());

    int lo = -1, hi = -1, found = 0;
    for (int i = 0; i < static_cast<int>(hist->size()); ++i)
    {
        if (want.count((*hist)[i].oid))
        {
            if (lo < 0)
                lo = i;
            hi = i;
            ++found;
        }
    }
    if (found != static_cast<int>(want.size()) || (hi - lo + 1) != found)
    {
        emit operationFailed(QStringLiteral("can only squash a contiguous range of commits"));
        co_return;
    }

    // Oldest selected commit sits at the deepest index (hi). Detach onto its parent.
    const QString oldest = QString::fromStdString((*hist)[hi].oid);
    auto baseOid = co_await m_repo->firstParent(oldest);
    if (!self)
        co_return;
    if (!baseOid)
    {
        emit operationFailed(QString::fromStdString(baseOid.error().message));
        co_return;
    }
    const QString base = QString::fromStdString(*baseOid);

    // Entries oldest-first: the oldest is the pick everything folds into; the rest
    // are squash (combined message, with a pause to edit it).
    QVariantList entries;
    for (int i = hi; i >= lo; --i)
    {
        QVariantMap m;
        m.insert(QStringLiteral("oid"), QString::fromStdString((*hist)[i].oid));
        m.insert(QStringLiteral("summary"), QString::fromStdString((*hist)[i].summary));
        m.insert(QStringLiteral("action"), i == hi ? QStringLiteral("pick") : QStringLiteral("squash"));
        entries.push_back(m);
    }
    emit rebaseTodoReady(base, entries);
}

QCoro::Task<void> RepoController::startInteractiveRebase(QString base, QStringList actions, QStringList oids)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    gittide::RebaseTodo todo;
    todo.base = base.toStdString();
    for (int i = 0; i < actions.size() && i < oids.size(); ++i)
    {
        gittide::RebaseTodoEntry e;
        const QString a = actions.at(i);
        e.action = a == "reword" ? gittide::RebaseAction::Reword
                 : a == "squash" ? gittide::RebaseAction::Squash
                 : a == "fixup"  ? gittide::RebaseAction::Fixup
                 : a == "drop"   ? gittide::RebaseAction::Drop
                                 : gittide::RebaseAction::Pick;
        e.oid = oids.at(i).toStdString();
        todo.entries.push_back(e);
    }

    auto saved = co_await m_repo->stashSave("gittide: auto-stash before rebase");
    if (!self)
        co_return;
    if (!saved)
    {
        emit operationFailed(QString::fromStdString(saved.error().message));
        co_return;
    }
    m_pendingStashPop = *saved;

    auto out = co_await m_repo->startInteractiveRebase(todo);
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await popPendingStash();
        if (!self)
            co_return;
        co_await refreshAfterRebase();
        co_return;
    }

    if (out->pause == gittide::RebasePause::None) // finished in one run
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    // else: paused (conflict or message) → banner drives; deferred pop waits.

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
