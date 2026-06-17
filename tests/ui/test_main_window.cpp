#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QThreadPool>
#include <filesystem>

#include <git2.h>

#include "gittide/projectstore.hpp"
#include "gittide/ui/MainWindow.hpp"
#include "gittide/ui/ProjectController.hpp"
#include "gittide/ui/ProjectSidebar.hpp"
#include "gittide/ui/ChangesView.hpp"

using gittide::Project;
using gittide::ProjectStore;
using gittide::RepoRef;
using gittide::ui::MainWindow;

namespace main_window_test {
std::filesystem::path make_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gittide-mw-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

// MainWindow wires project/repo activation to fire-and-forget QCoro tasks
// (dashboard refresh, status refresh). Those run on the global thread pool and
// resume via the event loop. A test must let that work finish BEFORE its local
// MainWindow is destroyed, or an in-flight coroutine resumes into freed objects.
// Call this at the end of each slot, while the window is still alive.
void drainAsync() {
    for (int i = 0; i < 3; ++i) {
        QThreadPool::globalInstance()->waitForDone();
        QTest::qWait(30);
    }
}
}  // namespace main_window_test

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void show_project_activates_and_lists_repos() {
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = "/home/u/api", .alias = "api"} }});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));
        QCOMPARE(win.currentProjectId(), QStringLiteral("id-a"));
        QCOMPARE(win.controller()->repos()->rowCount(), 1);
        auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 3);
        main_window_test::drainAsync();
    }

    void open_in_new_window_propagates_upward() {
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

    void changes_tab_is_a_changes_view() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        MainWindow win(&store);
        QVERIFY(win.findChild<gittide::ui::ChangesView*>() != nullptr);
        main_window_test::drainAsync();
    }

    void selecting_repo_opens_it_in_controller() {
        const auto dir = main_window_test::make_repo();
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = dir.generic_string(), .alias = "r"} }});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* sidebar = win.findChild<gittide::ui::ProjectSidebar*>();
        QSignalSpy spy(&win, &MainWindow::repoOpened);
        emit sidebar->repoSelected(QString::fromStdString(dir.generic_string()));

        QCOMPARE(spy.count(), 1);
        main_window_test::drainAsync();
        std::filesystem::remove_all(dir);
    }

    void no_projects_shows_create_project_cta() {
        ProjectStore store;
        MainWindow win(&store);

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 0);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("createProjectCta")) != nullptr);
        main_window_test::drainAsync();
    }

    void empty_project_shows_add_repo_cta() {
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

    void no_projects_page_has_branded_card() {
        ProjectStore store;
        MainWindow win(&store);
        auto* card = win.findChild<QFrame*>(QStringLiteral("emptyStateCard"));
        QVERIFY(card != nullptr);
        // headline + the existing CTA still live inside the card
        QVERIFY(card->findChild<QLabel*>() != nullptr);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("createProjectCta")) != nullptr);
        main_window_test::drainAsync();
    }

    void project_with_repos_shows_tabs() {
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = "/home/u/api", .alias = "api"} }});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* stack = win.findChild<QStackedWidget*>(QStringLiteral("centralStack"));
        QVERIFY(stack != nullptr);
        QCOMPARE(stack->currentIndex(), 2);
        auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs != nullptr);
        main_window_test::drainAsync();
    }
};

#include "test_main_window.moc"
