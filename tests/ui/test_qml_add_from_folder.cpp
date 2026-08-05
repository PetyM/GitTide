#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>
#include <filesystem>
#include <git2.h>

#include "gittide/projectstore.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

// The add-from-folder dialog must exist in the shell and be reachable from the
// empty state, so the bulk flow is not left orphaned behind a menu item.
class TestQmlAddFromFolder : public QObject
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

    void dialog_and_cta_exist_in_the_shell()
    {
        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        // Mirrors test_qml_shell.cpp's main_qml_loads_without_errors setup: Main.qml
        // reads several context properties (theme, repoModel, ...) beyond
        // projectController, and any missing one makes the load warn.
        ThemeManager    mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme        theme(&mgr);
        RepoListModel   repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        QObject* root = engine.rootObjects().first();
        QVERIFY(root->findChild<QObject*>(QStringLiteral("addFromFolderDialog")) != nullptr);
        QVERIFY(root->findChild<QObject*>(QStringLiteral("addFromFolderCta")) != nullptr);
    }

    // Covers review finding 1: a scan started in one dialog session must never
    // populate a later, fresh session's checklist. Closing (openDialog() reset)
    // and reopening before the in-flight scanFolder() call resolves used to
    // leave the stale response landing anyway — the old `!dialog.visible`
    // guard only caught the fully-closed case, not "closed then reopened".
    void reopening_the_dialog_ignores_a_stale_in_flight_scan_response()
    {
        const auto scanRoot = makeScanRoot();

        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        ThemeManager    mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme        theme(&mgr);
        RepoListModel   repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        QObject* root   = engine.rootObjects().first();
        QObject* dialog = root->findChild<QObject*>(QStringLiteral("addFromFolderDialog"));
        QVERIFY(dialog != nullptr);

        QSignalSpy finishedSpy(&controller, &ProjectController::scanFinished);

        QVERIFY(QMetaObject::invokeMethod(dialog, "openDialog"));
        dialog->setProperty("folder", QString::fromStdString(scanRoot.generic_string()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "startScan"));
        QCOMPARE(dialog->property("scanning").toBool(), true);

        // Simulate the user closing and reopening before the scan resolves —
        // openDialog() resets to a fresh session while the old request is
        // still outstanding.
        QVERIFY(QMetaObject::invokeMethod(dialog, "openDialog"));
        QCOMPARE(dialog->property("folder").toString(), QString());
        QCOMPARE(dialog->property("scanning").toBool(), false);

        // The abandoned scan still resolves for real...
        QVERIFY(finishedSpy.wait(5000));
        // ...but must not have populated the fresh (reopened) session.
        QCOMPARE(dialog->property("candidates").toList().size(), 0);
    }

    // Covers the other half of finding 1: startScan() must not fire a second
    // request while one is already outstanding — the depth SpinBox and
    // "Choose…" button are disabled while scanning specifically so this path
    // is never reachable through the UI, but the guard is what makes the
    // token check in the response handlers sound (see AddFromFolderDialog.qml).
    void starting_a_scan_while_one_is_in_flight_is_a_noop()
    {
        const auto scanRoot = makeScanRoot();

        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        ThemeManager    mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme        theme(&mgr);
        RepoListModel   repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        QObject* root   = engine.rootObjects().first();
        QObject* dialog = root->findChild<QObject*>(QStringLiteral("addFromFolderDialog"));
        QVERIFY(dialog != nullptr);

        QVERIFY(QMetaObject::invokeMethod(dialog, "openDialog"));
        dialog->setProperty("folder", QString::fromStdString(scanRoot.generic_string()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "startScan"));
        QCOMPARE(dialog->property("scanning").toBool(), true);
        const int tokenAfterFirstScan = dialog->property("scanToken").toInt();

        // A second call while still scanning (what a depth change or a repeat
        // "Choose…" click would trigger) must not start another request.
        QVERIFY(QMetaObject::invokeMethod(dialog, "startScan"));
        QCOMPARE(dialog->property("scanToken").toInt(), tokenAfterFirstScan);
    }

private:
    // A folder containing one child git repo — enough to exercise scanFolder()
    // for real and get a genuine scanFinished response.
    std::filesystem::path makeScanRoot()
    {
        const auto root = std::filesystem::temp_directory_path() /
                          ("gittide-qml-aff-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        const auto child = root / "api";
        std::filesystem::create_directories(child);
        git_repository* raw = nullptr;
        git_repository_init(&raw, child.generic_string().c_str(), 0);
        git_repository_free(raw);
        m_scanRoots.push_back(root);
        return root;
    }

    std::vector<std::filesystem::path> m_scanRoots;
};

#include "test_qml_add_from_folder.moc"
