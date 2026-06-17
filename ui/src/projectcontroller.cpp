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
    , store_(store)
    , storePath_(std::move(storePath))
    , projectModel_(new ProjectListModel(store, this))
    , repoModel_(new RepoListModel(this))
{
}

const std::vector<gittide::RepoRef>& ProjectController::activeRepos() const
{
    static const std::vector<gittide::RepoRef> kEmpty;
    for (const auto& p : store_->projects())
    {
        if (QString::fromStdString(p.id) == activeId_)
            return p.repos;
    }
    return kEmpty;
}

void ProjectController::saveStore() const
{
    if (!storePath_.empty())
        store_->save(storePath_);
}

void ProjectController::refreshRepoModel()
{
    for (const auto& p : store_->projects())
    {
        if (QString::fromStdString(p.id) == activeId_)
        {
            repoModel_->setRepos(p.repos);
            return;
        }
    }
    repoModel_->setRepos({});
}

void ProjectController::activate(const QString& projectId)
{
    const std::string id = projectId.toStdString();
    for (const auto& p : store_->projects())
    {
        if (p.id == id)
        {
            store_->setActiveProject(id);
            repoModel_->setRepos(p.repos);
            activeId_ = projectId;
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
    auto& p = store_->createProject(name.toStdString());
    saveStore();
    const QString id = QString::fromStdString(p.id);
    projectModel_->refresh();
    activate(id);
    emit projectCreated(id);
}

void ProjectController::addExistingRepo(const QString& path)
{
    if (activeId_.isEmpty())
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
    auto result = store_->addRepo(activeId_.toStdString(), gittide::RepoRef{.path = path.toStdString()});
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
    if (activeId_.isEmpty())
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
    auto result = store_->addRepo(activeId_.toStdString(), gittide::RepoRef{.path = dest.generic_string()});
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
    cloneCancel_.store(true);
}

QCoro::Task<void> ProjectController::cloneRepo(QString url, QString dest)
{
    cloneCancel_.store(false);

    gittide::ProgressCallback cb = [this](unsigned r, unsigned t) -> bool
    {
        if (cloneCancel_.load())
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
        if (!cloneCancel_.load())
        {
            emit repoAddFailed(QString::fromStdString(result.error().message));
        }
        co_return;
    }

    auto addResult = store_->addRepo(activeId_.toStdString(), gittide::RepoRef{.path = dest.toStdString()});
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
    if (activeId_.isEmpty())
        return;
    auto result = store_->removeRepo(activeId_.toStdString(), path.toStdString());
    if (!result)
        return; // silently ignore (shouldn't happen via UI)
    saveStore();
    refreshRepoModel();
    emit repoRemoved(path);
}

void ProjectController::removeProject()
{
    if (activeId_.isEmpty())
        return;
    const QString removedId = activeId_;
    store_->removeProject(activeId_.toStdString());
    activeId_.clear();
    projectModel_->refresh();
    // Activate another project if one exists
    if (!store_->projects().empty())
    {
        const QString nextId = QString::fromStdString(store_->projects().front().id);
        activate(nextId);
    }
    else
    {
        repoModel_->setRepos({});
    }
    saveStore();
    emit projectRemoved(removedId);
}

} // namespace gittide::ui
