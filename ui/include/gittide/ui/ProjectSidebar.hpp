#pragma once
#include <QWidget>
#include <QString>

class QComboBox;
class QTreeView;
class QToolButton;

namespace gittide::ui {

class ProjectController;

class ProjectSidebar : public QWidget {
    Q_OBJECT
public:
    explicit ProjectSidebar(ProjectController* controller, QWidget* parent = nullptr);

public slots:
    void requestOpenInNewWindow();

signals:
    void openInNewWindowRequested(const QString& projectId);
    void repoSelected(const QString& repoPath);
    void createProjectRequested();
    void addExistingRequested();
    void initRepoRequested();
    void cloneRepoRequested();

private slots:
    void syncCombo();

private:
    ProjectController* controller_;
    QComboBox* switcher_;
    QTreeView* repoList_;
    QToolButton* addExistingBtn_;
    QToolButton* initRepoBtn_;
    QToolButton* cloneBtn_;
};

}  // namespace gittide::ui
