#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectListModel.hpp"
#include "gitgui/ui/RepoListModel.hpp"
#include "gitgui/ProjectStore.hpp"

namespace gitgui::ui {

ProjectController::ProjectController(gitgui::ProjectStore* store, QObject* parent)
    : QObject(parent),
      store_(store),
      projectModel_(new ProjectListModel(store, this)),
      repoModel_(new RepoListModel(this)) {}

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

}  // namespace gitgui::ui
