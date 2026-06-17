#pragma once
#include <QObject>
#include <QString>
#include <filesystem>
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
    explicit ProjectController(gitgui::ProjectStore* store,
                               std::filesystem::path storePath = {},
                               QObject* parent = nullptr);

    ProjectListModel* projects() const { return projectModel_; }
    RepoListModel* repos() const { return repoModel_; }
    QString activeProjectId() const { return activeId_; }
    const std::vector<gitgui::RepoRef>& activeRepos() const;

public slots:
    // Activate the project with this id. Unknown id is a no-op (no signal).
    void activate(const QString& projectId);
    void createProject(const QString& name);
    void addExistingRepo(const QString& path);
    void initRepo(const QString& parentDir, const QString& name);

signals:
    void projectActivated(const QString& projectId);
    void projectCreated(const QString& projectId);
    void repoAdded(const QString& path);
    void repoAddFailed(const QString& message);

private:
    gitgui::ProjectStore* store_;
    std::filesystem::path storePath_;
    ProjectListModel* projectModel_;
    RepoListModel* repoModel_;
    QString activeId_;

    void saveStore() const;
    void refreshRepoModel();
};

}  // namespace gitgui::ui
