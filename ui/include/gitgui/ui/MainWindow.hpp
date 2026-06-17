#pragma once
#include <QMainWindow>
#include <QString>
#include <filesystem>

namespace gitgui { class ProjectStore; }

class QStackedWidget;

namespace gitgui::ui {

class ProjectController;
class ProjectSidebar;
class RepoController;
class ChangesView;
class DashboardModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(gitgui::ProjectStore* store,
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
    gitgui::ProjectStore* store_;
    ProjectController* controller_;
    ProjectSidebar* sidebar_;
    RepoController* repoController_;
    ChangesView* changesView_;
    DashboardModel* dashboardModel_;
    QStackedWidget* centralStack_;
};

}  // namespace gitgui::ui
