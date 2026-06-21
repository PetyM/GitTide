#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <filesystem>
#include <qcorotask.h>
#include <vector>

#include "gittide/projectstore.hpp"

namespace gittide::ui {

class ProjectListModel;
class RepoListModel;

// Per-window ViewModel. References the shared ProjectStore; owns the project
// and repo list models. "Active project" is per-window UI state — activating
// here also updates the store's last-focused hint.
class ProjectController : public QObject
{
    Q_OBJECT
    /// Id of the active project, or empty when none is active. QML gates the
    /// branded empty state on `activeProjectId.length` to choose between the
    /// "Create project" call-to-action and the add-repo actions.
    Q_PROPERTY(QString activeProjectId READ activeProjectId NOTIFY activeProjectChanged)
    /// Display name of the active project, or empty when none is active. The
    /// sidebar switcher shows this on its face.
    Q_PROPERTY(QString activeProjectName READ activeProjectName NOTIFY activeProjectChanged)
public:
    explicit ProjectController(gittide::ProjectStore* store, std::filesystem::path storePath = {}, QObject* parent = nullptr);

    ProjectListModel* projects() const
    {
        return m_projectModel;
    }
    RepoListModel* repos() const
    {
        return m_repoModel;
    }
    QString activeProjectId() const
    {
        return m_activeId;
    }
    QString activeProjectName() const;
    const std::vector<gittide::RepoRef>& activeRepos() const;

public slots:
    // Activate the project with this id. Unknown id is a no-op (no signal).
    void activate(const QString& projectId);
    void createProject(const QString& name);
    void addExistingRepo(const QString& path);
    void initRepo(const QString& parentDir, const QString& name);
    QCoro::Task<void> cloneRepo(QString url, QString dest);
    // QML-facing fire-and-forget wrapper: kicks cloneRepo() so QML (which cannot
    // await a QCoro::Task) can start a clone; results arrive via the usual
    // repoAdded / repoAddFailed / cloneProgress signals.
    Q_INVOKABLE void startClone(const QString& url, const QString& dest);
    void cancelClone();
    void removeRepo(const QString& path);
    void removeProject();

signals:
    void activeProjectChanged();
    void projectActivated(const QString& projectId);
    void projectCreated(const QString& projectId);
    void repoAdded(const QString& path);
    void repoAddFailed(const QString& message);
    void cloneProgress(int received, int total);
    void repoRemoved(const QString& path);
    void projectRemoved(const QString& id);

private:
    gittide::ProjectStore* m_store;
    std::filesystem::path m_storePath;
    ProjectListModel* m_projectModel;
    RepoListModel* m_repoModel;
    QString m_activeId;
    std::atomic<bool> m_cloneCancel{false};

    void saveStore() const;
    void refreshRepoModel();
};

} // namespace gittide::ui
