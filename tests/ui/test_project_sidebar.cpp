#include <QAbstractButton>
#include <QComboBox>
#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "gittide/projectstore.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/projectsidebar.hpp"
#include "gittide/ui/repolistmodel.hpp"

using gittide::Project;
using gittide::ProjectStore;
using gittide::RepoRef;
using gittide::ui::ProjectController;
using gittide::ui::ProjectSidebar;

class TestProjectSidebar : public QObject
{
    Q_OBJECT
private slots:
    void selecting_project_activates_it_and_fills_repos()
    {
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "id-a", .name = "Work", .repos = {RepoRef{.path = "/home/u/api", .alias = "api"}}});
        store.projects().push_back(Project{.id = "id-b", .name = "Home"});

        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3);

        combo->setCurrentIndex(0); // selects "Work"
        QCOMPARE(controller.activeProjectId(), QStringLiteral("id-a"));
        QCOMPARE(controller.repos()->rowCount(), 1);
    }

    void open_in_new_window_emits_current_project()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        combo->setCurrentIndex(0);

        QSignalSpy spy(&sidebar, &ProjectSidebar::openInNewWindowRequested);
        sidebar.requestOpenInNewWindow(); // what the context-menu action calls

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
    }

    void new_project_sentinel_is_last_combo_item()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        QVERIFY(combo != nullptr);
        // sentinel is last
        QCOMPARE(combo->itemText(combo->count() - 1), QStringLiteral("New project…"));
    }

    void selecting_sentinel_emits_createProjectRequested_not_activate()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        // First activate a real project
        combo->setCurrentIndex(0);
        const QString prevActive = controller.activeProjectId();

        QSignalSpy spyNew(&sidebar, &ProjectSidebar::createProjectRequested);
        QSignalSpy spyActivated(&controller, &ProjectController::projectActivated);
        // Select sentinel
        combo->setCurrentIndex(combo->count() - 1);

        QCOMPARE(spyNew.count(), 1);
        // Active project must not have changed
        QCOMPARE(controller.activeProjectId(), prevActive);
    }

    void toolbar_buttons_exist_with_correct_objectNames()
    {
        ProjectStore store;
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        QVERIFY(sidebar.findChild<QAbstractButton*>(QStringLiteral("addExistingButton")) != nullptr);
        QVERIFY(sidebar.findChild<QAbstractButton*>(QStringLiteral("initRepoButton")) != nullptr);
        QVERIFY(sidebar.findChild<QAbstractButton*>(QStringLiteral("cloneButton")) != nullptr);
    }

    void add_existing_button_emits_addExistingRequested()
    {
        ProjectStore store;
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        QSignalSpy spy(&sidebar, &ProjectSidebar::addExistingRequested);
        sidebar.findChild<QAbstractButton*>(QStringLiteral("addExistingButton"))->click();
        QCOMPARE(spy.count(), 1);
    }

    void open_in_new_window_button_exists_and_emits()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        ProjectSidebar sidebar(&controller);

        auto* combo = sidebar.findChild<QComboBox*>(QStringLiteral("projectSwitcher"));
        combo->setCurrentIndex(0); // activate "Work"

        auto* btn = sidebar.findChild<QAbstractButton*>(QStringLiteral("openInNewWindowButton"));
        QVERIFY(btn != nullptr);

        QSignalSpy spy(&sidebar, &ProjectSidebar::openInNewWindowRequested);
        btn->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
    }
};

#include "test_project_sidebar.moc"
