#include <QtTest>
#include <QAbstractItemModel>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QRandomGenerator>
#include <memory>
#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/graph.hpp"
#include "gittide/ui/historylistmodel.hpp"
#include "gittide/ui/graphcolumn.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/repoviewmodel.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"
#include "gittide/ui/repolistmodel.hpp"

using namespace gittide::ui;

namespace qml_history_test {
inline std::filesystem::path make_dirty_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-qhist-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    {
        std::ofstream(dir / "a.txt") << "one\n";
    }
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
    git_repository_free(raw);
    {
        std::ofstream(dir / "a.txt") << "one\ntwo\n";
    }
    git_libgit2_shutdown();
    return dir;
}
inline std::filesystem::path make_empty_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-qhe-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}
/// Build a linear repo with N commits, each adding a distinct file f0..fN-1.
/// reorderableRunLength will be N-1 (all commits except the root are reorderable).
inline std::filesystem::path makeLinearRepo(int n)
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path()
               / ("gittide-qhlin-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr; git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    for (int i = 0; i < n; ++i)
    {
        const std::string name = "f" + std::to_string(i) + ".txt";
        std::ofstream(dir / name) << "x\n";
        git_index* idx = nullptr; git_repository_index(&idx, raw);
        git_index_add_bypath(idx, name.c_str()); git_index_write(idx);
        git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
        git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
        git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
        git_commit* parent = nullptr; git_oid parent_oid;
        git_commit* parents[1] = { nullptr }; size_t pc = 0;
        if (git_reference_name_to_id(&parent_oid, raw, "HEAD") == 0
            && git_commit_lookup(&parent, raw, &parent_oid) == 0)
        {
            parents[0] = parent; pc = 1;
        }
        git_oid commit_oid;
        const std::string msg = "c" + std::to_string(i) + "\n";
        git_commit_create(&commit_oid, raw, "HEAD", sig, sig, nullptr, msg.c_str(), tree, pc, parents);
        if (parent) git_commit_free(parent);
        git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    }
    git_repository_free(raw); git_libgit2_shutdown();
    return dir;
}
} // namespace qml_history_test

namespace {
gittide::GraphLayout twoRowLayout()
{
    // Row 0: child (HEAD) at lane 0 with one out-edge to its parent (row 1).
    // Row 1: parent at lane 0, initial commit (no out-edges, line from above).
    gittide::GraphRow head;
    head.commit.oid     = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    head.commit.summary = "second";
    head.commit.author  = "Ada";
    head.commit.time    = 0;
    head.commit.lane    = 0;
    head.lineFromAbove  = false;
    head.outEdges       = {gittide::GraphEdge{0, 0}};

    gittide::GraphRow base;
    base.commit.oid     = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    base.commit.summary = "first";
    base.commit.author  = "Ada";
    base.commit.time    = 0;
    base.commit.lane    = 0;
    base.lineFromAbove  = true;

    gittide::GraphLayout layout;
    layout.rows      = {head, base};
    layout.laneCount = 1;
    return layout;
}
}

class TestQmlHistory : public QObject
{
    Q_OBJECT
private slots:
    void model_exposes_history_rows_via_roles()
    {
        HistoryListModel model;
        model.setLayout(twoRowLayout(), QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

        QCOMPARE(model.rowCount(QModelIndex()), 2);
        QCOMPARE(model.laneCount(), 1);

        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.data(top, HistoryListModel::SummaryRole).toString(), QStringLiteral("second"));
        QCOMPARE(model.data(top, HistoryListModel::AuthorRole).toString(), QStringLiteral("Ada"));
        QCOMPARE(model.data(top, HistoryListModel::ShortOidRole).toString(), QStringLiteral("aaaaaaa"));
        QCOMPARE(model.data(top, HistoryListModel::IsHeadRole).toBool(), true);
        QVERIFY(model.data(top, HistoryListModel::GraphRole).canConvert<gittide::GraphRow>());

        const QModelIndex bottom = model.index(1, 0);
        QCOMPARE(model.data(bottom, HistoryListModel::IsHeadRole).toBool(), false);

        // QML role names are present and spelled as the delegates expect.
        const auto names = model.roleNames();
        QCOMPARE(names.value(HistoryListModel::SummaryRole), QByteArrayLiteral("summary"));
        QCOMPARE(names.value(HistoryListModel::GraphRole), QByteArrayLiteral("graphRow"));
        QCOMPARE(names.value(HistoryListModel::IsHeadRole), QByteArrayLiteral("isHead"));
    }

    void history_model_populates_after_open()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        QVERIFY(vm.history()->rowCount(QModelIndex()) >= 1);
        // Top row is HEAD (the "init" commit) — IsHeadRole true.
        const QModelIndex top = vm.history()->index(0, 0);
        QCOMPARE(vm.history()->data(top, HistoryListModel::IsHeadRole).toBool(), true);

