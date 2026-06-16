#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QComboBox>
#include <QListView>

#include "gitgui/ProjectStore.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/RepoListModel.hpp"

using gitgui::Project;
using gitgui::ProjectStore;
using gitgui::RepoRef;
using gitgui::ui::ProjectController;
using gitgui::ui::ProjectSidebar;

class TestProjectSidebar : public QObject {
    Q_OBJECT
private slots:
    void selecting_project_activates_it_and_fills_repos() {
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = "/home/u/api", .alias = "api"} }});
        store.projects().push_back(Project{.id = "id-b", .name = "Home"});

        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 2);

        combo->setCurrentIndex(0);  // selects "Work"
        QCOMPARE(controller.activeProjectId(), QStringLiteral("id-a"));
        QCOMPARE(controller.repos()->rowCount(), 1);
    }

    void open_in_new_window_emits_current_project() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        combo->setCurrentIndex(0);

        QSignalSpy spy(&sidebar, &ProjectSidebar::openInNewWindowRequested);
        sidebar.requestOpenInNewWindow();  // what the context-menu action calls

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
    }
};

#include "test_project_sidebar.moc"
