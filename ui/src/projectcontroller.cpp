#include "gittide/ui/projectcontroller.hpp"

#include <QtConcurrent>
#include <core/qcorofuture.h>
#include <filesystem>

#include "gittide/gitrepo.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/ui/projectlistmodel.hpp"
#include "gittide/ui/repolistmodel.hpp"

namespace gittide::ui {

ProjectController::ProjectController(gittide::ProjectStore* store, std::filesystem::path storePath, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_storePath(std::move(storePath))
    , m_projectModel(new ProjectListModel(store, this))
    , m_repoModel(new RepoListModel(this))
{
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
    const std::string id = projectId.toStdString();
    for (const auto& p : m_store->projects())
    {
        if (p.id == id)
        {
            m_store->setActiveProject(id);
            m_repoModel->setRepos(p.repos);
            m_activeId = projectId;
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

    auto result = co_await QtConcurrent::run(
        [urlStr, destPath, cb = std::move(cb)]() mutable -> gittide::Expected<void>
        {
            auto r = gittide::GitRepo::clone(urlStr, destPath, std::move(cb));
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
    if (m_activeId.isEmpty())
        return;
    auto result = m_store->removeRepo(m_activeId.toStdString(), path.toStdString());
    if (!result)
        return; // silently ignore (shouldn't happen via UI)
    saveStore();
    refreshRepoModel();
    emit repoRemoved(path);
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

} // namespace gittide::ui
