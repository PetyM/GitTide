#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>
#include <git2.h>
#include <qcorotask.h>

#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/repocontroller.hpp"

using gittide::ui::RepoController;

namespace repo_controller_test {
std::filesystem::path make_empty_repo()
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-rc-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

// Repo with a committed a.txt then a local modification (1 unstaged change).
std::filesystem::path make_dirty_repo()
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-rcd-" + std::to_string(::QRandomGenerator::global()->generate()));
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
// Repo with one committed file and no working-tree modifications.
std::filesystem::path make_repo_with_commit()
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-rcwc-" + std::to_string(::QRandomGenerator::global()->generate()));
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
    git_libgit2_shutdown();
    return dir;
}
// Repo where "feature" branch has a commit not reachable from HEAD (main).
// Returns the path and sets `branchName` to the unmerged branch name.
// The repo has two branches: the default HEAD branch and "feature" which has
// one extra commit not reachable from HEAD, so a non-forced delete will fail
// with "not fully merged".
std::filesystem::path make_repo_with_unmerged_branch(std::string& branchName)
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-rcub-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    // Helper: write a.txt, stage it, commit it to `ref` using git_commit_create_v.
    auto makeCommit0 = [&](const char* ref, const char* msg, git_oid* out_oid)
    {
        std::ofstream(dir / "a.txt") << msg << "\n";
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
        git_commit_create_v(out_oid, raw, ref, sig, sig, nullptr, msg, tree, 0);
        git_signature_free(sig);
        git_tree_free(tree);
        git_index_free(idx);
    };

    auto makeCommit1 = [&](const char* ref, const char* msg, git_oid* out_oid, git_commit* parent)
    {
        std::ofstream(dir / "a.txt") << msg << "\n";
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
        git_commit_create_v(out_oid, raw, ref, sig, sig, nullptr, msg, tree, 1, parent);
        git_signature_free(sig);
        git_tree_free(tree);
        git_index_free(idx);
    };

    // 1. Initial commit on HEAD (default branch, e.g. "master" or "main").
    git_oid base_oid;
    makeCommit0("HEAD", "init", &base_oid);

    // 2. Create "feature" branch pointing at the base commit.
    git_commit* base_commit = nullptr;
    git_commit_lookup(&base_commit, raw, &base_oid);
    git_reference* feat_ref = nullptr;
    git_branch_create(&feat_ref, raw, "feature", base_commit, 0);
    git_reference_free(feat_ref);

    // 3. Make a commit on "feature" that is NOT reachable from the default branch.
    git_oid feat_oid;
    makeCommit1("refs/heads/feature", "feature-work", &feat_oid, base_commit);
    git_commit_free(base_commit);

    // HEAD remains on the default branch — "feature" is the non-HEAD branch.
    git_repository_free(raw);
    git_libgit2_shutdown();
    branchName = "feature";
    return dir;
}
} // namespace repo_controller_test

class TestRepoController : public QObject
{
    Q_OBJECT
private slots:
    void open_existing_repo_succeeds()
    {
        const auto dir = repo_controller_test::make_empty_repo();
        RepoController controller;
        QSignalSpy ok(&controller, &RepoController::repoOpened);
        QSignalSpy bad(&controller, &RepoController::repoFailed);
        controller.open(QString::fromStdString(dir.generic_string()));
        QCOMPARE(ok.count(), 1);
        QCOMPARE(bad.count(), 0);
        QVERIFY(controller.isOpen());
        std::filesystem::remove_all(dir);
    }

    void open_missing_repo_fails()
    {
        RepoController controller;
        QSignalSpy ok(&controller, &RepoController::repoOpened);
        QSignalSpy bad(&controller, &RepoController::repoFailed);
        controller.open(QStringLiteral("/no/such/gittide-repo"));
        QCOMPARE(ok.count(), 0);
        QCOMPARE(bad.count(), 1);
        QVERIFY(!controller.isOpen());
    }

