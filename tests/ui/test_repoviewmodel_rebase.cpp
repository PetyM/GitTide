// tests/ui/test_repoviewmodel_rebase.cpp
// Tests for RepoViewModel's RebaseState properties and rebase invokables.
#include <QtTest>
#include <QSignalSpy>
#include <QRandomGenerator>

#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/ui/repoviewmodel.hpp"

using gittide::ui::RepoViewModel;

namespace repo_view_model_rebase_test {

/// Build a repo where feature diverged from master on a different file
/// so that rebasing feature onto master produces no conflicts.
inline std::filesystem::path makeCleanRebaseRepo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path()
               / ("gittide-vmrb-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);

    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);

    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    auto commitFile = [&](const char* ref, const char* filename, const char* content,
                           const char* msg, git_oid* out_oid, git_commit* parent)
    {
        std::ofstream(dir / filename) << content;
        git_index* idx = nullptr;
        git_repository_index(&idx, raw);
        git_index_add_bypath(idx, filename);
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

    // 1. Base commit on master (a.txt)
    git_oid base_oid;
    commitFile("HEAD", "a.txt", "base-a\n", "base", &base_oid, nullptr);

    // 2. Create "feature" branch at base
    git_commit* base_commit = nullptr;
    git_commit_lookup(&base_commit, raw, &base_oid);
    git_reference* feat_ref = nullptr;
    git_branch_create(&feat_ref, raw, "feature", base_commit, 0);
    git_reference_free(feat_ref);

    // 3. Feature commit on b.txt (does NOT touch a.txt — no conflict possible)
    git_oid feat_oid;
    commitFile("refs/heads/feature", "b.txt", "feature-b\n", "feat", &feat_oid, base_commit);
    git_commit_free(base_commit);

    // 4. master commit on a.txt (only file) — feature's commit is on b.txt
    git_commit* master_commit = nullptr;
    git_reference_name_to_id(&base_oid, raw, "refs/heads/master");
    git_commit_lookup(&master_commit, raw, &base_oid);
    git_oid master_oid;
    commitFile("refs/heads/master", "a.txt", "master-a\n", "main", &master_oid, master_commit);
    git_commit_free(master_commit);

    // 5. Checkout feature so rebase operates on feature branch
    git_reference* head_ref = nullptr;
    git_repository_head(&head_ref, raw);
    git_reference* new_head = nullptr;
    git_reference_symbolic_create(&new_head, raw, "HEAD", "refs/heads/feature", 1, "checkout");
    if (new_head)
        git_reference_free(new_head);
    if (head_ref)
        git_reference_free(head_ref);

    // Reset the working tree to match the feature branch tip
    git_object* feat_obj = nullptr;
    git_object_lookup(&feat_obj, raw, &feat_oid, GIT_OBJECT_COMMIT);
    git_checkout_options co_opts = GIT_CHECKOUT_OPTIONS_INIT;
    co_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
    git_checkout_tree(raw, feat_obj, &co_opts);
    git_object_free(feat_obj);

    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

/// Build a repo with `n` linear commits (c0 root, then c1..c(n-1)), each adding a
/// distinct file so a reorder never conflicts. HEAD is on master at c(n-1).
inline std::filesystem::path makeLinearRepo(int n)
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path()
               / ("gittide-vmreorder-" + std::to_string(::QRandomGenerator::global()->generate()));
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

inline std::string headMessage(const std::filesystem::path& dir)
{
    git_libgit2_init();
    git_repository* raw = nullptr;
    git_repository_open(&raw, dir.generic_string().c_str());
    git_oid head; git_reference_name_to_id(&head, raw, "HEAD");
    git_commit* c = nullptr; git_commit_lookup(&c, raw, &head);
    std::string msg = git_commit_message(c) ? git_commit_message(c) : "";
    git_commit_free(c); git_repository_free(raw); git_libgit2_shutdown();
    return msg;
}

} // namespace repo_view_model_rebase_test

class TestRepoViewModelRebase : public QObject
{
    Q_OBJECT

private slots:
    /// Initial state (no rebase in progress) must report rebaseInProgress == false.
    void initial_rebase_state_is_not_in_progress()
    {
        const auto dir = repo_view_model_rebase_test::makeCleanRebaseRepo();

        RepoViewModel vm;
        vm.open(QString::fromStdString(dir.generic_string()));

        QCOMPARE(vm.property("rebaseInProgress").toBool(), false);
        QCOMPARE(vm.property("rebaseStep").toInt(), 0);
        QCOMPARE(vm.property("rebaseTotal").toInt(), 0);
        QCOMPARE(vm.property("rebaseConflictedCount").toInt(), 0);
        QCOMPARE(vm.property("rebaseHasSubmoduleConflicts").toBool(), false);

        std::filesystem::remove_all(dir);
    }

    /// A clean (non-conflicting) rebase must fire rebaseStateChanged and finish
    /// with rebaseInProgress == false.
    void properties_reflect_controller_state()
    {
        const auto dir = repo_view_model_rebase_test::makeCleanRebaseRepo();
        QVERIFY(!dir.empty());

        RepoViewModel vm;
        QSignalSpy openSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(openSpy.wait(3000));

        QSignalSpy spy(&vm, &RepoViewModel::rebaseStateChanged);
        vm.startRebase(QStringLiteral("master")); // non-conflicting → finishes cleanly

        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
        QVERIFY(!vm.rebaseInProgress());

        std::filesystem::remove_all(dir);
    }

