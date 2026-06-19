#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModel>
#include <QRandomGenerator>

#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/ui/repoviewmodel.hpp"

using gittide::ui::RepoViewModel;

namespace repo_view_model_test {

// Self-contained dirty repo: one committed file "a.txt", then a worktree edit
// (mirrors make_dirty_repo() in test_repo_controller.cpp).
inline std::filesystem::path make_dirty_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-rvm-" + std::to_string(::QRandomGenerator::global()->generate()));
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

} // namespace repo_view_model_test

class TestRepoViewModel : public QObject
{
    Q_OBJECT
private slots:
    void open_populates_changed_files_and_branch()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);

        vm.open(QString::fromStdString(dir.generic_string()));

        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);
        QVERIFY(branchSpy.wait(3000));
        QVERIFY(!vm.currentBranch().isEmpty());

        std::filesystem::remove_all(dir);
    }

    void select_file_populates_diff()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        QSignalSpy diffSpy(vm.diffLines(), &QAbstractItemModel::modelReset);
        vm.selectFile(QStringLiteral("a.txt"));
        QVERIFY(diffSpy.wait(3000));
        QVERIFY(vm.diffLines()->rowCount(QModelIndex()) > 0);
        QCOMPARE(vm.activeFile(), QStringLiteral("a.txt"));

        std::filesystem::remove_all(dir);
    }

    void commit_succeeds_and_clears_changes()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);

        QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
        vm.commit(QStringLiteral("test commit"), QString());
        QVERIFY(committedSpy.wait(3000));

        // commitSelection refreshes status after committing → worktree clean.
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 0);

        std::filesystem::remove_all(dir);
    }
};

#include "test_repo_view_model.moc"
