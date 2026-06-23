#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModel>
#include <QClipboard>
#include <QGuiApplication>
#include <QRandomGenerator>

#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/ui/repoviewmodel.hpp"
#include "support/temprepo.hpp"

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

// Self-contained repo whose worktree adds two new lines to a one-line committed
// file, so the diff carries two independently-checkable added lines.
inline std::filesystem::path make_multiline_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-rvm-ml-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    {
        std::ofstream(dir / "a.txt") << "L1\n";
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
        std::ofstream(dir / "a.txt") << "L1\nL2\nL3\n";
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

    // A submodule's working directory is a real git repo; selecting it in the
    // tree opens it as a first-class repo (its own Changes/History).
    void open_submodule_workdir_as_first_class_repo()
    {
        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("child");
        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("parent");
        parent.addSubmodule("libchild", child.path());
        parent.commitAll("add submodule");

        const auto subWorkdir = parent.path() / "libchild";

        RepoViewModel vm;
        QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);
        vm.open(QString::fromStdString(subWorkdir.generic_string()));
        QVERIFY(branchSpy.wait(3000));

        QVERIFY(vm.repoOpen());
        QCOMPARE(vm.repoPath(), QString::fromStdString(subWorkdir.generic_string()));
        QVERIFY(!vm.currentBranch().isEmpty());
    }

    void close_clears_repo_state()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);
        QVERIFY(vm.repoOpen());
        QCOMPARE(vm.repoPath(), QString::fromStdString(dir.generic_string()));

        // Switching to a project with no repo must wipe the working view.
        QSignalSpy changedSpy(&vm, &RepoViewModel::changed);
        vm.close();

        QVERIFY(!vm.repoOpen());
        QVERIFY(changedSpy.count() >= 1);
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 0);
        QCOMPARE(vm.diffLines()->rowCount(QModelIndex()), 0);
        QVERIFY(vm.currentBranch().isEmpty());
        QVERIFY(vm.activeFile().isEmpty());
        QVERIFY(vm.repoPath().isEmpty());   // active-repo affordance clears
        // pullRebase is no longer reset on close — it reflects the global default
        // set via applyPullDefault(), which persists across repo switches.
        QCOMPARE(vm.syncing(), false);

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

    void create_and_switch_branch_updates_model_and_head()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();
        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        vm.createBranch(QStringLiteral("feature"), QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(vm.branches()->rowCount() >= 2, 3000); // default branch + feature

        vm.switchBranch(QStringLiteral("feature"));
        QTRY_COMPARE_WITH_TIMEOUT(vm.currentBranch(), QStringLiteral("feature"), 3000);

        std::filesystem::remove_all(dir);
    }

    // Unchecking one of two added lines must drive the file to Partial and stage
    // only the still-checked line: after a partial commit the file stays dirty
    // because the unstaged line remains uncommitted in the worktree.
    void partial_line_staging_commits_only_checked_lines()
    {
        using Check = gittide::ui::ChangedFilesModel;
        const auto dir = repo_view_model_test::make_multiline_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);

        QSignalSpy diffSpy(vm.diffLines(), &QAbstractItemModel::modelReset);
        vm.selectFile(QStringLiteral("a.txt"));
        QVERIFY(diffSpy.wait(3000));

        // Locate the two checkable (added) rows in the flattened diff model.
        QList<int> checkable;
        for (int i = 0; i < vm.diffLines()->rowCount(QModelIndex()); ++i)
        {
            const QModelIndex idx = vm.diffLines()->index(i, 0);
            if (idx.data(gittide::ui::DiffLinesModel::CheckableRole).toBool())
                checkable.append(i);
        }
        QCOMPARE(checkable.size(), 2);

        // File starts whole-checked → every added line is checked.
        QCOMPARE(vm.changedFiles()->checkState(0), Check::Checked);

        // Uncheck the second added line → file becomes Partial, still counted.
        QSignalSpy checkedSpy(&vm, &RepoViewModel::checkedChanged);
        vm.setLineChecked(checkable.at(1), false);
        QVERIFY(checkedSpy.count() >= 1);
        QCOMPARE(vm.changedFiles()->checkState(0), Check::Partial);
        QCOMPARE(vm.checkedCount(), 1);

        // Commit the partial selection; status refresh follows.
        QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
        vm.commit(QStringLiteral("partial"), QString());
        QVERIFY(committedSpy.wait(3000));

        // One line was left unstaged → the file is still dirty after the commit.
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);

        std::filesystem::remove_all(dir);
    }

    void discardFile_restores_clean_status()
    {
        const auto dir = repo_view_model_test::make_dirty_repo(); // has modified a.txt
        RepoViewModel vm;

        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1); // a.txt modified

        QSignalSpy statusSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.discardFile(QStringLiteral("a.txt"));
        QVERIFY(statusSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 0); // clean after discard

        std::filesystem::remove_all(dir);
    }

    void copyToClipboard_sets_system_clipboard()
    {
        RepoViewModel vm;
        vm.copyToClipboard(QStringLiteral("abc123"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("abc123"));
    }
};

#include "test_repo_view_model.moc"