    void refresh_status_emits_changes()
    {
        const auto dir = repo_controller_test::make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::statusChanged);
        QCoro::waitFor(controller.refreshStatus());
        QCOMPARE(spy.count(), 1);
        const auto files = spy.at(0).at(0).value<std::vector<gittide::FileStatus>>();
        QCOMPARE(static_cast<int>(files.size()), 1);
        std::filesystem::remove_all(dir);
    }

    void stage_then_status_shows_staged()
    {
        const auto dir = repo_controller_test::make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::statusChanged);
        QCoro::waitFor(controller.stage(gittide::StageSelection{.path = "a.txt"}));
        QVERIFY(spy.count() >= 1); // stage chains a refreshStatus
        const auto files = spy.at(spy.count() - 1).at(0).value<std::vector<gittide::FileStatus>>();
        QCOMPARE(static_cast<int>(files.size()), 1);
        QVERIFY(gittide::hasFlag(files[0].flags, gittide::StatusFlag::IndexModified));
        std::filesystem::remove_all(dir);
    }

    void refresh_diff_emits_diff_ready()
    {
        const auto dir = repo_controller_test::make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::diffReady);
        QCoro::waitFor(controller.refreshDiff(QStringLiteral("a.txt"), gittide::DiffTarget::WorktreeVsIndex));
        QCOMPARE(spy.count(), 1);
        const auto result = spy.at(0).at(1).value<gittide::DiffResult>();
        QVERIFY(!result.hunks.empty());
        std::filesystem::remove_all(dir);
    }

    void commit_also_refreshes_history()
    {
        const auto dir = repo_controller_test::make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QCoro::waitFor(controller.stage(gittide::StageSelection{.path = "a.txt"}));

        QSignalSpy historySpy(&controller, &RepoController::historyReady);
        QCoro::waitFor(controller.commit(gittide::CommitRequest{.message = "second"}));

        QVERIFY(historySpy.count() >= 1);
        const auto layout = historySpy.last().at(0).value<gittide::GraphLayout>();
        QVERIFY(layout.rows.size() >= 2); // initial commit + new commit
        std::filesystem::remove_all(dir);
    }

    void refresh_branches_emits_branches_and_head()
    {
        const auto dir = repo_controller_test::make_repo_with_commit();
        RepoController controller;
        qRegisterMetaType<std::vector<gittide::BranchInfo>>();
        qRegisterMetaType<gittide::HeadState>();
        QSignalSpy branches(&controller, &RepoController::branchesChanged);
        QSignalSpy head(&controller, &RepoController::headChanged);

        controller.open(QString::fromStdString(dir.generic_string()));
        QCoro::waitFor(controller.refreshBranches());

        QCOMPARE(branches.count(), 1);
        QCOMPARE(head.count(), 1);
        std::filesystem::remove_all(dir);
    }

    void switch_branch_runs_the_refresh_cascade()
    {
        const auto dir = repo_controller_test::make_repo_with_commit();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QCoro::waitFor(controller.createBranch(QStringLiteral("feature"), QString(), false));

        QSignalSpy status(&controller, &RepoController::statusChanged);
        QSignalSpy history(&controller, &RepoController::historyReady);
        QSignalSpy branches(&controller, &RepoController::branchesChanged);
        QCoro::waitFor(controller.switchBranch(QStringLiteral("feature")));

        QCOMPARE(status.count(), 1);
        QCOMPARE(history.count(), 1);
        QCOMPARE(branches.count(), 1);
        std::filesystem::remove_all(dir);
    }

    void delete_unmerged_branch_emits_deleteFailedUnmerged()
    {
        std::string branchName;
        const auto dir = repo_controller_test::make_repo_with_unmerged_branch(branchName);
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(controller.isOpen());

        const QString qBranchName = QString::fromStdString(branchName);
        QSignalSpy unmergedSpy(&controller, &RepoController::deleteFailedUnmerged);
        QSignalSpy failedSpy(&controller, &RepoController::operationFailed);
        QSignalSpy branchesSpy(&controller, &RepoController::branchesChanged);

        // Non-forced delete of an unmerged branch should emit deleteFailedUnmerged.
        QCoro::waitFor(controller.deleteBranch(qBranchName, false));
        QCOMPARE(unmergedSpy.count(), 1);
        QCOMPARE(unmergedSpy.at(0).at(0).toString(), qBranchName);
        QCOMPARE(failedSpy.count(), 0); // not routed to operationFailed
        QCOMPARE(branchesSpy.count(), 0); // branch was NOT deleted

        // Force delete should succeed and emit branchesChanged.
        QCoro::waitFor(controller.deleteBranch(qBranchName, true));
        QCOMPARE(branchesSpy.count(), 1);

        std::filesystem::remove_all(dir);
    }

    void commit_selection_commits_only_checked_files()
    {
        const auto dir = repo_controller_test::make_repo_with_commit();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));

        // create two new files in the worktree
        { std::ofstream(dir / "keep.txt") << "keep\n"; }
        { std::ofstream(dir / "skip.txt") << "skip\n"; }

        QSignalSpy committed(&controller, &RepoController::committed);
        std::vector<gittide::StageSelection> sel{ {"keep.txt", std::nullopt, {}} }; // only keep.txt
        QCoro::waitFor(controller.commitSelection(gittide::CommitRequest{"add keep"}, sel));

        QCOMPARE(committed.count(), 1);

        // skip.txt must still be an uncommitted change after the commit
        auto repo = gittide::ui::AsyncRepo::open(dir);
        auto st   = QCoro::waitFor(repo->status());
        QVERIFY(st.has_value());
        bool skipStillThere = std::any_of(st->begin(), st->end(),
            [](const auto& f){ return f.path.generic_string() == "skip.txt"; });
        QVERIFY(skipStillThere);
        std::filesystem::remove_all(dir);
    }

    void refresh_commit_files_emits_for_a_commit()
    {
        const auto dir = repo_controller_test::make_repo_with_commit();
        RepoController controller;
        qRegisterMetaType<std::vector<gittide::FileStatus>>();
        controller.open(QString::fromStdString(dir.generic_string()));

        auto repo = gittide::ui::AsyncRepo::open(dir);
        const QString oid = QString::fromStdString(QCoro::waitFor(repo->log())->front().oid);

        QSignalSpy spy(&controller, &RepoController::commitFilesReady);
        QCoro::waitFor(controller.refreshCommitFiles(oid));
        QCOMPARE(spy.count(), 1);
        std::filesystem::remove_all(dir);
    }

    // D35: an external working-tree change refreshes status with no explicit call.
    void external_worktree_change_auto_refreshes_status()
    {
        const auto dir  = repo_controller_test::make_repo_with_commit();
        const auto root = std::filesystem::canonical(dir); // match libgit2's canonical workdir
        RepoController controller(nullptr, 30);            // small debounce for the test
        controller.open(QString::fromStdString(dir.generic_string()));
        QTest::qWait(300); // let the initial rearmWatch arm the watcher

        QSignalSpy spy(&controller, &RepoController::statusChanged);
        std::ofstream(root / "external.txt") << "made outside GitTide\n";

        QVERIFY(spy.wait(3000));
        const auto files = spy.last().at(0).value<std::vector<gittide::FileStatus>>();
        QVERIFY(!files.empty());
        std::filesystem::remove_all(dir);
    }

    // D35: an external git-dir change runs the full cascade (history refreshes).
    void external_gitdir_change_auto_refreshes_history()
    {
        const auto dir  = repo_controller_test::make_repo_with_commit();
        const auto root = std::filesystem::canonical(dir);
        RepoController controller(nullptr, 30);
        controller.open(QString::fromStdString(dir.generic_string()));
        QTest::qWait(300);

        QSignalSpy spy(&controller, &RepoController::historyReady);
        std::ofstream(root / ".git" / "gittide-probe") << "x\n";

        QVERIFY(spy.wait(3000));
        std::filesystem::remove_all(dir);
    }
};

#include "test_repo_controller.moc"
