#include "gittide/ui/repocontroller.hpp"

#include <filesystem>
#include <utility>

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
    auto result = AsyncRepo::open(std::filesystem::path(path.toStdString()));
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
    auto result = co_await m_repo->status();
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
    auto result = co_await m_repo->diff(target, std::filesystem::path(path.toStdString()));
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
    auto result = co_await m_repo->stage(sel);
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
    auto result = co_await m_repo->unstage(sel);
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
    auto result = co_await m_repo->discard(sel);
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
    auto result = co_await m_repo->commit(req);
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
    auto result = co_await m_repo->log(limit);
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
    auto list = co_await m_repo->branches();
    if (!list)
    {
        emit operationFailed(QString::fromStdString(list.error().message));
        co_return;
    }
    emit branchesChanged(*list);
    auto h = co_await m_repo->head();
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
    auto r = co_await m_repo->checkoutBranch(name);
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
    auto r = co_await m_repo->createBranch(name, fromOid);
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    if (checkout)
    {
        auto sw = co_await m_repo->checkoutBranch(name);
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
    auto r = co_await m_repo->checkoutCommit(oid);
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
    auto r = co_await m_repo->deleteBranch(name, force);
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
    auto r = co_await m_repo->renameBranch(oldName, newName, /*force=*/false);
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
}

} // namespace gittide::ui
