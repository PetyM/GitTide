#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
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

    // The dialog's defaults: depth 1 (the common ~/projects/<repo> layout), and
    // "keep this folder as a source" already ticked, since a folder the user
    // points at is usually somewhere repositories keep appearing.
    void dialog_opens_at_depth_one_with_keep_as_source_checked()
    {
        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        ThemeManager  mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme      theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        QObject* root = engine.rootObjects().first();

        QObject* dlg = root->findChild<QObject*>(QStringLiteral("addFromFolderDialog"));
        QVERIFY(dlg != nullptr);
        QVERIFY(QMetaObject::invokeMethod(dlg, "openDialog"));

        QObject* depth = root->findChild<QObject*>(QStringLiteral("addFromFolderDepth"));
        QVERIFY(depth != nullptr);
        QCOMPARE(depth->property("value").toInt(), 1);

        QObject* keep = root->findChild<QObject*>(QStringLiteral("addFromFolderKeepSource"));
        QVERIFY(keep != nullptr);
        QCOMPARE(keep->property("checked").toBool(), true);
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

    // Review finding: reposAdded's failures list carried addSource's error
    // unprefixed, so re-running the dialog on an already-registered folder
    // opened fetchErrorDialog reading "One repository failed to add:" even
    // though the repository itself added cleanly — only the source
    // registration failed. The wording must not claim a repository failed.
    void a_source_registration_failure_does_not_read_as_a_repository_failure()
    {
        const auto scanRoot = makeScanRoot();

        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));
        // Register the source once, up front (mirrors an earlier successful
        // run of the dialog on this same folder).
        controller.addRepos({}, {}, QString::fromStdString(scanRoot.generic_string()), 2);

        ThemeManager  mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme      theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        QObject* root        = engine.rootObjects().first();
        QObject* errorDialog = root->findChild<QObject*>(QStringLiteral("fetchErrorDialog"));
        QVERIFY(errorDialog != nullptr);
        QObject* header = errorDialog->findChild<QObject*>(QStringLiteral("fetchErrorHeader"));
        QVERIFY(header != nullptr);

        // Re-run addRepos on the same folder: the repo itself adds cleanly, but
        // the source is already registered.
        controller.addRepos({QString::fromStdString((scanRoot / "api").generic_string())}, {},
                            QString::fromStdString(scanRoot.generic_string()), 2);

        QVERIFY(errorDialog->property("visible").toBool());
        const QStringList failures = errorDialog->property("failures").toStringList();
        QCOMPARE(failures.size(), 1);
        QVERIFY(failures.at(0).startsWith(QStringLiteral("Source:")));

        // The header must not read as a repository failure.
        const QString headerText = header->property("text").toString();
        QVERIFY2(!headerText.contains(QStringLiteral("repository")) && !headerText.contains(QStringLiteral("repositories")),
                 qPrintable(QStringLiteral("header still reads as a repository failure: ") + headerText));
    }

    // Covers the "also fix" checklist-helper finding: setChecked / setAllChecked
    // / checkedCount / the already-added exclusion decide what lands in
    // `unchecked` — the paths seeded into a source's permanent ignore list — so
    // they need a real interaction test over a genuine scan result, not just
    // the existence checks the other tests in this file cover. Also pins that
    // both the ListView delegate (ItemDelegate.onClicked) and its AppCheckBox
    // (CheckBox.onClicked) independently toggle the same row.
    void checklist_toggling_select_all_and_already_added_exclusion()
    {
        const auto scanRoot = makeScanRoot(); // scanRoot/api
        const auto webRepo  = scanRoot / "web";
        std::filesystem::create_directories(webRepo);
        git_repository* raw = nullptr;
        git_repository_init(&raw, webRepo.generic_string().c_str(), 0);
        git_repository_free(raw);

        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));
        // "web" is already in the project before the folder is scanned.
        controller.addRepos({QString::fromStdString(webRepo.generic_string())}, {}, QString(), 2);
        QCOMPARE(controller.repos()->rowCount(), 1);

        ThemeManager  mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme      theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        QObject* root = engine.rootObjects().first();
        // The checklist ListView only instantiates delegates (itemAtIndex) once
        // its window is actually exposed — merely loading the component is not
        // enough to drive a polish/layout pass.
        auto* window = qobject_cast<QQuickWindow*>(root);
        QVERIFY(window != nullptr);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QObject* dialog = root->findChild<QObject*>(QStringLiteral("addFromFolderDialog"));
        QVERIFY(dialog != nullptr);

        QSignalSpy finishedSpy(&controller, &ProjectController::scanFinished);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openDialog"));
        dialog->setProperty("folder", QString::fromStdString(scanRoot.generic_string()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "startScan"));
        QVERIFY(finishedSpy.wait(5000));

        const QVariantList scanned = dialog->property("candidates").toList();
        QCOMPARE(scanned.size(), 2);
        int apiIndex = -1, webIndex = -1;
        for (int i = 0; i < scanned.size(); ++i)
        {
            const auto m = scanned.at(i).toMap();
            if (m.value("name").toString() == QStringLiteral("api"))
                apiIndex = i;
            if (m.value("name").toString() == QStringLiteral("web"))
                webIndex = i;
        }
        QVERIFY(apiIndex >= 0);
        QVERIFY(webIndex >= 0);

        // The already-added row starts unchecked and excluded from the count;
        // only the fresh one counts.
        QCOMPARE(dialog->property("checkedCount").toInt(), 1);
        QVERIFY(!scanned.at(webIndex).toMap().value("checked").toBool());

        // setChecked toggles a single row. QML JS functions are exposed on the
        // metaobject with QVariant-typed parameters, so Q_ARG must say QVariant
        // — not the JS-visible int/bool — or invokeMethod fails to match.
        QVERIFY(QMetaObject::invokeMethod(dialog, "setChecked", Q_ARG(QVariant, apiIndex), Q_ARG(QVariant, false)));
        QCOMPARE(dialog->property("checkedCount").toInt(), 0);

        // Select all / select none — the already-added row must never join in.
        QVERIFY(QMetaObject::invokeMethod(dialog, "setAllChecked", Q_ARG(QVariant, true)));
        QCOMPARE(dialog->property("checkedCount").toInt(), 1);
        QVERIFY(!dialog->property("candidates").toList().at(webIndex).toMap().value("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "setAllChecked", Q_ARG(QVariant, false)));
        QCOMPARE(dialog->property("checkedCount").toInt(), 0);

        // Real interaction: clicking the ListView delegate (ItemDelegate's own
        // onClicked) toggles the row. Re-fetched via itemAtIndex before each
        // click rather than cached: `candidates` is reassigned wholesale on
        // every toggle (QML doesn't track in-place array mutation), which the
        // ListView may answer by recreating its delegates rather than
        // rebinding the existing ones.
        QObject* list = root->findChild<QObject*>(QStringLiteral("addFromFolderList"));
        QVERIFY(list != nullptr);
        QTest::qWait(50); // let the now-visible ListView finish instantiating its delegates

        auto delegateAt = [&](int index) -> QQuickItem*
        {
            QQuickItem* item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex", Qt::DirectConnection, Q_RETURN_ARG(QQuickItem*, item),
                                      Q_ARG(int, index));
            return item;
        };

        QQuickItem* apiDelegate = delegateAt(apiIndex);
        QVERIFY2(apiDelegate != nullptr, "api delegate not created");
        QVERIFY(QMetaObject::invokeMethod(apiDelegate, "click"));
        QCOMPARE(dialog->property("checkedCount").toInt(), 1);

        // ...and clicking its AppCheckBox (a separate onClicked handler) does too.
        QTest::qWait(50);
        apiDelegate = delegateAt(apiIndex);
        QVERIFY2(apiDelegate != nullptr, "api delegate not re-created after the model reset");
        QObject* checkbox = apiDelegate->findChild<QObject*>(QStringLiteral("addFromFolderRowCheckbox"));
        QVERIFY2(checkbox != nullptr, "row checkbox not found");
        QVERIFY(QMetaObject::invokeMethod(checkbox, "click"));
        QCOMPARE(dialog->property("checkedCount").toInt(), 0);

        // The already-added delegate is interactively disabled.
        QTest::qWait(50);
        QQuickItem* webDelegate = delegateAt(webIndex);
        QVERIFY2(webDelegate != nullptr, "web delegate not created");
        QCOMPARE(webDelegate->property("enabled").toBool(), false);

        // Finally: an already-added row can never end up in "chosen", even
        // though it is still present (unchecked) in the checklist — drive the
        // real confirm button and check what actually reached the store.
        QVERIFY(QMetaObject::invokeMethod(dialog, "setChecked", Q_ARG(QVariant, apiIndex), Q_ARG(QVariant, true)));
        QCOMPARE(dialog->property("checkedCount").toInt(), 1);

        QObject* confirmButton = root->findChild<QObject*>(QStringLiteral("addFromFolderConfirm"));
        QVERIFY(confirmButton != nullptr);
        QSignalSpy addedSpy(&controller, &ProjectController::reposAdded);
        QVERIFY(QMetaObject::invokeMethod(confirmButton, "click"));
        if (addedSpy.isEmpty())
            QVERIFY(addedSpy.wait(2000));

        QCOMPARE(addedSpy.at(0).at(0).toInt(), 1); // only "api" — "web" never resubmitted
        QCOMPARE(addedSpy.at(0).at(1).toStringList().size(), 0);
        // Assert against the store, not the model's top-level rowCount: with
        // keep-as-source on by default the confirm also registers the folder, so
        // the repos are grouped under it and the roots count groups, not repos.
        QCOMPARE(static_cast<int>(store.projects()[0].repos.size()), 2); // web (pre-existing) + api (new)
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
