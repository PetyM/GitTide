#include <QtTest>
#include <QAbstractItemModel>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QQmlApplicationEngine>

#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/ui/historylistmodel.hpp"
#include "gittide/ui/repoviewmodel.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"
#include "gittide/ui/repolistmodel.hpp"

using gittide::ui::HistoryListModel;
using gittide::ui::RepoViewModel;
using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;
using gittide::ui::RepoListModel;
using gittide::ui::installQmlContext;

namespace qml_graph_test {

/// Build a repo with three commits reachable from two branches:
///   base  ── master-work   (HEAD on master)
///         └─ feature-work  (on feature)
/// logAllRefs covers all three; HEAD-only log covers two (base + master-work).
inline std::filesystem::path make_branched_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path()
               / ("gittide-qgraph-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);

    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    // Helper: stage a.txt and commit to `ref` with one optional parent.
    auto makeCommit = [&](const char* ref, const char* content, const char* msg,
                          git_oid* out_oid, git_commit* parent = nullptr)
    {
        std::ofstream(dir / "a.txt") << content << "\n";
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
        if (parent)
            git_commit_create_v(out_oid, raw, ref, sig, sig, nullptr, msg, tree, 1, parent);
        else
            git_commit_create_v(out_oid, raw, ref, sig, sig, nullptr, msg, tree, 0);
        git_signature_free(sig);
        git_tree_free(tree);
        git_index_free(idx);
    };

    // 1. Root commit on HEAD (master).
    git_oid base_oid;
    makeCommit("HEAD", "base", "base", &base_oid);

    // 2. Create "feature" branch at base.
    git_commit* base_commit = nullptr;
    git_commit_lookup(&base_commit, raw, &base_oid);
    git_reference* feat_ref = nullptr;
    git_branch_create(&feat_ref, raw, "feature", base_commit, 0);
    git_reference_free(feat_ref);

    // 3. Commit on master (HEAD stays on master).
    git_oid master_oid;
    makeCommit("HEAD", "master-work", "master-work", &master_oid, base_commit);

    // 4. Commit on feature.
    git_oid feat_oid;
    makeCommit("refs/heads/feature", "feature-work", "feature-work", &feat_oid, base_commit);

    git_commit_free(base_commit);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

} // namespace qml_graph_test

class TestQmlGraph : public QObject
{
    Q_OBJECT
private slots:

    void graph_property_is_non_null()
    {
        RepoViewModel vm;
        QVERIFY(vm.graph() != nullptr);
    }

    void graph_covers_all_refs_not_just_head()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        RepoViewModel vm;

        // Wait for the initial open to load history (HEAD-only walk).
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        // history (HEAD-only) should see 2 commits: master-work + base.
        QCOMPARE(vm.history()->rowCount(QModelIndex()), 2);

        // Now trigger the all-refs walk.
        QSignalSpy graphSpy(vm.graph(), &QAbstractItemModel::modelReset);
        vm.refreshGraph();
        QVERIFY(graphSpy.wait(3000));

        // graph must include commits from both branches: base + master-work + feature-work >= 3.
        QVERIFY2(vm.graph()->rowCount(QModelIndex()) >= 3,
                 qPrintable(QStringLiteral("expected >= 3 rows, got %1")
                                .arg(vm.graph()->rowCount(QModelIndex()))));

        std::filesystem::remove_all(dir);
    }

    void graph_model_is_empty_before_refresh()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        RepoViewModel vm;
        // Before any open/refresh the graph model starts empty.
        QCOMPARE(vm.graph()->rowCount(QModelIndex()), 0);

