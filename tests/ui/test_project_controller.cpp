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

    void activate_persists_active_project_to_disk()
    {
        const auto storePath = std::filesystem::temp_directory_path() /
                               ("gittide-pc-active-" +
                                QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() + ".json");

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        store.projects().push_back(Project{.id = "id-b", .name = "Play"});

        ProjectController controller(&store, storePath);
        controller.activate(QStringLiteral("id-b"));

        // Reloading the on-disk store must remember id-b as the active project.
        auto reloaded = ProjectStore::load(storePath);
        QVERIFY(reloaded.has_value());
        QCOMPARE(QString::fromStdString(reloaded->activeProject()), QStringLiteral("id-b"));

        std::filesystem::remove(storePath);
    }

    void setActiveRepo_persists_and_restores_with_stale_guard()
    {
        const auto base = std::filesystem::temp_directory_path() /
                          ("gittide-pc-lar-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        const auto repoDir  = base / "repo";
        const auto storePath = base / "projects.json";
        std::filesystem::create_directories(repoDir);

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});

        {
            ProjectController controller(&store, storePath);
            controller.activate(QStringLiteral("id-a"));
            controller.setActiveRepo(QString::fromStdString(repoDir.generic_string()));
            // Persisted to disk under the active project.
            auto reloaded = ProjectStore::load(storePath);
            QVERIFY(reloaded.has_value());
            QCOMPARE(QString::fromStdString(reloaded->projects().front().lastActiveRepo),
                     QString::fromStdString(repoDir.generic_string()));
            // Live accessor returns it while the folder exists.
            QCOMPARE(controller.lastActiveRepo(), QString::fromStdString(repoDir.generic_string()));
        }

        // Stale guard: once the folder is gone, the accessor reports empty so the
        // caller falls back to the first repo.
        std::filesystem::remove_all(repoDir);
        {
            ProjectController controller(&store, storePath);
            controller.activate(QStringLiteral("id-a"));
            QCOMPARE(controller.lastActiveRepo(), QString());
        }

        std::filesystem::remove_all(base);
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

    void initSubmodule_reinitialises_and_refreshes_tree()
    {
        using gittide::SubmoduleStatus;
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("seed child");

        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("seed parent");
        parent.addSubmodule("sub", child.path());
        parent.commitAll("add submodule");
        {
            auto repo = gittide::GitRepo::open(parent.path());
            QVERIFY(repo && repo->deinitSubmodule("sub"));
        }

        const QString repoPath = QString::fromStdString(parent.path().generic_string());
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "p", .name = "P", .repos = {RepoRef{.path = repoPath.toStdString()}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("p"));

        RepoListModel* model = controller.repos();
        const QModelIndex top = model->index(0, 0);
        QCOMPARE(model->rowCount(top), 1);
        const QModelIndex sub = model->index(0, 0, top);
        // deinitSubmodule keeps the .git gitlink; libgit2 reports Dirty (==1), not Uninitialized (==2)
        QCOMPARE(model->data(sub, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Dirty));

        const QString subPath = repoPath + QStringLiteral("/sub");
        QCoro::waitFor(controller.initSubmodule(repoPath, subPath));

        // Subtree was rebuilt; the (new) row is now Clean.
        const QModelIndex sub2 = model->index(0, 0, model->index(0, 0));
        QCOMPARE(model->data(sub2, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Clean));
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

    // D35: while the window is active, the poll re-reads each repo's local sync
    // counts (no network) and updates the sidebar — so a commit made in a
    // non-active repo shows up without any user action.
    void poll_refreshes_repo_sync_counts_when_window_active()
    {
        using gittide::ui::RepoListModel;

        // setRepos seeds sync counts synchronously from disk, so start the repo
        // at ahead=0 (upstream tracked, no local-only commit yet) and advance it
        // *after* activation to exercise the poll's refresh, not the seed.
        gittide::test::TempRepo* repo = makeRepoWithUpstream();
        const QString             path = QString::fromStdString(repo->path().generic_string());
        ProjectStore              store;
        store.projects().push_back(
            Project{.id = "p1", .name = "Fleet", .repos = {RepoRef{.path = path.toStdString()}}});

        ProjectController controller(&store, {}, nullptr, 80); // fast poll for the test
        controller.activate(QStringLiteral("p1"));
        RepoListModel* m = controller.repos();
        QCOMPARE(m->data(m->index(0, 0), RepoListModel::AheadRole).toInt(), 0); // seeded, not yet ahead

        repo->writeFile("a.txt", "two");
        repo->commitAll("c2"); // HEAD = c2 while origin/master = c1 → ahead by 1, behind the GUI's back

        controller.setWindowActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(m->data(m->index(0, 0), RepoListModel::AheadRole).toInt() == 1, 5000);
        controller.setWindowActive(false);
    }

    // D35 submodule refresh: while the window is active, the poll re-reads each
    // repo's submodule tree and updates the sidebar — so an external
    // `git submodule deinit` shows up without any user action.
    void poll_refreshes_submodule_subtree_on_external_change()
    {
        using gittide::SubmoduleStatus;
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("seed child");
        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("seed parent");
        parent.addSubmodule("sub", child.path()); // initialised
        parent.commitAll("add submodule");

        const QString repoPath = QString::fromStdString(parent.path().generic_string());
        ProjectStore  store;
        store.projects().push_back(
            Project{.id = "p", .name = "P", .repos = {RepoRef{.path = repoPath.toStdString()}}});

        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/50);
        controller.activate(QStringLiteral("p"));
        RepoListModel*    model = controller.repos();
        const QModelIndex sub   = model->index(0, 0, model->index(0, 0));
        QCOMPARE(model->data(sub, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Clean));

        // External change: deinit on disk behind the GUI's back.
        // deinitSubmodule keeps the .git gitlink; libgit2 reports Dirty (==1).
        {
            auto repo = gittide::GitRepo::open(parent.path());
            QVERIFY(repo && repo->deinitSubmodule("sub"));
        }

        controller.setWindowActive(true); // starts the poll
        QTRY_VERIFY_WITH_TIMEOUT(
            model->data(model->index(0, 0, model->index(0, 0)), RepoListModel::StatusRole).toInt()
                == static_cast<int>(SubmoduleStatus::Dirty),
            5000);
        controller.setWindowActive(false);
    }

    void pollRepos_refreshes_branch_and_dirty()
    {
        using namespace gittide::test;
        using gittide::ui::ProjectController;
        using gittide::ui::RepoListModel;

        TempRepo repo;
        repo.setIdentity("Test", "test@example.com");
        repo.writeFile("a.txt", "one\n");
        repo.commitAll("c1");

        gittide::ProjectStore store;
        auto& p = store.createProject("P");
        store.addRepo(p.id, gittide::RepoRef{.path = repo.path().generic_string()});

        // Short poll interval so the timer fires quickly under QTRY.
        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/100);
        controller.activate(QString::fromStdString(p.id));

        RepoListModel*    model = controller.repos();
        const QModelIndex i0    = model->index(0, 0);
        QCOMPARE(model->data(i0, RepoListModel::DirtyCountRole).toInt(), 0); // seeded clean

        // Dirty the tree on disk, then let the poll pick it up.
        repo.writeFile("a.txt", "two\n");
        controller.setWindowActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(model->data(i0, RepoListModel::DirtyCountRole).toInt(), 1, 5000);
        QVERIFY(!model->data(i0, RepoListModel::BranchRole).toString().isEmpty());
    }

    // The poll must not run while the window is inactive.
    void poll_does_not_run_when_window_inactive()
    {
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo* repo = makeRepoWithUpstream();
        const QString             path = QString::fromStdString(repo->path().generic_string());
        ProjectStore              store;
        store.projects().push_back(
            Project{.id = "p1", .name = "Fleet", .repos = {RepoRef{.path = path.toStdString()}}});

        ProjectController controller(&store, {}, nullptr, 80);
        controller.activate(QStringLiteral("p1"));
        RepoListModel* m = controller.repos();

        repo->writeFile("a.txt", "two");
        repo->commitAll("c2"); // ahead by 1 on disk, but window never activated → no poll

        QTest::qWait(400); // window never activated → no poll
        QCOMPARE(m->data(m->index(0, 0), RepoListModel::AheadRole).toInt(), 0);
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

    // Calling activate() while a fleet fetch is in flight must be a no-op: the
    // active project must stay the same and the repo model must not be rebuilt.
    // This is testable deterministically because fetchingAll is set to true
    // synchronously by fetchAll() before any coroutine suspension point, so
    // calling activate() in the same event-loop turn (before any QCoreApplication
    // event processing) hits the guard reliably.
    void activate_during_fetch_is_blocked()
    {
        using gittide::ui::RepoListModel;

        const QString behindA = makeRepoBehindBy1();
        const QString behindB = makeRepoBehindBy1();

        ProjectStore store;
        store.projects().push_back(Project{.id = "p1", .name = "Fleet",
            .repos = {RepoRef{.path = behindA.toStdString()},
                      RepoRef{.path = behindB.toStdString()}}});
        store.projects().push_back(Project{.id = "p2", .name = "Other"});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("p1"));
        QCOMPARE(controller.activeProjectId(), QStringLiteral("p1"));
        QCOMPARE(controller.repos()->rowCount(), 2);

        QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
        controller.fetchAll();
        // fetchingAll is set synchronously before the first co_await
        QVERIFY(controller.fetchingAll());

        // Attempt to switch project in the same event-loop turn — must be gated.
        controller.activate(QStringLiteral("p2"));
        QCOMPARE(controller.activeProjectId(), QStringLiteral("p1")); // unchanged
        QCOMPARE(controller.repos()->rowCount(), 2);                   // model not rebuilt

        // Let the fetch run to completion.
        QVERIFY(finished.wait(15000));
        QVERIFY(!controller.fetchingAll());
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

    // Returns the path of a working repo whose local HEAD is one commit AHEAD of
    // its (already-known) tracking ref — so syncStatus reports ahead=1 with no
    // fetch. Kept alive in m_temps.
    // Repo with an upstream tracking branch and no local-only commits yet
    // (ahead = behind = 0). Callers that need an ahead/behind count add
    // commits themselves once the repo is under test, so the seeded state at
    // activation time is deterministic.
    gittide::test::TempRepo* makeRepoWithUpstream()
    {
        auto repo = std::make_unique<gittide::test::TempRepo>();
        repo->setIdentity("Test", "test@example.com");
        repo->writeFile("a.txt", "one");
        repo->commitAll("c1");
        const auto bare = repo->addBareRemote("origin");
        repo->pushBranch("origin", "master"); // origin/master = c1, upstream set

        auto* ptr = repo.get();
        m_temps.push_back(std::move(repo));
        return ptr;
    }

    std::vector<std::unique_ptr<gittide::test::TempRepo>> m_temps;
};

#include "test_project_controller.moc"
