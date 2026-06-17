#pragma once
#include <QObject>
#include <QString>
#include <vector>

#include "gitgui/ProjectStore.hpp"

namespace gitgui::ui {

class ProjectListModel;
class RepoListModel;

// Per-window ViewModel. References the shared ProjectStore; owns the project
// and repo list models. "Active project" is per-window UI state — activating
// here also updates the store's last-focused hint.
class ProjectController : public QObject {
    Q_OBJECT
public:
    explicit ProjectController(gitgui::ProjectStore* store, QObject* parent = nullptr);

    ProjectListModel* projects() const { return projectModel_; }
    RepoListModel* repos() const { return repoModel_; }
    QString activeProjectId() const { return activeId_; }
    const std::vector<gitgui::RepoRef>& activeRepos() const;

public slots:
    // Activate the project with this id. Unknown id is a no-op (no signal).
    void activate(const QString& projectId);

signals:
    void projectActivated(const QString& projectId);

private:
    gitgui::ProjectStore* store_;
    ProjectListModel* projectModel_;
    RepoListModel* repoModel_;
    QString activeId_;
};

}  // namespace gitgui::ui
