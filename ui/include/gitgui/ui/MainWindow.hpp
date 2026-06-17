#pragma once
#include <QMainWindow>
#include <QString>

namespace gitgui { class ProjectStore; }

namespace gitgui::ui {

class ProjectController;
class ProjectSidebar;

// One window of the hybrid multi-window model. References the shared
// ProjectStore; owns a per-window ProjectController (active project is
// per-window state).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(gitgui::ProjectStore* store, QWidget* parent = nullptr);

    ProjectController* controller() const { return controller_; }
    QString currentProjectId() const;

    void showProject(const QString& projectId);

signals:
    void openInNewWindowRequested(const QString& projectId);

private:
    ProjectController* controller_;
    ProjectSidebar* sidebar_;
};

}  // namespace gitgui::ui
