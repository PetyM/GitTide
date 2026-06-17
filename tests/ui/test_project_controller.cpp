#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "gitgui/ProjectStore.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/RepoListModel.hpp"

using gitgui::Project;
using gitgui::ProjectStore;
using gitgui::RepoRef;
using gitgui::ui::ProjectController;

class TestProjectController : public QObject {
    Q_OBJECT
private slots:
    void activate_loads_repos_and_emits() {
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = "/home/u/api", .alias = "api"},
                       RepoRef{.path = "/home/u/web", .alias = "web"} }});

        ProjectController controller(&store);
        QSignalSpy spy(&controller, &ProjectController::projectActivated);

        controller.activate(QStringLiteral("id-a"));

        QCOMPARE(controller.activeProjectId(), QStringLiteral("id-a"));
        QCOMPARE(controller.repos()->rowCount(), 2);
        QCOMPARE(QString::fromStdString(store.activeProject()), QStringLiteral("id-a"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
    }

    void activate_unknown_id_is_ignored() {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy spy(&controller, &ProjectController::projectActivated);

        controller.activate(QStringLiteral("nope"));

        QCOMPARE(controller.activeProjectId(), QString());
        QCOMPARE(spy.count(), 0);
    }
};

#include "test_project_controller.moc"
