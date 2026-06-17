#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTabWidget>
#include <filesystem>

#include <git2.h>

#include "gitgui/ProjectStore.hpp"
#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/ChangesView.hpp"

using gitgui::Project;
using gitgui::ProjectStore;
using gitgui::RepoRef;
using gitgui::ui::MainWindow;

namespace main_window_test {
std::filesystem::path make_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-mw-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
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
    }

    void open_in_new_window_propagates_upward() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));
        QSignalSpy spy(&win, &MainWindow::openInNewWindowRequested);
        auto* sidebar = win.findChild<gitgui::ui::ProjectSidebar*>();
        QVERIFY(sidebar != nullptr);
        sidebar->requestOpenInNewWindow();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
    }

    void changes_tab_is_a_changes_view() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        MainWindow win(&store);
        QVERIFY(win.findChild<gitgui::ui::ChangesView*>() != nullptr);
    }

    void selecting_repo_opens_it_in_controller() {
        const auto dir = main_window_test::make_repo();
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = dir.generic_string(), .alias = "r"} }});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* sidebar = win.findChild<gitgui::ui::ProjectSidebar*>();
        QSignalSpy spy(&win, &MainWindow::repoOpened);
        emit sidebar->repoSelected(QString::fromStdString(dir.generic_string()));

        QCOMPARE(spy.count(), 1);
        std::filesystem::remove_all(dir);
    }
};

#include "test_main_window.moc"
