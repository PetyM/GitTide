#include "gittide/ui/asyncrepo.hpp"

#include <QtConcurrent>
#include <core/qcorofuture.h>
#include <mutex>
#include <utility>

#include "gittide/gitrepo.hpp"

namespace gittide::ui {

struct AsyncRepo::Impl
{
    explicit Impl(gittide::GitRepo r)
        : repo(std::move(r))
    {
    }
    gittide::GitRepo repo;
    std::mutex mutex; // serializes pool access to the non-thread-safe GitRepo
};

AsyncRepo::~AsyncRepo() = default;

gittide::Expected<AsyncRepo> AsyncRepo::open(const std::filesystem::path& path)
{
    auto r = gittide::GitRepo::open(path);
    if (!r)
        return std::unexpected(r.error());
    return AsyncRepo(std::make_shared<Impl>(std::move(*r)));
}

QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> AsyncRepo::status()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.status();
        });
}

QCoro::Task<gittide::Expected<gittide::DiffResult>> AsyncRepo::diff(gittide::DiffTarget target, std::filesystem::path file)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, target, file = std::move(file)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.diff(target, file);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::stage(gittide::StageSelection sel)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, sel = std::move(sel)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stage(sel);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::unstage(gittide::StageSelection sel)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, sel = std::move(sel)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.unstage(sel);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::discard(gittide::StageSelection sel)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, sel = std::move(sel)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.discard(sel);
        });
}

QCoro::Task<gittide::Expected<std::string>> AsyncRepo::commit(gittide::CommitRequest req)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, req = std::move(req)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.commit(req);
        });
}

QCoro::Task<gittide::Expected<std::vector<gittide::CommitNode>>> AsyncRepo::log(unsigned limit)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, limit]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.log(limit);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::resetIndexToHead()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.resetIndexToHead();
        });
}

QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> AsyncRepo::commitFiles(QString oid)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oid.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.commitFiles(o);
        });
}

QCoro::Task<gittide::Expected<gittide::DiffResult>> AsyncRepo::commitDiff(QString oid, std::filesystem::path file)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oid.toStdString(), file = std::move(file)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.commitDiff(o, file);
        });
}

QCoro::Task<gittide::Expected<std::vector<gittide::BranchInfo>>> AsyncRepo::branches()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.branches();
        });
}

QCoro::Task<gittide::Expected<gittide::HeadState>> AsyncRepo::head()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.head();
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::createBranch(QString name, QString fromOid)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, n = name.toStdString(), oid = fromOid.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.createBranch(n, oid);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::checkoutBranch(QString name)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, n = name.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.checkoutBranch(n);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::checkoutRemoteBranch(QString remoteShorthand)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, n = remoteShorthand.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.checkoutRemoteBranch(n);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::checkoutCommit(QString oid)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oid.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.checkoutCommit(o);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::deleteBranch(QString name, bool force)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, n = name.toStdString(), force]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.deleteBranch(n, force);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::renameBranch(QString oldName, QString newName, bool force)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, old = oldName.toStdString(), nw = newName.toStdString(), force]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.renameBranch(old, nw, force);
        });
}

QCoro::Task<gittide::Expected<gittide::SyncStatus>> AsyncRepo::syncStatus()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.syncStatus();
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::fetch(QString remote, gittide::Credentials cred, gittide::ProgressCallback onProgress)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, remote = remote.toStdString(), cred = std::move(cred), onProgress = std::move(onProgress)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.fetch(remote, cred, onProgress);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::pull(gittide::Credentials cred, gittide::ProgressCallback onProgress)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, cred = std::move(cred), onProgress = std::move(onProgress)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.pull(cred, onProgress);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::push(QString remote, QString branch, bool setUpstream, gittide::Credentials cred, gittide::ProgressCallback onProgress)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, remote = remote.toStdString(), branch = branch.toStdString(), setUpstream, cred = std::move(cred), onProgress = std::move(onProgress)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.push(remote, branch, setUpstream, cred, onProgress);
        });
}

QCoro::Task<gittide::Expected<gittide::PullStrategy>> AsyncRepo::pullStrategy()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.pullStrategy();
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::setPullStrategy(gittide::PullStrategy strategy)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, strategy]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.setPullStrategy(strategy);
        });
}

QCoro::Task<gittide::Expected<gittide::MergeOutcome>> AsyncRepo::mergeBranch(QString name)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, n = name.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.mergeBranch(n);
        });
}

QCoro::Task<gittide::Expected<gittide::MergeState>> AsyncRepo::mergeState()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.mergeState();
        });
}

QCoro::Task<gittide::Expected<std::string>> AsyncRepo::commitMerge(gittide::CommitRequest req)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, req = std::move(req)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.commitMerge(req);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::abortMerge()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.abortMerge();
        });
}

QCoro::Task<gittide::Expected<bool>> AsyncRepo::stashSave(QString message)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, msg = message.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashSave(msg);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::stashPop()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashPop();
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::deinitSubmodule(std::filesystem::path path)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, path = std::move(path)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.deinitSubmodule(path);
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::reinitSubmodule(std::filesystem::path path)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, path = std::move(path)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.reinitSubmodule(path);
        });
}

} // namespace gittide::ui
