#pragma once
#include <QMainWindow>
#include <QString>
#include <filesystem>

namespace gittide { class ProjectStore; }

class QStackedWidget;

namespace gittide::ui {

class ProjectController;
class ProjectSidebar;
class RepoController;
class ChangesView;
class HistoryView;
class DashboardModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(gittide::ProjectStore* store,
                        std::filesystem::path storePath = {},
                        QWidget* parent = nullptr);

    ProjectController* controller() const { return controller_; }
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
    void onCloneRepoRequested();  // stub in Task 4; implemented in Task 5

private:
    gittide::ProjectStore* store_;
    ProjectController* controller_;
    ProjectSidebar* sidebar_;
    RepoController* repoController_;
    ChangesView* changesView_;
    HistoryView* historyView_;
    DashboardModel* dashboardModel_;
    QStackedWidget* centralStack_;
};

}  // namespace gittide::ui