        std::filesystem::remove_all(dir);
    }

    void graph_model_clears_on_close()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        QSignalSpy graphSpy(vm.graph(), &QAbstractItemModel::modelReset);
        vm.refreshGraph();
        QVERIFY(graphSpy.wait(3000));
        QVERIFY(vm.graph()->rowCount(QModelIndex()) >= 3);

        // close() must reset the graph model.
        vm.close();
        QCOMPARE(vm.graph()->rowCount(QModelIndex()), 0);

        std::filesystem::remove_all(dir);
    }

    void graph_model_exposes_local_branch_name_for_tip_commits()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        RepoViewModel vm;
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(historySpy.wait(3000));

        QSignalSpy graphSpy(vm.graph(), &QAbstractItemModel::modelReset);
        vm.refreshGraph();
        QVERIFY(graphSpy.wait(3000));

        // At least one row in the graph must have a non-empty LocalBranchNameRole —
        // the "master" tip and the "feature" tip.
        const int rows = vm.graph()->rowCount(QModelIndex());
        QVERIFY(rows >= 3);

        bool foundBranchTip = false;
        for (int r = 0; r < rows; ++r)
        {
            const QModelIndex idx = vm.graph()->index(r, 0);
            const QString branchName =
                vm.graph()->data(idx, HistoryListModel::LocalBranchNameRole).toString();
            if (!branchName.isEmpty())
            {
                foundBranchTip = true;
                break;
            }
        }
        QVERIFY2(foundBranchTip,
                 "No graph row has a non-empty LocalBranchNameRole; "
                 "setLocalBranchTips was not called on the graph model");

        std::filesystem::remove_all(dir);
    }

    void graph_tab_exists_and_selection_drives_commit_detail()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        // Open repo and wait for history to load.
        {
            QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historySpy.wait(3000));
        }

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        // Main.qml's Component.onCompleted calls openFirstRepo() → repoVm.close().
        // Re-open and wait for a real modelReset so history is populated.
        {
            QSignalSpy historyReady2(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historyReady2.wait(3000));
        }

        QObject* root = engine.rootObjects().first();

        // Assert graphTabBody exists (fails until GraphPane.qml + WorkingPane tab 3 are added).
        QObject* graphBody = root->findChild<QObject*>(QStringLiteral("graphTabBody"));
        QVERIFY(graphBody != nullptr);

        // Switch to Graph tab (index 2).
        // WorkingPane's onCurrentIndexChanged calls repoVm.refreshGraph() automatically.
        QSignalSpy graphSpy(vm.graph(), &QAbstractItemModel::modelReset);
        QObject* tabBar = root->findChild<QObject*>(QStringLiteral("changesTabBar"));
        QVERIFY(tabBar != nullptr);
        tabBar->setProperty("currentIndex", 2);
        QVERIFY(graphSpy.wait(3000));
        QVERIFY(vm.graph()->rowCount(QModelIndex()) >= 1);

        // Select row 0 via selectGraphCommitAtRow — selectedCommit becomes non-empty.
        vm.selectGraphCommitAtRow(0);
        QVERIFY(!vm.selectedCommit().isEmpty());

        std::filesystem::remove_all(dir);
    }

    void graph_tab_has_no_inline_commit_detail_panel()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        {
            QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historySpy.wait(3000));
        }

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        {
            QSignalSpy historyReady2(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historyReady2.wait(3000));
        }

        QObject* root = engine.rootObjects().first();

        // Switch to Graph tab (index 2).
        QSignalSpy graphSpy(vm.graph(), &QAbstractItemModel::modelReset);
        QObject* tabBar = root->findChild<QObject*>(QStringLiteral("changesTabBar"));
        QVERIFY(tabBar != nullptr);
        tabBar->setProperty("currentIndex", 2);
        QVERIFY(graphSpy.wait(3000));

        // No inline commit-detail panel in the Graph tab anymore.
        QVERIFY(root->findChild<QObject*>(QStringLiteral("graphCommitDetail")) == nullptr);

        // The commit list fills the whole pane width — find its ListView and
        // compare its width against the tab body's width (allow a 2px margin
        // for the pane's own layout spacing/borders).
        QObject* graphList = root->findChild<QObject*>(QStringLiteral("graphList"));
        QVERIFY(graphList != nullptr);
        QObject* graphTabBody = root->findChild<QObject*>(QStringLiteral("graphTabBody"));
        QVERIFY(graphTabBody != nullptr);
        const qreal listWidth = graphList->property("width").toReal();
        const qreal paneWidth = graphTabBody->property("width").toReal();
        QVERIFY2(listWidth > paneWidth - 2.0,
                 qPrintable(QStringLiteral("expected graphList to fill the pane: list=%1 pane=%2")
                                .arg(listWidth).arg(paneWidth)));

        std::filesystem::remove_all(dir);
    }
};

#include "test_qml_graph.moc"
