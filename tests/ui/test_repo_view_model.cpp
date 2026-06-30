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

    // D35: resync() re-reads the working tree, so an external change (made while
    // GitTide was backgrounded) shows up on focus-in.
    void resync_picks_up_external_change()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy    filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);

        // A second file appears outside GitTide; an explicit resync must see it.
        std::ofstream(dir / "external.txt") << "made outside\n";
        vm.resync();

        QTRY_COMPARE_WITH_TIMEOUT(vm.changedFiles()->rowCount(QModelIndex()), 2, 3000);
        std::filesystem::remove_all(dir);
    }

    // A refresh must not silently grow a partial selection: if the user has
    // unchecked something, new files/changes arrive unchecked and prior unchecks
    // survive the refresh.
    void refresh_keeps_unchecks_and_leaves_new_files_unchecked()
    {
        using Check = gittide::ui::ChangedFilesModel;
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy    filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);

        // Uncheck the one file → selection is now partial (not "all checked").
        vm.setFileChecked(vm.changedFiles()->rowForPath(QStringLiteral("a.txt")), false);
        QCOMPARE(vm.checkedCount(), 0);

        // A new file appears and a refresh runs.
        std::ofstream(dir / "external.txt") << "new\n";
        vm.resync();
        QTRY_COMPARE_WITH_TIMEOUT(vm.changedFiles()->rowCount(QModelIndex()), 2, 3000);

        // The prior uncheck survived and the new file came in unchecked.
        QCOMPARE(vm.changedFiles()->checkState(vm.changedFiles()->rowForPath(QStringLiteral("a.txt"))), Check::Unchecked);
        QCOMPARE(vm.changedFiles()->checkState(vm.changedFiles()->rowForPath(QStringLiteral("external.txt"))), Check::Unchecked);
        QCOMPARE(vm.checkedCount(), 0);

        std::filesystem::remove_all(dir);
    }

    // When everything is checked, new files/changes inherit that and come in
    // checked, so an "all-in" selection stays all-in across refreshes.
    void refresh_auto_checks_new_files_when_all_checked()
    {
        using Check = gittide::ui::ChangedFilesModel;
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy    filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.checkedCount(), 1); // the one file is checked by default

        std::ofstream(dir / "external.txt") << "new\n";
        vm.resync();
        QTRY_COMPARE_WITH_TIMEOUT(vm.changedFiles()->rowCount(QModelIndex()), 2, 3000);

        QCOMPARE(vm.changedFiles()->checkState(vm.changedFiles()->rowForPath(QStringLiteral("external.txt"))), Check::Checked);
        QCOMPARE(vm.checkedCount(), 2);

        std::filesystem::remove_all(dir);
    }

    void resync_when_closed_is_safe_noop()
    {
        RepoViewModel vm;
        vm.resync(); // nothing open → must be a safe no-op
        QVERIFY(!vm.repoOpen());
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

    // The dirty property must NOTIFY on a clean→dirty transition: a QML binding
    // like `enabled: repoVm.dirty` only re-evaluates when dirtyChanged() fires.
    // onStatus() emits it on every status rebuild, so a refresh that turns the
    // tree dirty both flips dirty() and notifies.
    void dirty_notifies_on_status_refresh()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        // Commit the initial dirt so the working tree is clean (dirty() == false).
        QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
        vm.commit(QStringLiteral("clean it"), QString());
        QVERIFY(committedSpy.wait(3000));
        QTRY_VERIFY_WITH_TIMEOUT(!vm.dirty(), 3000);

        // Now dirty the tree externally and refresh: dirty() flips true and the
        // dedicated notify fires at least once.
        QSignalSpy dirtySpy(&vm, &RepoViewModel::dirtyChanged);
        std::ofstream(dir / "a.txt") << "one\ntwo\nthree\n";
        vm.resync();

        QTRY_VERIFY_WITH_TIMEOUT(vm.dirty(), 3000);
        QVERIFY(dirtySpy.count() >= 1);

        std::filesystem::remove_all(dir);
    }

    void copyToClipboard_sets_system_clipboard()
    {
        RepoViewModel vm;
        vm.copyToClipboard(QStringLiteral("abc123"));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("abc123"));
    }

    void select_file_at_row_selects_correct_file()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QVERIFY(vm.changedFiles()->rowCount() > 0);

        // Also wait for diff to populate to ensure the async diff load completes
        QSignalSpy diffSpy(vm.diffLines(), &QAbstractItemModel::modelReset);
        vm.selectFileAtRow(0);
        QVERIFY(diffSpy.wait(3000));
        QVERIFY(!vm.activeFile().isEmpty());

        std::filesystem::remove_all(dir);
    }

    void select_file_at_row_ignores_out_of_bounds()
    {
        RepoViewModel vm;
        // No repo open — changedFiles is empty. Should not crash.
        vm.selectFileAtRow(-1);
        vm.selectFileAtRow(0);
        vm.selectFileAtRow(99);
        // Just verify no crash.
        QVERIFY(true);
    }

    void select_commit_at_row_ignores_out_of_bounds()
    {
        RepoViewModel vm;
        // No repo open — history is empty. Should not crash.
        vm.selectCommitAtRow(-1);
        vm.selectCommitAtRow(0);
        vm.selectCommitAtRow(99);
        // Just verify no crash.
        QVERIFY(true);
    }

    void select_commit_at_row_selects_correct_commit()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        // Wait for history to load
        QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
        QVERIFY(historySpy.wait(3000));
        QVERIFY(vm.history()->rowCount(QModelIndex()) > 0);

        // Select the first commit and verify selectedCommit is populated
        vm.selectCommitAtRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(!vm.selectedCommit().isEmpty(), 3000);

        std::filesystem::remove_all(dir);
    }

    void select_commit_file_at_row_ignores_out_of_bounds()
    {
        RepoViewModel vm;
        vm.selectCommitFileAtRow(-1);
        vm.selectCommitFileAtRow(0);
        QVERIFY(true);
    }

    void reword_updates_head_summary_in_history()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();
        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        // Commit the dirty change so HEAD has a known message.
        QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
        vm.commit(QStringLiteral("before reword"), QString());
        QVERIFY(committedSpy.wait(3000));
        QTRY_VERIFY_WITH_TIMEOUT(vm.history()->rowCount() >= 1, 3000);

        const QString headOid = vm.history()->data(
            vm.history()->index(0, 0), gittide::ui::HistoryListModel::OidRole).toString();

        // Lazy-fetch the message round-trips the committed text.
        QSignalSpy msgSpy(&vm, &RepoViewModel::commitMessageReady);
        vm.requestCommitMessage(headOid);
        QVERIFY(msgSpy.wait(3000));
        QCOMPARE(msgSpy.takeFirst().at(1).toString(), QStringLiteral("before reword"));

        // Reword → the top history row's summary changes.
        vm.rewordHead(QStringLiteral("after reword"));
        QTRY_COMPARE_WITH_TIMEOUT(
            vm.history()->data(vm.history()->index(0, 0),
                               gittide::ui::HistoryListModel::SummaryRole).toString(),
            QStringLiteral("after reword"), 3000);

        std::filesystem::remove_all(dir);
    }

    void range_selection_shows_combined_files_and_header()
    {
        // Build a repo with 3 commits: init (a.txt), c2 (dirty a.txt), c3 (b.txt).
        const auto dir = repo_view_model_test::make_dirty_repo(); // has 1 commit + dirty a.txt
        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        // c2: commit the dirty a.txt
        {
            QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
            vm.commit(QStringLiteral("c2"), QString());
            QVERIFY(committedSpy.wait(3000));
        }
        QTRY_VERIFY_WITH_TIMEOUT(vm.history()->rowCount() >= 2, 3000);

        // c3: write b.txt, re-open the repo to trigger a status refresh, then commit.
        {
            std::ofstream(dir / "b.txt") << "b content\n";
            QSignalSpy filesSpy2(vm.changedFiles(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(filesSpy2.wait(3000));
            QTRY_VERIFY_WITH_TIMEOUT(vm.changedFiles()->rowCount() >= 1, 3000);
            QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
            vm.commit(QStringLiteral("c3"), QString());
            QVERIFY(committedSpy.wait(3000));
        }
        QTRY_VERIFY_WITH_TIMEOUT(vm.history()->rowCount() >= 3, 3000);

        auto oidAt = [&](int row) {
            return vm.history()->data(vm.history()->index(row, 0),
                                      gittide::ui::HistoryListModel::OidRole).toString();
        };
        const QString newest = oidAt(0);
        const QString oldest = oidAt(2);
        (void)newest; (void)oldest;

        // Contiguous range rows {0,1} → combined files + header.
        QSignalSpy cfReset(vm.commitFiles(), &QAbstractItemModel::modelReset);
        vm.selectCommitRows(QVariantList{0, 1});
        QVERIFY(cfReset.wait(3000));
        QVERIFY(vm.commitFiles()->rowCount() >= 1);
        QVERIFY(vm.property("historyDetailHeader").toString().contains(QStringLiteral("2 commits")));
        QVERIFY(vm.property("historyDetailHint").toString().isEmpty());

        // Non-contiguous rows {0,2} (3 commits present) → hint set, header empty, no core call.
        QVERIFY(vm.history()->rowCount() >= 3);
        vm.selectCommitRows(QVariantList{0, 2});
        QTRY_VERIFY_WITH_TIMEOUT(!vm.property("historyDetailHint").toString().isEmpty(), 3000);
        QVERIFY(vm.property("historyDetailHeader").toString().isEmpty());
        QCOMPARE(vm.commitFiles()->rowCount(), 0); // no files loaded for non-contiguous

        // Single row → header + hint both cleared.
        vm.selectCommitRows(QVariantList{0});
        QTRY_VERIFY_WITH_TIMEOUT(vm.property("historyDetailHeader").toString().isEmpty(), 3000);
        QVERIFY(vm.property("historyDetailHint").toString().isEmpty());

        std::filesystem::remove_all(dir);
    }

    // Finding 2: A repo opened that already has stashes (made in a prior session
    // or via CLI) must show a populated Stashes panel immediately — open() must
    // trigger a stash-state refresh so stashAvailable and stashes->rowCount are
    // correct without any subsequent user action.
    void open_with_pre_existing_stash_populates_stash_list()
    {
        // Use a first VM to create a stash in the repo (simulates a prior session).
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");
        tmp.writeFile("a.txt", "dirty\n");

        {
            RepoViewModel vmA;
            QSignalSpy filesSpy(vmA.changedFiles(), &QAbstractItemModel::modelReset);
            vmA.open(QString::fromStdString(tmp.path().generic_string()));
            QVERIFY(filesSpy.wait(3000));
            vmA.stashChanges();
            QTRY_COMPARE_WITH_TIMEOUT(vmA.stashes()->rowCount(), 1, 5000);
        }

        // A fresh VM opened on the same repo must see the stash without any manual op.
        RepoViewModel vm;
        vm.open(QString::fromStdString(tmp.path().generic_string()));
        QTRY_VERIFY_WITH_TIMEOUT(vm.stashAvailable(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(vm.stashes()->rowCount(), 1, 5000);
    }

    // Finding 1: After previewing a stash in repo A, switching to repo B via
    // open() (without a preceding close(), which is the Sidebar pattern) must
    // clear the stash preview immediately and keep the stash list empty after
    // repo B finishes loading.
    void switch_repo_clears_stash_preview()
    {
        // Repo A: has a stash.
        gittide::test::TempRepo repoA;
        repoA.writeFile("a.txt", "orig\n");
        repoA.commitAll("init");
        repoA.writeFile("a.txt", "dirty\n");

        // Repo B: clean (no changes, no stashes).
        gittide::test::TempRepo repoB;
        repoB.writeFile("b.txt", "clean\n");
        repoB.commitAll("init");

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(repoA.path().generic_string()));
        QVERIFY(filesSpy.wait(3000));

        // Stash the dirty file in repo A.
        vm.stashChanges();
        QTRY_COMPARE_WITH_TIMEOUT(vm.stashes()->rowCount(), 1, 5000);

        // Enter preview mode for stash 0.
        vm.previewStash(0);
        QTRY_VERIFY_WITH_TIMEOUT(vm.stashPreviewActive(), 3000);

        // Switch to repo B without close() — mirrors the Sidebar repo-switching path.
        vm.open(QString::fromStdString(repoB.path().generic_string()));

        // Preview state and stash list must be cleared synchronously by open().
        QVERIFY(!vm.stashPreviewActive());
        QCOMPARE(vm.stashes()->rowCount(), 0);

        // After repo B fully loads its stash refresh, the list stays empty (B has none).
        QTRY_VERIFY_WITH_TIMEOUT(vm.repoOpen(), 3000);
        QCOMPARE(vm.stashes()->rowCount(), 0);
        QVERIFY(!vm.stashPreviewActive());
    }

    // Stash stack populates after stashChanges(); previewStash() loads the
    // commit-diff machinery (commitFiles/commitDiff) and sets stashPreviewActive
    // + stashPreviewLabel. exitStashPreview() tears it all down.
    void stashesPopulateAndPreview()
    {
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(tmp.path().generic_string()));
        QVERIFY(filesSpy.wait(3000));

        QCOMPARE(vm.stashes()->rowCount(), 0);
        QVERIFY(!vm.stashPreviewActive());

        tmp.writeFile("a.txt", "dirty\n");
        vm.stashChanges();
        QTRY_COMPARE_WITH_TIMEOUT(vm.stashes()->rowCount(), 1, 5000);

        vm.previewStash(0);
        QTRY_VERIFY_WITH_TIMEOUT(vm.stashPreviewActive(), 3000);
        QVERIFY(vm.stashPreviewLabel().startsWith(QStringLiteral("stash@{0}")));
        QTRY_VERIFY_WITH_TIMEOUT(vm.commitFiles()->rowCount() > 0, 5000);

        vm.exitStashPreview();
        QVERIFY(!vm.stashPreviewActive());
        QCOMPARE(vm.commitFiles()->rowCount(), 0);
    }
};

#include "test_repo_view_model.moc"
