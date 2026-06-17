#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "gittide/projectstore.hpp"
#include "gittide/ui/MainWindow.hpp"
#include "gittide/ui/WindowManager.hpp"

using gittide::Project;
using gittide::ui::MainWindow;
using gittide::ui::WindowManager;

class TestWindowManager : public QObject {
    Q_OBJECT
private slots:
    void open_creates_a_window_bound_to_project() {
        WindowManager wm;
        wm.store()->projects().push_back(Project{.id = "id-a", .name = "Work"});

        MainWindow* w = wm.openProject(QStringLiteral("id-a"));
        QVERIFY(w != nullptr);
        QCOMPARE(w->currentProjectId(), QStringLiteral("id-a"));
        QCOMPARE(wm.windowCount(), 1);
    }

    void dedup_raises_existing_instead_of_opening_second() {
        WindowManager wm;
        wm.setDeduplicate(true);
        wm.store()->projects().push_back(Project{.id = "id-a", .name = "Work"});

        MainWindow* first = wm.openProject(QStringLiteral("id-a"));
        MainWindow* second = wm.openProject(QStringLiteral("id-a"));  // dedup -> same
        QCOMPARE(first, second);
        QCOMPARE(wm.windowCount(), 1);
    }

    void force_new_opens_second_window_even_with_dedup() {
        WindowManager wm;
        wm.setDeduplicate(true);
        wm.store()->projects().push_back(Project{.id = "id-a", .name = "Work"});

        wm.openProject(QStringLiteral("id-a"));
        wm.openProject(QStringLiteral("id-a"), /*forceNew=*/true);
        QCOMPARE(wm.windowCount(), 2);
    }

    void session_round_trips_open_windows() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        WindowManager wm(dir.path());
        wm.store()->projects().push_back(Project{.id = "id-a", .name = "Work"});
        wm.store()->projects().push_back(Project{.id = "id-b", .name = "Home"});
        wm.openProject(QStringLiteral("id-a"));
        wm.openProject(QStringLiteral("id-b"), /*forceNew=*/true);
        wm.saveSession();

        WindowManager restored(dir.path());
        restored.store()->projects().push_back(Project{.id = "id-a", .name = "Work"});
        restored.store()->projects().push_back(Project{.id = "id-b", .name = "Home"});
        restored.restoreSession();
        QCOMPARE(restored.windowCount(), 2);
    }
};

#include "test_window_manager.moc"
