#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QUuid>

#include <filesystem>
#include <fstream>
#include <git2.h>

#include <qcorotask.h>

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

    void cloneRepo_file_url_succeeds_and_emits_repoAdded() {
        // Create a source repo with one commit so transfer_progress fires
        auto srcDir = std::filesystem::temp_directory_path() /
                      ("gitgui-pc-src-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(srcDir);
        git_repository* srcRaw = nullptr;
        git_repository_init(&srcRaw, srcDir.generic_string().c_str(), 0);
        // Config + commit
        git_config* cfg = nullptr;
        git_repository_config(&cfg, srcRaw);
        git_config_set_string(cfg, "user.name", "T");
        git_config_set_string(cfg, "user.email", "t@e.x");
        git_config_free(cfg);
        { std::ofstream(srcDir / "README") << "hello\n"; }
        git_index* idx = nullptr; git_repository_index(&idx, srcRaw);
        git_index_add_bypath(idx, "README");
        git_index_write(idx);
        git_oid treeOid; git_index_write_tree(&treeOid, idx);
        git_tree* tree = nullptr; git_tree_lookup(&tree, srcRaw, &treeOid);
        git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
        git_oid cOid;
        git_commit_create_v(&cOid, srcRaw, "HEAD", sig, sig, nullptr, "init", tree, 0);
        git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
        git_repository_free(srcRaw);

        auto destDir = std::filesystem::temp_directory_path() /
                       ("gitgui-pc-dst-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::remove_all(destDir);  // clone creates it

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        QCoro::waitFor(controller.cloneRepo(
            QString::fromStdString("file://" + srcDir.generic_string()),
            QString::fromStdString(destDir.generic_string())));

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::filesystem::exists(destDir / ".git"));
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));

        std::filesystem::remove_all(srcDir);
        std::filesystem::remove_all(destDir);
    }

    void cloneRepo_invalid_url_emits_repoAddFailed() {
        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spyAdded(&controller, &ProjectController::repoAdded);
        QSignalSpy spyFailed(&controller, &ProjectController::repoAddFailed);
        QCoro::waitFor(controller.cloneRepo(
            QStringLiteral("file:///no/such/gitgui-repo-notexist"),
            QStringLiteral("/tmp/gitgui-clone-dst-noexist")));

        QCOMPARE(spyAdded.count(), 0);
        QCOMPARE(spyFailed.count(), 1);
        QVERIFY(!spyFailed.at(0).at(0).toString().isEmpty());
    }
};

#include "test_project_controller.moc"
