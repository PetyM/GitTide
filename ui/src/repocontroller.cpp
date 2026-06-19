#include "gittide/ui/repocontroller.hpp"

#include <filesystem>
#include <utility>

#include <QPointer>

#include "gittide/graphbuilder.hpp"
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
}

void RepoController::open(const QString& path)
{
    auto result = AsyncRepo::open(qstringToPath(path));
    if (!result)
    {
        m_repo.reset();
        m_path.clear();
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

} // namespace gittide::ui