        std::filesystem::remove_all(dir);
    }

    void history_model_is_empty_for_unborn_repo()
    {
        const auto dir = qml_history_test::make_empty_repo();
        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));
        QCOMPARE(vm.history()->rowCount(QModelIndex()), 0);
        std::filesystem::remove_all(dir);
    }

    void history_model_refreshes_on_reopen()
    {
        const auto repoA = qml_history_test::make_dirty_repo(); // 1 commit
        const auto repoB = qml_history_test::make_empty_repo(); // 0 commits
        RepoViewModel vm;
        {
            QSignalSpy s(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(repoA.generic_string()));
            QVERIFY(s.wait(3000));
        }
        QVERIFY(vm.history()->rowCount(QModelIndex()) >= 1);
        {
            QSignalSpy s(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(repoB.generic_string()));
            QVERIFY(s.wait(3000));
        }
        QCOMPARE(vm.history()->rowCount(QModelIndex()), 0);
        std::filesystem::remove_all(repoA);
        std::filesystem::remove_all(repoB);
    }

    void graph_column_unpacks_row_and_sizes_to_lane_count()
    {
        GraphColumn item;

        gittide::GraphRow gr;
        gr.commit.oid  = "cccccccccccccccccccccccccccccccccccccccc";
        gr.commit.lane = 1;
        item.setGraphRow(QVariant::fromValue(gr));
        item.setLaneCount(3);

        QCOMPARE(item.laneCount(), 3);
        // implicitWidth tracks lane count so the ListView can reserve a fixed gutter.
        QCOMPARE(item.implicitWidth(), qreal(3 * GraphColumn::kLaneWidth));

        // The QML type is registered under the GitTide module.
        registerQmlTypes();
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData("import GitTide 1.0\nGraphColumn { laneCount: 2 }", QUrl());
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QCOMPARE(obj->property("laneCount").toInt(), 2);
    }

    void selecting_a_commit_loads_its_files_and_diff()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        // Select the HEAD commit (row 0) by its oid.
        const QString oid = vm.history()->data(vm.history()->index(0, 0), HistoryListModel::OidRole).toString();
        QSignalSpy filesSpy(vm.commitFiles(), &QAbstractItemModel::modelReset);
        vm.selectCommit(oid);
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.selectedCommit(), oid);
        QVERIFY(vm.commitFiles()->rowCount(QModelIndex()) >= 1);

        // Select the first file → its read-only diff loads.
        const QString path = vm.commitFiles()->pathAt(0);
        QSignalSpy diffSpy(vm.commitDiff(), &QAbstractItemModel::modelReset);
        vm.selectCommitFile(path);
        QVERIFY(diffSpy.wait(3000));
        QCOMPARE(vm.activeCommitFile(), path);
        QVERIFY(vm.commitDiff()->rowCount(QModelIndex()) >= 1);

        std::filesystem::remove_all(dir);
    }

    void selecting_a_commit_auto_selects_first_file_and_loads_diff()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        const QString oid = vm.history()->data(vm.history()->index(0, 0), HistoryListModel::OidRole).toString();
        QSignalSpy diffSpy(vm.commitDiff(), &QAbstractItemModel::modelReset);
        vm.selectCommit(oid);

        // Selecting a commit alone must auto-select its first file and load that
        // file's diff — no explicit selectCommitFile() call.
        QVERIFY(diffSpy.wait(3000));
        QVERIFY(vm.commitFiles()->rowCount(QModelIndex()) >= 1);
        QCOMPARE(vm.activeCommitFile(), vm.commitFiles()->pathAt(0));
        QVERIFY(vm.commitDiff()->rowCount(QModelIndex()) >= 1);

        std::filesystem::remove_all(dir);
    }

    void history_list_binds_to_history_model()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* list = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("model").value<QAbstractItemModel*>(), vm.history());

        std::filesystem::remove_all(dir);
    }

    void checkout_commit_detaches_head_at_that_commit()
    {
        const auto dir = qml_history_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        const QString oid = vm.history()->data(vm.history()->index(0, 0), HistoryListModel::OidRole).toString();
        QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);
        vm.checkoutCommit(oid);
        QVERIFY(branchSpy.wait(3000));
        // Detached HEAD label is "detached @ <short>".
        QVERIFY(vm.currentBranch().startsWith(QStringLiteral("detached @ ")));

        std::filesystem::remove_all(dir);
    }

    void reword_dialog_loads_and_exposes_summary()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/RewordDialog.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QVERIFY(obj->property("summary").isValid());
    }

    void commit_detail_has_range_header_item()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/CommitDetail.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QVERIFY(obj->findChild<QObject *>("rangeHeaderLabel") != nullptr);
    }

    void drop_zone_resolves_three_bands()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* pane = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyPane"));
        QVERIFY(pane != nullptr);

        QString zone;
        const qreal h = 48.0;
        // Top third → "above".
        QMetaObject::invokeMethod(pane, "dropZoneAt", Q_RETURN_ARG(QString, zone),
                                  Q_ARG(QVariant, 4.0), Q_ARG(QVariant, h));
        QCOMPARE(zone, QStringLiteral("above"));
        // Middle third → "squash".
        QMetaObject::invokeMethod(pane, "dropZoneAt", Q_RETURN_ARG(QString, zone),
                                  Q_ARG(QVariant, 24.0), Q_ARG(QVariant, h));
        QCOMPARE(zone, QStringLiteral("squash"));
        // Bottom third → "below".
        QMetaObject::invokeMethod(pane, "dropZoneAt", Q_RETURN_ARG(QString, zone),
                                  Q_ARG(QVariant, 44.0), Q_ARG(QVariant, h));
        QCOMPARE(zone, QStringLiteral("below"));
    }

    void perform_drop_squash_routes_to_squash_commit_into()
    {
        const auto dir = qml_history_test::makeLinearRepo(3); // run length 2

        ThemeManager mgr; mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        // Main.qml's Component.onCompleted calls openFirstRepo() → repoVm.close()
        // (no projectController in test). m_lastLayout is now empty but
        // m_reorderableRunLength is stale (still 2). Re-open and wait for a real
        // modelReset so m_lastLayout is repopulated before squashCommitInto runs.
        {
            QSignalSpy historyReady2(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historyReady2.wait(3000));
        }
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);
        QObject* pane = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyPane"));
        QVERIFY(pane != nullptr);

        // Squash row 0 into row 1 → engine pauses for the combined message.
        QMetaObject::invokeMethod(pane, "performDrop",
                                  Q_ARG(QVariant, 0), Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("squash")));
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("rebasePauseReason").toString(), QStringLiteral("message"), 5000);

        std::filesystem::remove_all(dir);
    }

    void perform_drop_reorder_opens_confirm_dialog()
    {
        const auto dir = qml_history_test::makeLinearRepo(3);

        ThemeManager mgr; mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        // Main.qml's Component.onCompleted calls openFirstRepo() → repoVm.close()
        // (no projectController in test). m_lastLayout is now empty but
        // m_reorderableRunLength is stale (still 2). Re-open and wait for a real
        // modelReset so m_lastLayout is repopulated before performDrop runs.
        {
            QSignalSpy historyReady2(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historyReady2.wait(3000));
        }
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);
        QObject* root = engine.rootObjects().first();
        QObject* pane = root->findChild<QObject*>(QStringLiteral("historyPane"));
        QVERIFY(pane != nullptr);

        // Reorder (below) row 0 onto row 1 → the confirm dialog opens (no engine drive yet).
        QMetaObject::invokeMethod(pane, "performDrop",
                                  Q_ARG(QVariant, 0), Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("below")));
        QObject* dlg = root->findChild<QObject*>(QStringLiteral("reorderConfirmDialog"));
        QVERIFY(dlg != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(dlg->property("visible").toBool(), 3000);
        // Reorder must NOT have driven the engine directly.
        QCOMPARE(vm.property("rebaseInProgress").toBool(), false);

        std::filesystem::remove_all(dir);
    }

    // Regression guard for the drag fix (Plan 25, Task 6):
    // - The grab-stealing MouseArea must be gone (it blocked DragHandler from ever winning).
    // - A TapHandler must replace it (cooperates with DragHandler via shared grabs).
    // - The visual reorderGrip Label must be removed from the delegate.
    //
    // ListView delegate items are not instantiated in the offscreen harness (the
    // virtualized view never gets a real render frame, so no delegate QObjects appear
    // in the child tree). Structural assertions therefore use the compiled QRC source
    // rather than findChild. performDrop routing is covered by the two tests above.
    void history_delegate_has_tap_handler_not_mouse_area()
    {
        // Read the compiled QML source from the QRC bundle.
        QFile historyPaneSrc(QStringLiteral(":/qml/HistoryPane.qml"));
        QVERIFY(historyPaneSrc.open(QIODevice::ReadOnly));
        const QByteArray src = historyPaneSrc.readAll();

        // (1) The reorderGrip Label must be removed from the delegate.
        QVERIFY2(!src.contains("\"reorderGrip\""),
                 "reorderGrip objectName must not exist in HistoryPane.qml");

        // (2) The grab-stealing MouseArea element must be gone — it took an exclusive
        //     pointer grab on press, preventing DragHandler from ever activating.
        QVERIFY2(!src.contains("MouseArea {"),
                 "HistoryPane.qml must not contain a MouseArea element; "
                 "it steals pointer grabs from DragHandler");

        // (3) A TapHandler must replace it — TapHandlers cooperate with DragHandler
        //     via shared grabs, so the 250 ms hold can arm the drag.
        QVERIFY2(src.contains("TapHandler {"),
                 "HistoryPane.qml must contain a TapHandler element");
        QVERIFY2(src.contains("\"leftTapHandler\""),
                 "HistoryPane.qml must have objectName: \"leftTapHandler\" "
                 "on the selection TapHandler (regression guard)");
    }
};

#include "test_qml_history.moc"
