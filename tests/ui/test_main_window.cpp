#include <QAbstractButton>
#include <QFrame>
#include <QtWidgets/QStatusBar>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QThreadPool>
#include <QToolButton>
#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>
#include <git2.h>

#include "gittide/projectstore.hpp"
#include "gittide/ui/branchbar.hpp"
#include "gittide/ui/changesview.hpp"
#include "gittide/ui/mainwindow.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/projectsidebar.hpp"
#include "gittide/ui/repolistmodel.hpp"

using gittide::Project;
using gittide::ProjectStore;
using gittide::RepoRef;
using gittide::ui::MainWindow;

namespace main_window_test {
std::filesystem::path make_repo()
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-mw-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

// Repo with an initial commit on "main" and a second local branch "feature".
std::filesystem::path make_repo_with_two_branches()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gittide-mw2b-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);

    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    // Write a file and create the initial commit on HEAD (master/main).
    { std::ofstream(dir / "a.txt") << "one\n"; }
    git_index* idx = nullptr;
    git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt");
    git_index_write(idx);
    git_oid tree_oid;
    git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr;
    git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr;
    git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid;
    git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, "init", tree, 0);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(idx);

    // Create a second branch "feature" pointing at the same commit.
    git_commit* commit = nullptr;
    git_commit_lookup(&commit, raw, &commit_oid);
    git_reference* ref = nullptr;
    git_branch_create(&ref, raw, "feature", commit, 0);
    git_reference_free(ref);
    git_commit_free(commit);

    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

// MainWindow wires project/repo activation to fire-and-forget QCoro tasks
// (dashboard refresh, status refresh). Those run on the global thread pool and
// resume via the event loop. A test must let that work finish BEFORE its local
// MainWindow is destroyed, or an in-flight coroutine resumes into freed objects.
// Call this at the end of each slot, while the window is still alive.
void drainAsync()
{
    for (int i = 0; i < 3; ++i)
    {
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(30);
    }
}
} // namespace main_window_test

class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void show_project_activates_and_lists_repos()
    {
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "id-a", .name = "Work", .repos = {RepoRef{.path = "/home/u/api", .alias = "api"}}});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));
        QCOMPARE(win.currentProjectId(), QStringLiteral("id-a"));
        QCOMPARE(win.controller()->repos()->rowCount(), 1);
        auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 2);
        main_window_test::drainAsync();
    }

    void central_layout_has_no_dashboard_and_a_shared_diff()
    {
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "id-a", .name = "Work", .repos = {RepoRef{.path = "/home/u/api", .alias = "api"}}});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs);
        // exactly two sub-tabs now: Changes, History
        QCOMPARE(tabs->count(), 2);
        QVERIFY(!win.findChild<QWidget*>(QStringLiteral("dashboardList")));
        // one shared diff panel exists
        QVERIFY(win.findChild<QWidget*>(QStringLiteral("diffLines")));
        // sidebar collapse toggle exists
        QVERIFY(win.findChild<QAbstractButton*>(QStringLiteral("sidebarCollapseButton")));
        main_window_test::drainAsync();
    }

    void open_in_new_window_propagates_upward()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));
        QSignalSpy spy(&win, &MainWindow::openInNewWindowRequested);
        auto* sidebar = win.findChild<gittide::ui::ProjectSidebar*>();
        QVERIFY(sidebar != nullptr);
        sidebar->requestOpenInNewWindow();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
        main_window_test::drainAsync();
    }

    void changes_tab_is_a_changes_view()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        MainWindow win(&store);
        QVERIFY(win.findChild<gittide::ui::ChangesView*>() != nullptr);
        main_window_test::drainAsync();
    }

    void selecting_repo_opens_it_in_controller()
    {
        const auto dir = main_window_test::make_repo();
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "id-a", .name = "Work", .repos = {RepoRef{.path = dir.generic_string(), .alias = "r"}}});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* sidebar = win.findChild<gittide::ui::ProjectSidebar*>();
        QSignalSpy spy(&win, &MainWindow::repoOpened);
        emit sidebar->repoSelected(QString::fromStdString(dir.generic_string()));

        QCOMPARE(spy.count(), 1);
        main_window_test::drainAsync();
        std::filesystem::remove_all(dir);
    }

    void no_projects_shows_create_project_cta()
    {
        ProjectStore store;
        MainWindow win(&store);

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 0);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("createProjectCta")) != nullptr);
        main_window_test::drainAsync();
    }

    void empty_project_shows_add_repo_cta()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Empty"});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 1);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("addExistingCta")) != nullptr);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("initRepoCta")) != nullptr);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("cloneCta")) != nullptr);
        main_window_test::drainAsync();
    }

    void no_projects_page_has_branded_card()
    {
        ProjectStore store;
        MainWindow win(&store);
        auto* card = win.findChild<QFrame*>(QStringLiteral("emptyStateCard"));
        QVERIFY(card != nullptr);
        // headline + the existing CTA still live inside the card
        QVERIFY(card->findChild<QLabel*>() != nullptr);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("createProjectCta")) != nullptr);
        main_window_test::drainAsync();
    }

    void project_with_repos_shows_tabs()
    {
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "id-a", .name = "Work", .repos = {RepoRef{.path = "/home/u/api", .alias = "api"}}});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 2);
        auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs != nullptr);
        main_window_test::drainAsync();
    }

    void failed_repo_open_shows_status_bar_message()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "P"});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* sidebar = win.findChild<gittide::ui::ProjectSidebar*>();
        QVERIFY(sidebar != nullptr);
        // Opening a non-existent path triggers repoFailed → status bar
        emit sidebar->repoSelected(QStringLiteral("/no/such/gittide-path"));
        QVERIFY(!win.statusBar()->currentMessage().isEmpty());
        main_window_test::drainAsync();
    }

    void branch_bar_exists_and_shows_current_branch_after_open()
    {
        const auto dir = main_window_test::make_repo_with_two_branches();
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "id-a", .name = "Work", .repos = {RepoRef{.path = dir.generic_string(), .alias = "r"}}});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        // A BranchBar must be present in the widget tree.
        auto* bar = win.findChild<gittide::ui::BranchBar*>(QStringLiteral("branchBar"));
        QVERIFY(bar != nullptr);

        // Open the repo by emitting repoSelected from the sidebar.
        auto* sidebar = win.findChild<gittide::ui::ProjectSidebar*>();
        QVERIFY(sidebar != nullptr);
        qRegisterMetaType<std::vector<gittide::BranchInfo>>();
        qRegisterMetaType<gittide::HeadState>();
        emit sidebar->repoSelected(QString::fromStdString(dir.generic_string()));

        // Drain the async cascade so refreshBranches/headChanged can complete.
        main_window_test::drainAsync();

        // The current-branch button must show a non-empty branch name.
        auto* btn = bar->findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn != nullptr);
        QVERIFY(!btn->text().isEmpty());
        // The initial commit was on the default branch (not detached, not "(no commits)").
        QVERIFY(btn->text() != QStringLiteral("(no commits)"));
        QVERIFY(!btn->text().startsWith(QStringLiteral("detached")));

        std::filesystem::remove_all(dir);
    }
};

#include "test_main_window.moc"
