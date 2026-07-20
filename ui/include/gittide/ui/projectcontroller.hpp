#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <filesystem>
#include <qcorotask.h>
#include <vector>

#include "gittide/projectstore.hpp"
#include "gittide/sync.hpp"

class QTimer;

namespace gittide::ui {

class ProjectListModel;
class RepoListModel;
class CredentialManager;

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
    /// True while a project-wide fetch is in flight; QML disables the action and
    /// shows a spinner on the project header.
    Q_PROPERTY(bool fetchingAll READ fetchingAll NOTIFY fetchingAllChanged)
    /// Human-readable result of the last fleet fetch, e.g. "12 fetched, 1 failed".
    Q_PROPERTY(QString fetchSummary READ fetchSummary NOTIFY fetchingAllChanged)
public:
    /// @param pollIntervalMs how often, while the window is active, to re-read each
    /// repo's local sync counts (D35). Injectable so tests can poll fast.
    explicit ProjectController(gittide::ProjectStore* store, std::filesystem::path storePath = {}, QObject* parent = nullptr,
                               int pollIntervalMs = 5000);

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

    bool fetchingAll() const
    {
        return m_fetchingAll;
    }
    QString fetchSummary() const
    {
        return m_fetchSummary;
    }

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
    // Fetch every non-missing repo in the active project in parallel. No-op when
    // there is no active project, no repos, or a fetch is already running.
    Q_INVOKABLE void fetchAll();
    // Start/stop the low-frequency poll that keeps non-active repos' sidebar sync
    // counts current (D35). Driven by the window's active state from QML.
    Q_INVOKABLE void setWindowActive(bool active);
    // Store the credentials and re-fetch any repos that failed on auth. No-op
    // when no auth failures are pending or a fetch is already running.
    Q_INVOKABLE void submitFleetCredentials(const QString& username, const QString& token);

    /// Wire the process-wide credential manager so clone and fleet-fetch resolve
    /// keychain-backed credentials for each remote.
    void setCredentialManager(CredentialManager* cm) { m_credentials = cm; }

    // Record the repo (or submodule) the user has open as the active project's
    // "last active" hint and persist it, so the next launch reopens it. No-op when
    // unchanged or with no active project. `path` empty clears the hint.
    Q_INVOKABLE void setActiveRepo(const QString& path);
    // The active project's last-active repo path, or empty when none is stored or
    // the stored path no longer exists on disk (stale → caller falls back).
    Q_INVOKABLE QString lastActiveRepo() const;

    // Fetch the submodule tree for repoPath off-thread and refresh the model row.
    Q_INVOKABLE QCoro::Task<void> refreshSubmodules(QString repoPath);
    // Re-initialise the submodule at the absolute submodulePath and refresh the tree.
    Q_INVOKABLE QCoro::Task<void> initSubmodule(QString repoPath, QString submodulePath);
    // Initialise/update every direct submodule for repoPath and refresh the tree.
    Q_INVOKABLE QCoro::Task<void> updateAllSubmodules(QString repoPath);
    // De-initialise the submodule at the absolute submodulePath and refresh the tree.
    Q_INVOKABLE QCoro::Task<void> deinitSubmodule(QString repoPath, QString submodulePath);

signals:
    void activeProjectChanged();
    void projectActivated(const QString& projectId);
    /// Emitted when a submodule op (init/update/deinit) fails. submodulePath is
    /// empty for repo-wide ops (updateAllSubmodules).
    void submoduleOpFailed(const QString& repoPath, const QString& submodulePath, const QString& message);
    void projectCreated(const QString& projectId);
    void repoAdded(const QString& path);
    void repoAddFailed(const QString& message);
    void cloneProgress(int received, int total);
    void repoRemoved(const QString& path);
    void projectRemoved(const QString& id);
    void fetchingAllChanged();
    void fleetFetchFinished(int ok, int failed);
    void authRequired();

private:
    gittide::ProjectStore* m_store;
    std::filesystem::path m_storePath;
    ProjectListModel* m_projectModel;
    RepoListModel* m_repoModel;
    QString m_activeId;
    std::atomic<bool> m_cloneCancel{false};

    bool                 m_fetchingAll = false;
    QString              m_fetchSummary;
    int                  m_fetchPending = 0;
    int                  m_fetchOk      = 0;
    int                  m_fetchFailed  = 0;
    std::vector<int>     m_authFailedRows;             // rows that failed on auth (retried in submitFleetCredentials)
    gittide::Credentials m_sessionCred;
    CredentialManager*   m_credentials = nullptr; // process-wide; not owned

    void saveStore() const;
    void refreshRepoModel();
    QCoro::Task<void> fetchOne(int row, gittide::RepoRef ref);
    void              finishOneFetch();                // counter bookkeeping + finalize

    // One poll pass: re-read each non-missing top-level repo's local sync counts
    // (HEAD vs its tracking ref — no network) and update the sidebar rows (D35).
    QCoro::Task<void> pollRepos();
    QTimer*           m_pollTimer = nullptr;

    // Shared body for the three mutating submodule ops. `op` performs the core call
    // on a transient AsyncRepo handle; the busy flag, path conversion, and
    // post-refresh are handled here. submodulePath may be empty for bulk ops.
    // Defined in the .cpp; only instantiated there (not a cross-TU template).
    template <class Op>
    QCoro::Task<void> runSubmoduleOp(QString repoPath, QString submodulePath, Op op);
};

} // namespace gittide::ui
