#pragma once
#include <QString>
#include <QWidget>

class QComboBox;
class QTreeView;
class QToolButton;

namespace gittide::ui {

class ProjectController;

class ProjectSidebar : public QWidget
{
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
    ProjectController* m_controller;
    QComboBox* m_switcher;
    QToolButton* m_removeProjectBtn;
    QTreeView* m_repoList;
    QToolButton* m_addExistingBtn;
    QToolButton* m_initRepoBtn;
    QToolButton* m_cloneBtn;
};

} // namespace gittide::ui
