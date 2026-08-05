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
using gittide::RepoSource;
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
        for (const auto& root : m_scanRoots)
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
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
        { std::error_code rec; std::filesystem::remove_all(repoDir, rec); }
        {
            ProjectController controller(&store, storePath);
            controller.activate(QStringLiteral("id-a"));
            QCOMPARE(controller.lastActiveRepo(), QString());
        }

        { std::error_code rec; std::filesystem::remove_all(base, rec); }
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

    void active_project_repos_lists_path_and_name()
    {
        ProjectStore store;
        store.projects().push_back(
            Project{.id    = "id-a",
                    .name  = "Work",
                    .repos = {RepoRef{.path = "/home/u/api", .alias = "api"},
                              RepoRef{.path = "/home/u/web-client", .alias = ""}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        const QVariantList rows = controller.activeProjectRepos();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value("path").toString(), QStringLiteral("/home/u/api"));
        QCOMPARE(rows.at(0).toMap().value("name").toString(), QStringLiteral("api"));       // alias
        QCOMPARE(rows.at(1).toMap().value("path").toString(), QStringLiteral("/home/u/web-client"));
        QCOMPARE(rows.at(1).toMap().value("name").toString(), QStringLiteral("web-client")); // basename
    }

    void active_project_repos_empty_without_active_project()
    {
        ProjectStore      store;
        ProjectController controller(&store);
        QCOMPARE(controller.activeProjectRepos().size(), 0);
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
        { std::error_code rec; std::filesystem::remove_all(dir, rec); }
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

    void addRepos_adds_the_batch_and_saves_once()
    {
        const auto root      = makeScanRoot({"api", "web"});
        const auto storePath = std::filesystem::temp_directory_path() /
                               ("gittide-pc-batch-" +
                                QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() + ".json");

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store, storePath);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::reposAdded);
        controller.addRepos({QString::fromStdString((root / "api").generic_string()),
                             QString::fromStdString((root / "web").generic_string())},
                            {}, QString(), 2);

        QCOMPARE(spy.count(), 1); // one signal for the whole batch, not one per repo
        QCOMPARE(spy.at(0).at(0).toInt(), 2);
        QCOMPARE(spy.at(0).at(1).toStringList().size(), 0);
        QCOMPARE(controller.repos()->rowCount(), 2);

        auto reloaded = ProjectStore::load(storePath);
        QVERIFY(reloaded.has_value());
        QCOMPARE(static_cast<int>(reloaded->projects()[0].repos.size()), 2);
    }

    void addRepos_reports_failures_without_aborting_the_batch()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::reposAdded);
        controller.addRepos({QStringLiteral("/definitely/not/a/repo"),
                             QString::fromStdString((root / "api").generic_string())},
                            {}, QString(), 2);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        QCOMPARE(spy.at(0).at(1).toStringList().size(), 1);
        QCOMPARE(controller.repos()->rowCount(), 1);
    }

    void addRepos_registers_a_source_with_the_unchecked_paths_ignored()
    {
        const auto root = makeScanRoot({"api", "web"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        controller.addRepos({QString::fromStdString((root / "api").generic_string())},
                            {QString::fromStdString((root / "web").generic_string())},
                            QString::fromStdString(root.generic_string()), 3);

        QCOMPARE(static_cast<int>(store.projects()[0].sources.size()), 1);
        QCOMPARE(store.projects()[0].sources[0].maxDepth, 3);
        QCOMPARE(static_cast<int>(store.projects()[0].sources[0].ignored.size()), 1);
        QCOMPARE(QString::fromStdString(store.projects()[0].sources[0].ignored[0]),
                 QString::fromStdString((root / "web").generic_string()));
    }

    void addRepos_without_an_active_project_fails_loudly()
    {
        ProjectStore      store;
        ProjectController controller(&store);

        QSignalSpy spy(&controller, &ProjectController::repoAddFailed);
        controller.addRepos({QStringLiteral("/anything")}, {}, QString(), 2);

        QCOMPARE(spy.count(), 1);
    }

    void rescanSources_adds_a_repo_that_appeared_after_registration()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));
        controller.addRepos({QString::fromStdString((root / "api").generic_string())}, {},
                            QString::fromStdString(root.generic_string()), 1);
        QCOMPARE(controller.repos()->rowCount(), 1);

        // A repo cloned into the source folder after registration.
        const auto later = root / "web";
        std::filesystem::create_directories(later);
        git_repository* raw = nullptr;
        git_repository_init(&raw, later.generic_string().c_str(), 0);
        git_repository_free(raw);

        QSignalSpy spy(&controller, &ProjectController::sourcesRescanned);
        controller.rescanSources();
        QVERIFY(spy.wait(5000));

        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        QCOMPARE(controller.repos()->rowCount(), 2);
    }

    void rescanSources_never_re_adds_a_removed_repo()
    {
        const auto root = makeScanRoot({"api", "web"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));
        controller.addRepos({QString::fromStdString((root / "api").generic_string()),
                             QString::fromStdString((root / "web").generic_string())},
                            {}, QString::fromStdString(root.generic_string()), 1);
        QCOMPARE(controller.repos()->rowCount(), 2);

        controller.removeRepo(QString::fromStdString((root / "web").generic_string()));
        QCOMPARE(controller.repos()->rowCount(), 1);

        QSignalSpy spy(&controller, &ProjectController::sourcesRescanned);
        controller.rescanSources();
        QVERIFY(spy.wait(5000));

        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(controller.repos()->rowCount(), 1);
    }

    void rescanSources_counts_an_unavailable_source_and_keeps_going()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        // One source that does not exist, one that does.
        store.projects()[0].sources.push_back(gittide::RepoSource{.path = "/definitely/not/here", .maxDepth = 1});
        store.projects()[0].sources.push_back(
            gittide::RepoSource{.path = root.generic_string(), .maxDepth = 1});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::sourcesRescanned);
        QVERIFY(spy.wait(5000)); // activate() kicks the rescan itself

        QCOMPARE(spy.at(0).at(0).toInt(), 1); // the reachable source still added its repo
        QCOMPARE(spy.at(0).at(1).toInt(), 1); // and the missing one is reported
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
        { std::error_code rec; std::filesystem::remove_all(dest, rec); }
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
        { std::error_code rec; std::filesystem::remove_all(destDir, rec); } // clone creates it

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

        { std::error_code rec; std::filesystem::remove_all(srcDir, rec); }
        { std::error_code rec; std::filesystem::remove_all(destDir, rec); }
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
        { std::error_code rec; std::filesystem::remove_all(destDir, rec); }

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
        QVERIFY(added.wait(15000));
        QVERIFY(std::filesystem::exists(destDir / ".git"));

        { std::error_code rec; std::filesystem::remove_all(srcDir, rec); }
        { std::error_code rec; std::filesystem::remove_all(destDir, rec); }
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
        // activate() builds bare rows and hydrates them off-thread, so the
        // submodule subtree arrives a turn or two later.
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(top), 1, 15000);
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

    // A non-auth fetch failure (here: no 'origin' remote) surfaces through
    // fleetFetchFailed as a one-line "name: message" entry, so the UI can raise an
    // error dialog. Successful repos never appear in the list.
    void fetchAll_reports_failures_via_fleetFetchFailed()
    {
        const QString behindA = makeRepoBehindBy1();

        gittide::test::TempRepo noRemote;
        noRemote.setIdentity("N", "n@e.x");
        noRemote.writeFile("x.txt", "x");
        noRemote.commitAll("c1");
        const QString failPath = QString::fromStdString(noRemote.path().generic_string());
        const QString failName = QString::fromStdString(noRemote.path().filename().generic_string());

        ProjectStore store;
        store.projects().push_back(Project{.id = "p1", .name = "Fleet",
            .repos = {RepoRef{.path = behindA.toStdString()},
                      RepoRef{.path = failPath.toStdString()}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("p1"));

        QSignalSpy failed(&controller, &ProjectController::fleetFetchFailed);
        QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
        controller.fetchAll();
        QVERIFY(finished.wait(15000));

        QCOMPARE(failed.count(), 1);
        const QStringList msgs = failed.at(0).at(0).toStringList();
        QCOMPARE(msgs.size(), 1);                 // only the failing repo
        QVERIFY(msgs.at(0).contains(failName));   // names the repo
    }

    // Fleet fetch exposes determinate progress: fetchTotal is the batch size set
    // synchronously, fetchDone advances to fetchTotal once every repo settles.
    void fetchAll_exposes_determinate_progress()
    {
        const QString a = makeRepoBehindBy1();
        const QString b = makeRepoBehindBy1();

        ProjectStore store;
        store.projects().push_back(Project{.id = "p1", .name = "Fleet",
            .repos = {RepoRef{.path = a.toStdString()},
                      RepoRef{.path = b.toStdString()}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("p1"));

        QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
        controller.fetchAll();
        QCOMPARE(controller.fetchTotal(), 2);        // batch size, set synchronously
        QVERIFY(controller.fetchDone() < 2);         // not all settled yet
        QVERIFY(finished.wait(15000));

        QCOMPARE(controller.fetchDone(), controller.fetchTotal());   // full bar at the end
    }

    // A fully successful fleet fetch raises no error dialog.
    void fetchAll_no_failures_emits_no_fleetFetchFailed()
    {
        const QString behindA = makeRepoBehindBy1();

        ProjectStore store;
        store.projects().push_back(Project{.id = "p1", .name = "Fleet",
            .repos = {RepoRef{.path = behindA.toStdString()}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("p1"));

        QSignalSpy failed(&controller, &ProjectController::fleetFetchFailed);
        QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
        controller.fetchAll();
        QVERIFY(finished.wait(15000));

        QCOMPARE(failed.count(), 0);
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
        QTRY_VERIFY_WITH_TIMEOUT(m->data(m->index(0, 0), RepoListModel::AheadRole).toInt() == 1, 15000);
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
        QTRY_COMPARE_WITH_TIMEOUT(model->data(i0, RepoListModel::DirtyCountRole).toInt(), 1, 15000);
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

    // setRepos no longer reads git on the UI thread (that stalled every project
    // switch), so activate() must kick a hydration pass itself — the rows would
    // otherwise sit blank until the first poll tick, and not at all while the
    // window is unfocused.
    void activate_hydrates_rows_asynchronously()
    {
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo repo;
        repo.setIdentity("Test", "test@example.com");
        repo.writeFile("a.txt", "one\n");
        repo.commitAll("c1");
        repo.writeFile("a.txt", "two\n"); // dirty on disk before activate()

        gittide::ProjectStore store;
        auto& p = store.createProject("P");
        store.addRepo(p.id, gittide::RepoRef{.path = repo.path().generic_string()});

        // Long interval and an inactive window: only activate()'s own kick can
        // hydrate these rows.
        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/600000);
        controller.activate(QString::fromStdString(p.id));
        RepoListModel*    m  = controller.repos();
        const QModelIndex i0 = m->index(0, 0);

        QCOMPARE(m->data(i0, RepoListModel::DirtyCountRole).toInt(), 0); // not yet — async
        QTRY_COMPARE_WITH_TIMEOUT(m->data(i0, RepoListModel::DirtyCountRole).toInt(), 1, 15000);
        QVERIFY(!m->data(i0, RepoListModel::BranchRole).toString().isEmpty());
    }

    // The poll opens every repo in the project and awaits four git ops on each,
    // sequentially, on the pool the active repo's own refreshes share. With no
    // re-entrancy guard the timer kept stacking passes on top of a pass still
    // running, saturating that pool — the diffuse "everything is randomly slow".
    void poll_does_not_overlap_itself()
    {
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo repo;
        repo.setIdentity("Test", "test@example.com");
        repo.writeFile("a.txt", "one\n");
        repo.commitAll("c1");

        gittide::ProjectStore store;
        auto& p = store.createProject("P");
        store.addRepo(p.id, gittide::RepoRef{.path = repo.path().generic_string()});

        // Interval far shorter than one pass: the timer fires repeatedly while a
        // pass is still suspended on its git work.
        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/1);
        controller.activate(QString::fromStdString(p.id));
        controller.setWindowActive(true);
        QTest::qWait(600);
        controller.setWindowActive(false);
        QTest::qWait(300); // let the last pass unwind

        // Never more than one pass in flight at any instant.
        QCOMPARE(controller.maxConcurrentPolls(), 1);
    }

    // The repo open in the working pane is kept current by its own watcher and
    // by applyActiveRepoState, so re-reading it in the poll is pure waste on the
    // shared pool — and it is the repo the user is actually waiting on.
    void poll_skips_the_active_repo()
    {
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo active;
        active.setIdentity("Test", "test@example.com");
        active.writeFile("a.txt", "one\n");
        active.commitAll("c1");

        gittide::test::TempRepo other;
        other.setIdentity("Test", "test@example.com");
        other.writeFile("b.txt", "one\n");
        other.commitAll("c1");

        const QString activePath = QString::fromStdString(active.path().generic_string());

        gittide::ProjectStore store;
        auto& p = store.createProject("P");
        store.addRepo(p.id, gittide::RepoRef{.path = activePath.toStdString()});
        store.addRepo(p.id, gittide::RepoRef{.path = other.path().generic_string()});

        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/60);
        controller.activate(QString::fromStdString(p.id));
        RepoListModel* m = controller.repos();
        // activate() kicks one hydration pass covering every row (nothing is open
        // yet). Let it finish so what follows measures the steady-state poll.
        QTRY_VERIFY_WITH_TIMEOUT(!m->data(m->index(0, 0), RepoListModel::BranchRole).toString().isEmpty(), 15000);

        controller.setActiveRepo(activePath); // now it is the repo in the working pane

        // Dirty both trees on disk.
        active.writeFile("a.txt", "two\n");
        other.writeFile("b.txt", "two\n");

        controller.setWindowActive(true);
        // The non-active repo is picked up by the poll…
        QTRY_COMPARE_WITH_TIMEOUT(m->data(m->index(1, 0), RepoListModel::DirtyCountRole).toInt(), 1, 15000);
        // …while the active one is left to its own live refresh, so the poll has
        // not touched it. (In the running app applyActiveRepoState fills it in.)
        QCOMPARE(m->data(m->index(0, 0), RepoListModel::DirtyCountRole).toInt(), 0);
        controller.setWindowActive(false);
    }

    // The repo open in the working pane keeps itself current through its own
    // watcher and post-mutation cascades; nothing carried that into the sidebar
    // row, which was written only by the 5 s fleet poll. Reverting a change in
    // GitTide therefore emptied the Changes pane while the row kept its dirty
    // badge. applyActiveRepoState is the push that closes the gap — no timer.
    void applyActiveRepoState_updates_the_row_without_polling()
    {
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo repo;
        repo.setIdentity("Test", "test@example.com");
        repo.writeFile("a.txt", "one\n");
        repo.commitAll("c1");
        const QString path = QString::fromStdString(repo.path().generic_string());

        ProjectStore store;
        store.projects().push_back(
            Project{.id = "p1", .name = "P", .repos = {RepoRef{.path = path.toStdString()}}});

        // Deliberately long poll interval and an inactive window: nothing but the
        // push can move these values.
        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/600000);
        controller.activate(QStringLiteral("p1"));
        RepoListModel*    m  = controller.repos();
        const QModelIndex i0 = m->index(0, 0);

        controller.applyActiveRepoState(path, QStringLiteral("main"), false,
                                        QStringLiteral("abc1234"), 3);
        QCOMPARE(m->data(i0, RepoListModel::DirtyCountRole).toInt(), 3);
        QCOMPARE(m->data(i0, RepoListModel::BranchRole).toString(), QStringLiteral("main"));

        // The revert: back to clean, reflected immediately.
        controller.applyActiveRepoState(path, QStringLiteral("main"), false,
                                        QStringLiteral("abc1234"), 0);
        QCOMPARE(m->data(i0, RepoListModel::DirtyCountRole).toInt(), 0);

        controller.applyActiveRepoSync(path, 2, 1, true);
        QCOMPARE(m->data(i0, RepoListModel::AheadRole).toInt(), 2);
        QCOMPARE(m->data(i0, RepoListModel::BehindRole).toInt(), 1);
        QCOMPARE(m->data(i0, RepoListModel::HasUpstreamRole).toBool(), true);

        // A repo that is not in this project is ignored, not a crash.
        controller.applyActiveRepoState(QStringLiteral("/tmp/not-in-project"),
                                        QStringLiteral("x"), false, QString(), 9);
        QCOMPARE(m->data(i0, RepoListModel::DirtyCountRole).toInt(), 0);
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

    void scanFolder_lists_candidates_and_marks_already_added()
    {
        const auto root = makeScanRoot({"api", "web"});

        ProjectStore store;
        store.projects().push_back(Project{.id    = "id-a",
                                           .name  = "Work",
                                           .repos = {RepoRef{.path = (root / "api").generic_string()}}});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::scanFinished);
        controller.scanFolder(QString::fromStdString(root.generic_string()), 1);
        QVERIFY(spy.wait(5000));

        const QVariantList candidates = spy.at(0).at(0).toList();
        QCOMPARE(candidates.size(), 2);

        // Sorted by path, so "api" precedes "web".
        const QVariantMap api = candidates.at(0).toMap();
        QCOMPARE(api.value("name").toString(), QStringLiteral("api"));
        QCOMPARE(api.value("alreadyAdded").toBool(), true);
        QCOMPARE(candidates.at(1).toMap().value("alreadyAdded").toBool(), false);
    }

    void scanFolder_reports_a_missing_folder()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::scanFailed);
        controller.scanFolder(QStringLiteral("/definitely/not/here"), 2);
        QVERIFY(spy.wait(5000));
        QVERIFY(!spy.at(0).at(0).toString().isEmpty());
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

    // A scratch folder holding one empty repository per name, removed with the
    // fixture. Used to exercise the folder scan without a full TempRepo each.
    std::filesystem::path makeScanRoot(const QStringList& names)
    {
        const auto root = std::filesystem::temp_directory_path() /
                          ("gittide-pc-scan-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(root);
        for (const QString& name : names)
        {
            const auto dir = root / name.toStdString();
            std::filesystem::create_directories(dir);
            git_repository* raw = nullptr;
            git_repository_init(&raw, dir.generic_string().c_str(), 0);
            git_repository_free(raw);
        }
        m_scanRoots.push_back(root);
        return root;
    }

    std::vector<std::unique_ptr<gittide::test::TempRepo>> m_temps;
    std::vector<std::filesystem::path>                    m_scanRoots;
};

#include "test_project_controller.moc"