    /// reorderableRunLength counts the linear single-parent run from HEAD. For a
    /// fully linear history of n commits, the run is n-1 (the root has no parent).
    void reorderable_run_counts_linear_run()
    {
        const auto dir = repo_view_model_rebase_test::makeLinearRepo(3); // c0(root), c1, c2
        RepoViewModel vm;
        vm.open(QString::fromStdString(dir.generic_string()));
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

        std::filesystem::remove_all(dir);
    }

    /// reorderCommits replays the run in the new order: swapping the two newest
    /// commits makes the former second-newest the new HEAD.
    void reorder_commits_rewrites_history_order()
    {
        const auto dir = repo_view_model_rebase_test::makeLinearRepo(3); // newest-first: c2, c1, c0
        QCOMPARE(repo_view_model_rebase_test::headMessage(dir), std::string("c2\n"));

        RepoViewModel vm;
        vm.open(QString::fromStdString(dir.generic_string()));
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

        QSignalSpy spy(&vm, &RepoViewModel::rebaseStateChanged);
        // Move row 0 (c2) below row 1 (c1) → new order newest-first: c1, c2, c0.
        QMetaObject::invokeMethod(&vm, "reorderCommits", Q_ARG(int, 0), Q_ARG(int, 1));
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
        QTRY_VERIFY(!vm.rebaseInProgress());

        QTRY_COMPARE_WITH_TIMEOUT(repo_view_model_rebase_test::headMessage(dir), std::string("c1\n"), 3000);

        std::filesystem::remove_all(dir);
    }

    /// squashCommitInto folds the dragged commit into the target: squashing the
    /// newest commit (row 0) into the second-newest (row 1) yields one combined
    /// commit at the target's slot, carrying both commits' changes.
    void squash_commit_into_folds_dragged_into_target()
    {
        const auto dir = repo_view_model_rebase_test::makeLinearRepo(3); // newest-first: c2, c1, c0
        QCOMPARE(repo_view_model_rebase_test::headMessage(dir), std::string("c2\n"));

        RepoViewModel vm;
        vm.open(QString::fromStdString(dir.generic_string()));
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

        // Drag c2 (row 0) onto c1 (row 1) → squash c2 into c1.
        QMetaObject::invokeMethod(&vm, "squashCommitInto", Q_ARG(int, 0), Q_ARG(int, 1));

        // The engine pauses for the combined message (squash), prefilled target-then-dragged.
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("rebasePauseReason").toString(), QStringLiteral("message"), 5000);
        QVERIFY(vm.property("rebaseMessagePrefill").toString().contains(QStringLiteral("c1")));
        QVERIFY(vm.property("rebaseMessagePrefill").toString().contains(QStringLiteral("c2")));

        // Confirm the combined message → rebase completes.
        QMetaObject::invokeMethod(&vm, "continueRebase", Q_ARG(QString, QStringLiteral("c1+c2\n")));
        QTRY_VERIFY_WITH_TIMEOUT(!vm.rebaseInProgress(), 5000);

        // HEAD is the combined commit (target's identity, new message), and it carries
        // the dragged commit's file (f2.txt from c2) folded in.
        QTRY_COMPARE_WITH_TIMEOUT(repo_view_model_rebase_test::headMessage(dir), std::string("c1+c2\n"), 3000);
        QVERIFY(std::filesystem::exists(dir / "f2.txt"));
        QVERIFY(std::filesystem::exists(dir / "f1.txt"));

        std::filesystem::remove_all(dir);
    }

    /// Opening a new repo clears the reorderable run: a stale run length must not
    /// survive a layout reset, or squashCommitInto/reorderCommits would index an
    /// empty rows vector and crash.
    void opening_new_repo_resets_reorderable_run()
    {
        const auto linear = repo_view_model_rebase_test::makeLinearRepo(3);
        RepoViewModel vm;
        vm.open(QString::fromStdString(linear.generic_string()));
        QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

        // Open a fresh empty (unborn) repo → layout clears → run length must reset to 0
        // synchronously at open() time (not wait for async history), so an immediately
        // following squashCommitInto is a safe no-op rather than a crash.
        git_libgit2_init();
        auto empty = std::filesystem::temp_directory_path()
                     / ("gittide-vmempty-" + std::to_string(::QRandomGenerator::global()->generate()));
        std::filesystem::create_directories(empty);
        git_repository* raw = nullptr;
        git_repository_init(&raw, empty.generic_string().c_str(), 0);
        git_repository_free(raw);
        git_libgit2_shutdown();

        vm.open(QString::fromStdString(empty.generic_string()));
        // Synchronous: open() clears m_lastLayout and must reset the run immediately.
        QCOMPARE(vm.property("reorderableRunLength").toInt(), 0);

        // Must NOT crash — guarded no-op.
        QMetaObject::invokeMethod(&vm, "squashCommitInto", Q_ARG(int, 0), Q_ARG(int, 1));
        QMetaObject::invokeMethod(&vm, "reorderCommits", Q_ARG(int, 0), Q_ARG(int, 1));

        std::filesystem::remove_all(linear);
        std::filesystem::remove_all(empty);
    }
};

#include "test_repoviewmodel_rebase.moc"
