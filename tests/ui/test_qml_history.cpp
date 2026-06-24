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
};

#include "test_qml_history.moc"
