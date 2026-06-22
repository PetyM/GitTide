#include <QObject>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>
#include <git2.h>
#include <memory>
#include <qcorotask.h>

#include "gittide/projectstore.hpp"
#include "gittide/ui/asyncrepo.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "support/temprepo.hpp"

using gittide::Project;
using gittide::ProjectStore;
using gittide::RepoRef;
using gittide::ui::ProjectController;

class TestProjectController : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        git_libgit2_init();
    }
    void cleanupTestCase()
    {
        git_libgit2_shutdown();
    }

    void activate_loads_repos_and_emits()
    {
        ProjectStore store;
        store.projects().push_back(
            Project{.id    = "id-a",
                    .name  = "Work",
                    .repos = {RepoRef{.path = "/home/u/api", .alias = "api"}, RepoRef{.path = "/home/u/web", .alias = "web"}}});

        ProjectController controller(&store);
        QSignalSpy spy(&controller, &ProjectController::projectActivated);

        controller.activate(QStringLiteral("id-a"));

        QCOMPARE(controller.activeProjectId(), QStringLiteral("id-a"));
        QCOMPARE(controller.repos()->rowCount(), 2);
        QCOMPARE(QString::fromStdString(store.activeProject()), QStringLiteral("id-a"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
    }

    void activate_unknown_id_is_ignored()
    {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy spy(&controller, &ProjectController::projectActivated);

        controller.activate(QStringLiteral("nope"));

        QCOMPARE(controller.activeProjectId(), QString());
        QCOMPARE(spy.count(), 0);
    }

    void activeProjectName_tracks_active_project()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        store.projects().push_back(Project{.id = "id-b", .name = "Play"});

        ProjectController controller(&store);
        QCOMPARE(controller.activeProjectName(), QString()); // none active yet

        controller.activate(QStringLiteral("id-b"));
        QCOMPARE(controller.activeProjectName(), QStringLiteral("Play"));

        controller.activate(QStringLiteral("id-a"));
        QCOMPARE(controller.activeProjectName(), QStringLiteral("Work"));
    }

    void removeProject_activates_next_and_updates_name()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        store.projects().push_back(Project{.id = "id-b", .name = "Play"});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::projectRemoved);
        controller.removeProject();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(store.projects().size(), std::size_t(1));
        // The surviving project becomes active.
        QCOMPARE(controller.activeProjectName(), QStringLiteral("Play"));
    }

    void removeProject_last_one_leaves_no_active()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Solo"});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));
        controller.removeProject();

        QCOMPARE(store.projects().size(), std::size_t(0));
        QCOMPARE(controller.activeProjectId(), QString());
        QCOMPARE(controller.activeProjectName(), QString());
    }

    void createProject_appends_project_and_emits()
    {
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

    void createProject_empty_name_is_ignored()
    {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy spy(&controller, &ProjectController::projectCreated);

        controller.createProject(QStringLiteral("   "));

        QCOMPARE(store.projects().size(), std::size_t(0));
        QCOMPARE(spy.count(), 0);
    }

    void addExistingRepo_valid_repo_emits_repoAdded()
    {
        auto dir = std::filesystem::temp_directory_path() /
                   ("gittide-pc-add-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
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

    void addExistingRepo_nonrepo_emits_repoAddFailed()
    {
        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAddFailed);
        controller.addExistingRepo(QStringLiteral("/no/such/gittide-notarepo"));

        QCOMPARE(spy.count(), 1);
        QVERIFY(!spy.at(0).at(0).toString().isEmpty());
    }

    void initRepo_creates_repo_and_emits_repoAdded()
    {
        const auto parentDir       = std::filesystem::temp_directory_path();
        const std::string repoName = "gittide-pc-init-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        const auto dest            = parentDir / repoName;

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        controller.initRepo(QString::fromStdString(parentDir.generic_string()), QString::fromStdString(repoName));

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::filesystem::exists(dest / ".git"));
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));
        std::filesystem::remove_all(dest);
    }

    void cloneRepo_file_url_succeeds_and_emits_repoAdded()
    {
        // Create a source repo with one commit so transfer_progress fires
        auto srcDir = std::filesystem::temp_directory_path() /
                      ("gittide-pc-src-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(srcDir);
        git_repository* srcRaw = nullptr;
        git_repository_init(&srcRaw, srcDir.generic_string().c_str(), 0);
        // Config + commit
        git_config* cfg = nullptr;
        git_repository_config(&cfg, srcRaw);
        git_config_set_string(cfg, "user.name", "T");
        git_config_set_string(cfg, "user.email", "t@e.x");
        git_config_free(cfg);
        {
            std::ofstream(srcDir / "README") << "hello\n";
        }
        git_index* idx = nullptr;
        git_repository_index(&idx, srcRaw);
        git_index_add_bypath(idx, "README");
        git_index_write(idx);
        git_oid treeOid;
        git_index_write_tree(&treeOid, idx);
        git_tree* tree = nullptr;
        git_tree_lookup(&tree, srcRaw, &treeOid);
        git_signature* sig = nullptr;
        git_signature_now(&sig, "T", "t@e.x");
        git_oid cOid;
        git_commit_create_v(&cOid, srcRaw, "HEAD", sig, sig, nullptr, "init", tree, 0);
        git_signature_free(sig);
        git_tree_free(tree);
        git_index_free(idx);
        git_repository_free(srcRaw);

        auto destDir = std::filesystem::temp_directory_path() /
                       ("gittide-pc-dst-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::remove_all(destDir); // clone creates it

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        // Build an RFC 8089 file:// URL: POSIX paths start with '/' (file:///tmp/x),
        // Windows paths start with a drive letter and need the extra slash
        // (file:///C:/x), else libgit2 reads "C:" as the URL host and the clone fails.
        const auto srcGeneric = srcDir.generic_string();
        const std::string srcUrl =
            (srcGeneric.starts_with('/') ? "file://" : "file:///") + srcGeneric;

        QSignalSpy spy(&controller, &ProjectController::repoAdded);
        QCoro::waitFor(controller.cloneRepo(QString::fromStdString(srcUrl),
                                            QString::fromStdString(destDir.generic_string())));

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::filesystem::exists(destDir / ".git"));
        QCOMPARE(store.projects()[0].repos.size(), std::size_t(1));

        std::filesystem::remove_all(srcDir);
        std::filesystem::remove_all(destDir);
    }

    void startClone_file_url_emits_repoAdded()
    {
        // Source repo with one commit (so the clone has content to transfer).
        auto srcDir = std::filesystem::temp_directory_path() /
                      ("gittide-pc-sc-src-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(srcDir);
        git_repository* srcRaw = nullptr;
        git_repository_init(&srcRaw, srcDir.generic_string().c_str(), 0);
        git_config* cfg = nullptr;
        git_repository_config(&cfg, srcRaw);
        git_config_set_string(cfg, "user.name", "T");
        git_config_set_string(cfg, "user.email", "t@e.x");
        git_config_free(cfg);
        {
            std::ofstream(srcDir / "README") << "hello\n";
        }
        git_index* idx = nullptr;
        git_repository_index(&idx, srcRaw);
        git_index_add_bypath(idx, "README");
        git_index_write(idx);
        git_oid treeOid;
        git_index_write_tree(&treeOid, idx);
        git_tree* tree = nullptr;
        git_tree_lookup(&tree, srcRaw, &treeOid);
        git_signature* sig = nullptr;
        git_signature_now(&sig, "T", "t@e.x");
        git_oid cOid;
        git_commit_create_v(&cOid, srcRaw, "HEAD", sig, sig, nullptr, "init", tree, 0);
        git_signature_free(sig);
        git_tree_free(tree);
        git_index_free(idx);
        git_repository_free(srcRaw);

        auto destDir = std::filesystem::temp_directory_path() /
                       ("gittide-pc-sc-dst-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::remove_all(destDir);

        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        const auto        srcGeneric = srcDir.generic_string();
        const std::string srcUrl     = (srcGeneric.starts_with('/') ? "file://" : "file:///") + srcGeneric;

        // startClone is fire-and-forget: it must kick the coroutine itself, so the
        // signal arrives without the caller awaiting a Task.
        QSignalSpy added(&controller, &ProjectController::repoAdded);
        controller.startClone(QString::fromStdString(srcUrl), QString::fromStdString(destDir.generic_string()));
        QVERIFY(added.wait(5000));
        QVERIFY(std::filesystem::exists(destDir / ".git"));

        std::filesystem::remove_all(srcDir);
        std::filesystem::remove_all(destDir);
    }

    void cloneRepo_invalid_url_emits_repoAddFailed()
    {
        ProjectStore store;
        auto& p = store.createProject("proj");
        ProjectController controller(&store);
        controller.activate(QString::fromStdString(p.id));

        QSignalSpy spyAdded(&controller, &ProjectController::repoAdded);
        QSignalSpy spyFailed(&controller, &ProjectController::repoAddFailed);
        QCoro::waitFor(controller.cloneRepo(QStringLiteral("file:///no/such/gittide-repo-notexist"),
                                            QStringLiteral("/tmp/gittide-clone-dst-noexist")));

        QCOMPARE(spyAdded.count(), 0);
        QCOMPARE(spyFailed.count(), 1);
        QVERIFY(!spyFailed.at(0).at(0).toString().isEmpty());
    }

    void cleanup()
    {
        m_temps.clear();
    }

    void fetchAll_updates_behind_repos_and_marks_failures()
    {
        using gittide::ui::RepoListModel;

        const QString behindA = makeRepoBehindBy1();
        const QString behindB = makeRepoBehindBy1();

        // A repo with no 'origin' remote -> fetch fails.
        gittide::test::TempRepo noRemote;
        noRemote.setIdentity("N", "n@e.x");
        noRemote.writeFile("x.txt", "x");
        noRemote.commitAll("c1");
        const QString failPath = QString::fromStdString(noRemote.path().generic_string());

        ProjectStore store;
        store.projects().push_back(Project{.id = "p1", .name = "Fleet",
            .repos = {RepoRef{.path = behindA.toStdString()},
                      RepoRef{.path = behindB.toStdString()},
                      RepoRef{.path = failPath.toStdString()}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("p1"));

        QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
        QVERIFY(controller.fetchSummary().isEmpty());
        controller.fetchAll();
        QVERIFY(controller.fetchingAll());           // turns on synchronously
        QVERIFY(finished.wait(15000));               // all repos settle

        QCOMPARE(finished.at(0).at(0).toInt(), 2);   // ok
        QCOMPARE(finished.at(0).at(1).toInt(), 1);   // failed
        QVERIFY(!controller.fetchingAll());
        QCOMPARE(controller.fetchSummary(), QStringLiteral("2 fetched, 1 failed"));

        RepoListModel* m = controller.repos();
        QCOMPARE(m->data(m->index(0, 0), RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Updated));
        QCOMPARE(m->data(m->index(0, 0), RepoListModel::BehindRole).toInt(), 1);
        QCOMPARE(m->data(m->index(2, 0), RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Failed));
    }

    void fetchAll_no_active_project_is_noop()
    {
        ProjectStore store;
        ProjectController controller(&store);
        QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
        controller.fetchAll();
        QVERIFY(!controller.fetchingAll());
        QCOMPARE(finished.count(), 0);
    }

    void submitFleetCredentials_with_no_pending_is_safe_noop()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "p1", .name = "Fleet"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("p1"));

        QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
        controller.submitFleetCredentials(QStringLiteral("u"), QStringLiteral("t")); // nothing pending
        QVERIFY(!controller.fetchingAll());
        QCOMPARE(finished.count(), 0);
    }

private:
    // Returns the path of a fresh working repo whose 'origin' is one commit ahead.
    // Kept alive by leaking the TempRepos into a member vector (cleaned in dtor).
    QString makeRepoBehindBy1()
    {
        auto repo = std::make_unique<gittide::test::TempRepo>();
        repo->setIdentity("Test", "test@example.com");
        repo->writeFile("a.txt", "one");
        repo->commitAll("c1");
        const auto bare = repo->addBareRemote("origin");
        repo->pushBranch("origin", "master");

        auto other = std::make_unique<gittide::test::TempRepo>();
        other->cloneFrom(bare);
        other->setIdentity("Other", "o@example.com");
        other->writeFile("a.txt", "two");
        other->commitAll("c2");
        other->pushBranch("origin", "master");

        const QString p = QString::fromStdString(repo->path().generic_string());
        m_temps.push_back(std::move(repo));
        m_temps.push_back(std::move(other));
        return p;
    }

    std::vector<std::unique_ptr<gittide::test::TempRepo>> m_temps;
};

#include "test_project_controller.moc"
