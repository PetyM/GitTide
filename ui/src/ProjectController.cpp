#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectListModel.hpp"
#include "gitgui/ui/RepoListModel.hpp"
#include "gitgui/GitRepo.hpp"
#include "gitgui/ProjectStore.hpp"

#include <filesystem>

namespace gitgui::ui {

ProjectController::ProjectController(gitgui::ProjectStore* store,
                                     std::filesystem::path storePath,
                                     QObject* parent)
    : QObject(parent),
      store_(store),
      storePath_(std::move(storePath)),
      projectModel_(new ProjectListModel(store, this)),
      repoModel_(new RepoListModel(this)) {}

const std::vector<gitgui::RepoRef>& ProjectController::activeRepos() const {
    static const std::vector<gitgui::RepoRef> kEmpty;
    for (const auto& p : store_->projects()) {
        if (QString::fromStdString(p.id) == activeId_) return p.repos;
    }
    return kEmpty;
}

void ProjectController::saveStore() const {
    if (!storePath_.empty()) store_->save(storePath_);
}

void ProjectController::refreshRepoModel() {
    for (const auto& p : store_->projects()) {
        if (QString::fromStdString(p.id) == activeId_) {
            repoModel_->setRepos(p.repos);
            return;
        }
    }
    repoModel_->setRepos({});
}

void ProjectController::activate(const QString& projectId) {
    const std::string id = projectId.toStdString();
    for (const auto& p : store_->projects()) {
        if (p.id == id) {
            store_->setActiveProject(id);
            repoModel_->setRepos(p.repos);
            activeId_ = projectId;
            emit projectActivated(projectId);
            return;
        }
    }
    // Unknown id: no-op.
}

void ProjectController::createProject(const QString& name) {
    if (name.trimmed().isEmpty()) return;
    auto& p = store_->createProject(name.toStdString());
    saveStore();
    const QString id = QString::fromStdString(p.id);
    projectModel_->refresh();
    activate(id);
    emit projectCreated(id);
}

void ProjectController::addExistingRepo(const QString& path) {
    if (activeId_.isEmpty()) {
        emit repoAddFailed(QStringLiteral("No active project"));
        return;
    }
    const std::filesystem::path p(path.toStdString());
    auto validation = gitgui::GitRepo::open(p);
    if (!validation) {
        emit repoAddFailed(QString::fromStdString(validation.error().message));
        return;
    }
    auto result = store_->addRepo(activeId_.toStdString(),
                                  gitgui::RepoRef{.path = path.toStdString()});
    if (!result) {
        emit repoAddFailed(QString::fromStdString(result.error().message));
        return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(path);
}

void ProjectController::initRepo(const QString& parentDir, const QString& name) {
    if (activeId_.isEmpty()) {
        emit repoAddFailed(QStringLiteral("No active project"));
        return;
    }
    const std::filesystem::path dest =
        std::filesystem::path(parentDir.toStdString()) / name.toStdString();
    auto repo = gitgui::GitRepo::init(dest);
    if (!repo) {
        emit repoAddFailed(QString::fromStdString(repo.error().message));
        return;
    }
    auto result = store_->addRepo(activeId_.toStdString(),
                                  gitgui::RepoRef{.path = dest.generic_string()});
    if (!result) {
        emit repoAddFailed(QString::fromStdString(result.error().message));
        return;
    }
    saveStore();
    refreshRepoModel();
    emit repoAdded(QString::fromStdString(dest.generic_string()));
}

}  // namespace gitgui::ui
