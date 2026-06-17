#pragma once
#include <QWidget>
#include <QString>

class QComboBox;
class QListView;

namespace gitgui::ui {

class ProjectController;

// Layout-A left pane: project switcher + repo list of the active project.
class ProjectSidebar : public QWidget {
    Q_OBJECT
public:
    explicit ProjectSidebar(ProjectController* controller, QWidget* parent = nullptr);

public slots:
    // Emit openInNewWindowRequested for the currently active project.
    void requestOpenInNewWindow();

signals:
    void openInNewWindowRequested(const QString& projectId);

private:
    ProjectController* controller_;
    QComboBox* switcher_;
    QListView* repoList_;
};

}  // namespace gitgui::ui
