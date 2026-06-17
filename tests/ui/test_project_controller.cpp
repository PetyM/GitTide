#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QUuid>

#include <filesystem>
#include <git2.h>

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
    void initTestCase()    { git_libgit2_init(); }
    void cleanupTestCase() { git_libgit2_shutdown(); }

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

    void createProject_appends_project_and_emits() {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy spyCreated(&controller, &ProjectController::projectCreated);
        QSignalSpy spyActivated(&controller, &ProjectController::projectActivated);

        controller.createProject(QStringLiteral("Sandbox"));

        QCOMPARE(store.projects().size(), std::size_t(1));
        QCOMPARE(QString::fromStdString(store.projects()[0].name), QStringLiteral("Sandbox"));
        QCOMPARE(spyCreated.count(), 1);
        QCOMPARE(spyActivated.count(), 1);
        QCOMPARE(controller.activeProjectId(), spyCreated.at(0).at(0).toString());
    }

    void createProject_empty_name_is_ignored() {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy spy(&controller, &ProjectController::projectCreated);

        controller.createProject(QStringLiteral("   "));

        QCOMPARE(store.projects().size(), std::size_t(0));
        QCOMPARE(spy.count(), 0);
    }

    void addExistingRepo_valid_repo_emits_repoAdded() {
        auto dir = std::filesystem::temp_directory_path() /
                   ("gitgui-pc-add-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(dir);
        git_repository* raw = nullptr;
        git_repository_init(&raw, dir.generic_string().c_str(), 0);
        git_repository_free(raw);

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        controller.addExistingRepo(QString::fromStdString(dir.generic_string()));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));
        std::filesystem::remove_all(dir);
    }

    void addExistingRepo_nonrepo_emits_repoAddFailed() {
        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAddFailed);
        controller.addExistingRepo(QStringLiteral("/no/such/gitgui-notarepo"));

        QCOMPARE(spy.count(), 1);
        QVERIFY(!spy.at(0).at(0).toString().isEmpty());
    }

    void initRepo_creates_repo_and_emits_repoAdded() {
        const auto parentDir = std::filesystem::temp_directory_path();
        const std::string repoName =
            "gitgui-pc-init-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        const auto dest = parentDir / repoName;

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        controller.initRepo(
            QString::fromStdString(parentDir.generic_string()),
            QString::fromStdString(repoName));

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::filesystem::exists(dest / ".git"));
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));
        std::filesystem::remove_all(dest);
    }
};

#include "test_project_controller.moc"
