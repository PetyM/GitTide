#pragma once
#include <QMainWindow>
#include <QString>
#include <filesystem>

namespace gittide {
class ProjectStore;
}

class QStackedWidget;
class QDockWidget;
class QTabWidget;

namespace gittide::ui {

class BranchBar;
class ProjectController;
class ProjectSidebar;
class RepoController;
class ChangesView;
class ChangedFilesList;
class DiffView;
class HistoryView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(gittide::ProjectStore* store, std::filesystem::path storePath = {}, QWidget* parent = nullptr);

    ProjectController* controller() const
    {
        return m_controller;
    }
    QString currentProjectId() const;
    void showProject(const QString& projectId);

signals:
    void openInNewWindowRequested(const QString& projectId);
    void repoOpened(const QString& path);

private slots:
    void updateCentralPage();
    void onCreateProjectRequested();
    void onAddExistingRequested();
    void onInitRepoRequested();
    void onCloneRepoRequested(); // stub in Task 4; implemented in Task 5

private:
    gittide::ProjectStore* m_store;
    ProjectController* m_controller;
    ProjectSidebar* m_sidebar;
    QDockWidget* m_projectsDock;
    RepoController* m_repoController;
    BranchBar* m_branchBar;
    ChangesView* m_changesView;
    HistoryView* m_historyView;
    ChangedFilesList* m_commitFiles; // read-only file list for the selected commit
    DiffView* m_diff;                // shared diff panel (right pane)
    QTabWidget* m_mainTabs;
    QStackedWidget* m_centralStack;
    QString m_currentBranch; // tracks latest headChanged branch name
    QString m_currentOid;    // tracks the currently selected history commit OID
};

} // namespace gittide::ui
