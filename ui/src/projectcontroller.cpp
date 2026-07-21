#include "gittide/ui/projectcontroller.hpp"

#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrent>
#include <algorithm>
#include <core/qcorofuture.h>
#include <filesystem>

#include "gittide/gitrepo.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/ui/asyncrepo.hpp"
#include "gittide/ui/autherror.hpp"
#include "gittide/ui/credentialmanager.hpp"
#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/projectlistmodel.hpp"
#include "gittide/ui/repolistmodel.hpp"

namespace gittide::ui {

ProjectController::ProjectController(gittide::ProjectStore* store, std::filesystem::path storePath, QObject* parent,
                                    int pollIntervalMs)
    : QObject(parent)
    , m_store(store)
    , m_storePath(std::move(storePath))
    , m_projectModel(new ProjectListModel(store, this))
    , m_repoModel(new RepoListModel(this))
    , m_pollTimer(new QTimer(this))
{
    // Live refresh of non-active repos (D35): a low-frequency poll, gated on the
    // window being active (started/stopped via setWindowActive).
    m_pollTimer->setInterval(pollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() { QCoro::connect(pollRepos(), this, []() {}); });
}

void ProjectController::setWindowActive(bool active)
{
    if (active)
        m_pollTimer->start();
    else
        m_pollTimer->stop();
}

QCoro::Task<void> ProjectController::pollRepos()
{
    if (m_activeId.isEmpty())
        co_return;

    QPointer<ProjectController> self = this;
    // Copy the refs: activeRepos() returns a reference into the store, which may
    // change across a co_await below.
    const std::vector<gittide::RepoRef> repos = activeRepos();
    for (int row = 0; row < static_cast<int>(repos.size()); ++row)
    {
        const std::filesystem::path p(repos[row].path);
        std::error_code             ec;
        if (!std::filesystem::exists(p, ec) || ec)
            continue; // missing repo: leave its row as-is

        // Each repo gets its OWN handle (one-owner invariant); syncStatus is a
        // read-only, local comparison (HEAD vs tracking ref) — no network.
        auto opened = AsyncRepo::open(p);
        if (!opened)
            continue;
        AsyncRepo repo = std::move(*opened);
        auto      st   = co_await repo.syncStatus();
        if (!self)
            co_return;
        if (st)
            m_repoModel->setSyncCounts(row, st->ahead, st->behind, st->hasUpstream);

        QString branch, shortOid;
        bool    detached = false;
        int     dirty    = 0;
        bool    haveHead = false, haveStatus = false;
        if (auto hs = co_await repo.head(); hs)
        {
            branch   = QString::fromStdString(hs->branch);
            detached = hs->detached;
            shortOid = QString::fromStdString(
                hs->oid.substr(0, std::min<std::size_t>(7, hs->oid.size())));
            haveHead = true;
        }
        if (!self)
            co_return;
        if (auto ds = co_await repo.status(); ds)
        {
            dirty      = static_cast<int>(ds->size());
            haveStatus = true;
        }
        if (!self)
            co_return;
        if (haveHead && haveStatus)
            m_repoModel->setRepoHead(row, branch, detached, shortOid, dirty);

        auto tree = co_await repo.submoduleTree();
        if (!self)
            co_return;
        if (tree)
            m_repoModel->applySubmodules(QString::fromStdString(repos[row].path), *tree);
    }
}

QString ProjectController::activeProjectName() const
{
    const std::string id = m_activeId.toStdString();
    for (const auto& p : m_store->projects())
    {
        if (p.id == id)
            return QString::fromStdString(p.name);
    }
    return {};
}

const std::vector<gittide::RepoRef>& ProjectController::activeRepos() const
{
    static const std::vector<gittide::RepoRef> kEmpty;
    for (const auto& p : m_store->projects())
    {
        if (QString::fromStdString(p.id) == m_activeId)
            return p.repos;
    }
    return kEmpty;
}

void ProjectController::saveStore() const
{
    if (!m_storePath.empty())
        m_store->save(m_storePath);
}

void ProjectController::refreshRepoModel()
{
    for (const auto& p : m_store->projects())
    {
        if (QString::fromStdString(p.id) == m_activeId)
        {
            m_repoModel->setRepos(p.repos);
            return;
        }
    }
    m_repoModel->setRepos({});
}

void ProjectController::activate(const QString& projectId)
{
    if (m_fetchingAll)
        return;
    const std::string id = projectId.toStdString();
    for (const auto& p : m_store->projects())
    {
        if (p.id == id)
        {
            m_store->setActiveProject(id);
            m_repoModel->setRepos(p.repos);
            m_activeId = projectId;
            // Persist the active-project hint so the next launch reopens it.
            saveStore();
            emit activeProjectChanged();
            emit projectActivated(projectId);
            return;
        }
    }
    // Unknown id: no-op.
}

void ProjectController::createProject(const QString& name)
{
    if (name.trimmed().isEmpty())
        return;
    auto& p = m_store->createProject(name.toStdString());
    saveStore();
    const QString id = QString::fromStdString(p.id);
    m_projectModel->refresh();
    activate(id);
    emit projectCreated(id);
}

void ProjectController::addExistingRepo(const QString& path)
{
    if (m_activeId.isEmpty())
    {
        emit repoAddFailed(QStringLiteral("No active project"));
        return;
    }
    const std::filesystem::path p(path.toStdString());
    auto validation = gittide::GitRepo::open(p);
    if (!validation)
    {
        emit repoAddFailed(QString::fromStdString(validation.error().message));
        return;
    }
    auto result = m_store->addRepo(m_activeId.toStdString(), gittide::RepoRef{.path = path.toStdString()});
    if (!result)
    {
        emit repoAddFailed(QString::fromStdString(result.error().message));
        return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(path);
}

void ProjectController::initRepo(const QString& parentDir, const QString& name)
{
    if (m_activeId.isEmpty())
    {
        emit repoAddFailed(QStringLiteral("No active project"));
        return;
    }
    const std::filesystem::path dest = std::filesystem::path(parentDir.toStdString()) / name.toStdString();
    auto repo                        = gittide::GitRepo::init(dest);
    if (!repo)
    {
        emit repoAddFailed(QString::fromStdString(repo.error().message));
        return;
    }
    auto result = m_store->addRepo(m_activeId.toStdString(), gittide::RepoRef{.path = dest.generic_string()});
    if (!result)
    {
        emit repoAddFailed(QString::fromStdString(result.error().message));
        return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(QString::fromStdString(dest.generic_string()));
}

void ProjectController::cancelClone()
{
    m_cloneCancel.store(true);
}

void ProjectController::startClone(const QString& url, const QString& dest)
{
    QCoro::connect(cloneRepo(url, dest), this, [] {});
}

QCoro::Task<void> ProjectController::cloneRepo(QString url, QString dest)
{
    m_cloneCancel.store(false);

    gittide::ProgressCallback cb = [this](unsigned r, unsigned t) -> bool
    {
        if (m_cloneCancel.load())
            return false;
        QMetaObject::invokeMethod(
            this,
            [this, r, t]
            {
                emit cloneProgress(static_cast<int>(r), static_cast<int>(t));
            },
            Qt::QueuedConnection);
        return true;
    };

    const std::string urlStr = url.toStdString();
    const std::filesystem::path destPath(dest.toStdString());

    // Resolve keychain-backed credentials for the clone URL (host token for private
    // https, keyfile/agent for ssh); falls back to defaults with no manager wired.
    gittide::Credentials cred =
        m_credentials ? co_await m_credentials->credentialsForRemote(url) : gittide::Credentials{};
    auto result = co_await QtConcurrent::run(
        [urlStr, destPath, cred, cb = std::move(cb)]() mutable -> gittide::Expected<void>
        {
            auto r = gittide::GitRepo::clone(urlStr, destPath, std::move(cred), std::move(cb));
            if (!r)
                return std::unexpected(r.error());
            return {};
        });

    if (!result)
    {
        if (!m_cloneCancel.load())
        {
            emit repoAddFailed(QString::fromStdString(result.error().message));
        }
        co_return;
    }

    auto addResult = m_store->addRepo(m_activeId.toStdString(), gittide::RepoRef{.path = dest.toStdString()});
    if (!addResult)
    {
        emit repoAddFailed(QString::fromStdString(addResult.error().message));
        co_return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(dest);
}

void ProjectController::removeRepo(const QString& path)
{
    if (m_fetchingAll || m_activeId.isEmpty())
        return;
    auto result = m_store->removeRepo(m_activeId.toStdString(), path.toStdString());
    if (!result)
        return; // silently ignore (shouldn't happen via UI)
    // Drop the last-active hint if it pointed at the repo just removed, so the
    // next launch doesn't try to reopen a repo no longer in the project.
    for (auto& p : m_store->projects())
    {
        if (QString::fromStdString(p.id) == m_activeId && p.lastActiveRepo == path.toStdString())
        {
            p.lastActiveRepo.clear();
            break;
        }
    }
    saveStore();
    refreshRepoModel();
    emit repoRemoved(path);
}

void ProjectController::setActiveRepo(const QString& path)
{
    if (m_activeId.isEmpty())
        return;
    const std::string id = m_activeId.toStdString();
    const std::string s  = path.toStdString();
    for (auto& p : m_store->projects())
    {
        if (p.id == id)
        {
            if (p.lastActiveRepo == s)
                return; // unchanged — avoid a redundant disk write on every open
            p.lastActiveRepo = s;
            saveStore();
            return;
        }
    }
}

QString ProjectController::lastActiveRepo() const
{
    const std::string id = m_activeId.toStdString();
    for (const auto& p : m_store->projects())
    {
        if (p.id == id)
        {
            if (p.lastActiveRepo.empty())
                return {};
            std::error_code ec;
            if (!std::filesystem::exists(std::filesystem::path(p.lastActiveRepo), ec) || ec)
                return {}; // stale (folder gone) — caller falls back to first repo
            return QString::fromStdString(p.lastActiveRepo);
        }
    }
    return {};
}

QVariantList ProjectController::activeProjectRepos() const
{
    QVariantList out;
    for (const auto& r : activeRepos())
    {
        QVariantMap m;
        m.insert(QStringLiteral("path"), QString::fromStdString(r.path));
        QString name = QString::fromStdString(r.alias);
        if (name.isEmpty())
            name = pathToQString(std::filesystem::path(r.path).filename());
        m.insert(QStringLiteral("name"), name);
        out.append(m);
    }
    return out;
}

void ProjectController::removeProject()
{
    if (m_activeId.isEmpty())
        return;
    const QString removedId = m_activeId;
    m_store->removeProject(m_activeId.toStdString());
    m_activeId.clear();
    m_projectModel->refresh();
    // Activate another project if one exists
    if (!m_store->projects().empty())
    {
        const QString nextId = QString::fromStdString(m_store->projects().front().id);
        activate(nextId);
    }
    else
    {
        m_repoModel->setRepos({});
        emit activeProjectChanged();
    }
    saveStore();
    emit projectRemoved(removedId);
}

void ProjectController::fetchAll()
{
    if (m_activeId.isEmpty() || m_fetchingAll)
        return;

    const auto& repos = activeRepos();
    if (repos.empty())
        return;

    m_repoModel->resetFetchStates();
    m_fetchOk     = 0;
    m_fetchFailed = 0;
    m_authFailedRows.clear();
    m_fetchErrors.clear();

    // Only fetch non-missing top-level repos. A missing repo is SKIPPED — left in
    // its existing (missing) rendering, not counted as a failure (per spec).
    std::vector<int> rows;
    for (int row = 0; row < static_cast<int>(repos.size()); ++row)
    {
        const std::filesystem::path p(repos[row].path);
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !ec)
            rows.push_back(row);
    }

    if (rows.empty())
        return; // nothing fetchable; leave fetchingAll false, no summary change

    m_fetchPending = static_cast<int>(rows.size());
    m_fetchTotal   = m_fetchPending;
    m_fetchingAll  = true;
    emit fetchingAllChanged();
    emit fetchProgressChanged();

    for (int row : rows)
        QCoro::connect(fetchOne(row, repos[row]), this, [] {});
}

QCoro::Task<void> ProjectController::fetchOne(int row, gittide::RepoRef ref)
{
    m_repoModel->setFetchState(row, RepoListModel::FetchState::Running);

    // Alias-aware display name for any failure line (matches the tree row).
    const QString name = m_repoModel->data(m_repoModel->index(row, 0), Qt::DisplayRole).toString();

    // Each repo gets its OWN handle — the one-owner invariant holds; we never
    // touch the active RepoController's repo. The AsyncRepo lives in this
    // coroutine frame, so it stays valid across every co_await below.
    auto opened = AsyncRepo::open(std::filesystem::path(ref.path));
    if (!opened)
    {
        m_repoModel->setFetchState(row, RepoListModel::FetchState::Failed,
                                   QString::fromStdString(opened.error().message));
        m_fetchFailed++;
        m_fetchErrors << (name + QStringLiteral(": ") + QString::fromStdString(opened.error().message));
        finishOneFetch();
        co_return;
    }
    AsyncRepo repo = std::move(*opened);

    // Resolve credentials: a dialog-entered token (from an earlier auth failure in
    // this run) overrides; otherwise the keychain-backed store for this repo's URL.
    gittide::Credentials cred = m_sessionCred;
    if (cred.password.empty() && m_credentials)
    {
        auto u = co_await repo.remoteUrl(QStringLiteral("origin"));
        if (u && !u->empty())
            cred = co_await m_credentials->credentialsForRemote(QString::fromStdString(*u));
    }

    // Fleet fetch surfaces per-row state in the tree, not byte-level transfer
    // progress, so a no-op progress callback is fine here.
    auto fr = co_await repo.fetch(QStringLiteral("origin"), cred, [](unsigned, unsigned) { return true; });
    if (!fr)
    {
        if (gittide::ui::isAuthError(fr.error()))
            m_authFailedRows.push_back(row);   // retried after credentials — not a hard failure yet
        else
            m_fetchErrors << (name + QStringLiteral(": ") + QString::fromStdString(fr.error().message));
        m_repoModel->setFetchState(row, RepoListModel::FetchState::Failed,
                                   QString::fromStdString(fr.error().message));
        m_fetchFailed++;
        finishOneFetch();
        co_return;
    }

    // Refresh ahead/behind so the sidebar shows incoming commits. behind > 0 is
    // our first-cut heuristic for "Updated" vs "UpToDate" (fetch itself does not
    // report whether refs moved).
    int behind = 0, ahead = 0;
    if (auto st = co_await repo.syncStatus(); st)
    {
        ahead  = st->ahead;
        behind = st->behind;
        m_repoModel->setSyncCounts(row, ahead, behind, st->hasUpstream);
    }
    m_repoModel->setFetchState(row, behind > 0 ? RepoListModel::FetchState::Updated
                                               : RepoListModel::FetchState::UpToDate);
    m_fetchOk++;
    finishOneFetch();
}

void ProjectController::submitFleetCredentials(const QString& username, const QString& token)
{
    if (m_authFailedRows.empty() || m_fetchingAll)
        return;

    m_sessionCred.username    = username.toStdString();
    m_sessionCred.password    = token.toStdString();
    m_sessionCred.sshUseAgent = true;

    const std::vector<int> retry = std::move(m_authFailedRows);
    m_authFailedRows.clear();

    // These rows were counted as failures in the initial run; we are re-attempting
    // them, so back them out — fetchOne re-counts each into ok or failed.
    m_fetchFailed -= static_cast<int>(retry.size());

    const auto& repos = activeRepos();
    m_fetchPending    = static_cast<int>(retry.size());
    m_fetchTotal      = m_fetchPending;
    m_fetchingAll     = true;
    emit fetchingAllChanged();
    emit fetchProgressChanged();

    for (int row : retry)
    {
        if (row >= 0 && row < static_cast<int>(repos.size()))
            QCoro::connect(fetchOne(row, repos[row]), this, [] {});
    }
}

void ProjectController::finishOneFetch()
{
    // fetchOne resumes on the UI thread after each co_await, so this runs
    // single-threaded — plain counters are safe.
    --m_fetchPending;
    emit fetchProgressChanged();   // advance the bar as each repo settles
    if (m_fetchPending > 0)
        return;

    m_fetchingAll  = false;
    m_fetchSummary = QStringLiteral("%1 fetched, %2 failed").arg(m_fetchOk).arg(m_fetchFailed);
    emit fetchingAllChanged();
    emit fleetFetchFinished(m_fetchOk, m_fetchFailed);
    if (!m_authFailedRows.empty())
    {
        // Auth failures aren't hard failures yet — prompt for credentials and
        // retry. The error dialog waits until that second pass settles.
        emit authRequired();
        return;
    }
    if (!m_fetchErrors.isEmpty())
        emit fleetFetchFailed(m_fetchErrors);
}

QCoro::Task<void> ProjectController::refreshSubmodules(QString repoPath)
{
    QPointer<ProjectController> self = this;
    const std::filesystem::path p(repoPath.toStdString());
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec)
        co_return;

    auto opened = AsyncRepo::open(p);
    if (!opened)
        co_return;
    AsyncRepo repo = std::move(*opened);
    auto      tree = co_await repo.submoduleTree();
    if (!self || !tree)
        co_return;
    m_repoModel->applySubmodules(repoPath, *tree);
}

template <class Op>
QCoro::Task<void> ProjectController::runSubmoduleOp(QString repoPath, QString submodulePath, Op op)
{
    QPointer<ProjectController> self = this;
    const std::filesystem::path root(repoPath.toStdString());
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec)
        co_return;

    std::filesystem::path rel;
    if (!submodulePath.isEmpty())
    {
        rel = std::filesystem::relative(std::filesystem::path(submodulePath.toStdString()), root, ec);
        if (ec)
        {
            m_repoModel->setSubmoduleBusy(submodulePath, false);
            emit submoduleOpFailed(repoPath, submodulePath,
                QStringLiteral("could not compute relative submodule path"));
            co_return;
        }
        m_repoModel->setSubmoduleBusy(submodulePath, true);
    }

    auto opened = AsyncRepo::open(root);
    if (!opened)
    {
        if (self && !submodulePath.isEmpty())
            m_repoModel->setSubmoduleBusy(submodulePath, false);
        if (self)
            emit submoduleOpFailed(repoPath, submodulePath, QStringLiteral("could not open repository"));
        co_return;
    }
    AsyncRepo handle = std::move(*opened);
    auto      result = co_await op(handle, rel);
    if (!self)
        co_return;
    if (!submodulePath.isEmpty())
        m_repoModel->setSubmoduleBusy(submodulePath, false);

    if (!result)
    {
        emit submoduleOpFailed(repoPath, submodulePath, QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshSubmodules(repoPath);
}

QCoro::Task<void> ProjectController::initSubmodule(QString repoPath, QString submodulePath)
{
    co_await runSubmoduleOp(repoPath, submodulePath,
        [](AsyncRepo& r, std::filesystem::path rel) { return r.reinitSubmodule(std::move(rel)); });
}

QCoro::Task<void> ProjectController::deinitSubmodule(QString repoPath, QString submodulePath)
{
    co_await runSubmoduleOp(repoPath, submodulePath,
        [](AsyncRepo& r, std::filesystem::path rel) { return r.deinitSubmodule(std::move(rel)); });
}

QCoro::Task<void> ProjectController::updateAllSubmodules(QString repoPath)
{
    co_await runSubmoduleOp(repoPath, /*submodulePath=*/QString{},
        [](AsyncRepo& r, std::filesystem::path) { return r.updateSubmodules(); });
}

} // namespace gittide::ui
