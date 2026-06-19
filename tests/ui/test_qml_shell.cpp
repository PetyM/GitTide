#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QAbstractItemModel>
#include <QSignalSpy>

#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/repoviewmodel.hpp"
#include "gittide/ui/thememanager.hpp"
#include "gittide/projectstore.hpp"
#include <git2.h>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>

using namespace gittide::ui;

namespace qml_shell_test {
inline std::filesystem::path make_dirty_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-qsh-" + std::to_string(::QRandomGenerator::global()->generate()));
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
}

class TestQmlShell : public QObject
{
    Q_OBJECT
private slots:
    void main_qml_loads_without_errors()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

        QCOMPARE(engine.rootObjects().size(), 1);
        QCOMPARE(engine.rootObjects().first()->objectName(), QStringLiteral("appWindow"));
    }

    void repo_tree_is_bound_to_the_model()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        RepoListModel repoModel;
        std::vector<gittide::RepoRef> repos;
        gittide::RepoRef r;
        r.alias = "gittide";
        r.path  = "/tmp/gittide";
        repos.push_back(r);
        repoModel.setRepos(repos);

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* tree = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("repoTree"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->property("model").value<QAbstractItemModel*>(), &repoModel);
    }

    void branch_bar_binds_to_view_model()
    {
        const auto dir = qml_shell_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(branchSpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* bar = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("branchBar"));
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->property("text").toString(), vm.currentBranch());

        std::filesystem::remove_all(dir);
    }

    void file_list_binds_to_changed_files_model()
    {
        const auto dir = qml_shell_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* list = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("fileList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("model").value<QAbstractItemModel*>(), vm.changedFiles());

        QObject* btn = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("commitButton"));
        QVERIFY(btn != nullptr);

        std::filesystem::remove_all(dir);
    }

    void diff_list_binds_to_diff_lines_model()
    {
        const auto dir = qml_shell_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QSignalSpy diffSpy(vm.diffLines(), &QAbstractItemModel::modelReset);
        vm.selectFile(QStringLiteral("a.txt"));
        QVERIFY(diffSpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* diff = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("diffList"));
        QVERIFY(diff != nullptr);
        QCOMPARE(diff->property("model").value<QAbstractItemModel*>(), vm.diffLines());

        std::filesystem::remove_all(dir);
    }
};

#include "test_qml_shell.moc"
