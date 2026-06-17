#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTabWidget>

#include "gitgui/ProjectStore.hpp"
#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"

using gitgui::Project;
using gitgui::ProjectStore;
using gitgui::RepoRef;
using gitgui::ui::MainWindow;

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
        QCOMPARE(tabs->count(), 3);  // Changes / History / Dashboard
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
};

#include "test_main_window.moc"
