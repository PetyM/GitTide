#include <QtTest>
#include <fstream>
#include <qcoro/qcorotask.h>
#include <git2.h>

#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/repocontroller.hpp"

using gittide::ui::RepoController;

class TestRepoControllerMerge : public QObject
{
    Q_OBJECT

private:
    // Build a repo where master and feature both diverged from a common base —
    // merging feature into master will produce a conflict on a.txt.
    static std::filesystem::path makeConflictRepo()
    {
        git_libgit2_init();
        auto dir = std::filesystem::temp_directory_path()
                   / ("gittide-rcm-" + std::to_string(::QRandomGenerator::global()->generate()));
        std::filesystem::create_directories(dir);

        git_repository* raw = nullptr;
        git_repository_init(&raw, dir.generic_string().c_str(), 0);

        git_config* cfg = nullptr;
        git_repository_config(&cfg, raw);
        git_config_set_string(cfg, "user.name", "T");
        git_config_set_string(cfg, "user.email", "t@e.x");
        git_config_free(cfg);

        auto makeCommit = [&](const char* ref, const char* content, const char* msg,
                              git_oid* out_oid, git_commit* parent)
        {
            std::ofstream(dir / "a.txt") << content;
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

        // 1. Base commit on HEAD (master)
        git_oid base_oid;
        makeCommit("HEAD", "base\n", "base", &base_oid, nullptr);

        // 2. Create "feature" branch at base
        git_commit* base_commit = nullptr;
        git_commit_lookup(&base_commit, raw, &base_oid);
        git_reference* feat_ref = nullptr;
        git_branch_create(&feat_ref, raw, "feature", base_commit, 0);
        git_reference_free(feat_ref);

        // 3. Feature commit (diverging content)
        git_oid feat_oid;
        makeCommit("refs/heads/feature", "feature-side\n", "feat", &feat_oid, base_commit);
        git_commit_free(base_commit);

        // 4. Master commit (conflicting content — HEAD still on master)
        git_commit* master_commit = nullptr;
        git_reference_name_to_id(&base_oid, raw, "refs/heads/master");
        git_commit_lookup(&master_commit, raw, &base_oid);
        git_oid master_oid;
        makeCommit("refs/heads/master", "master-side\n", "main", &master_oid, master_commit);
        git_commit_free(master_commit);

        git_repository_free(raw);
        git_libgit2_shutdown();
        return dir;
    }

    // Build a repo where feature is strictly ahead of master (FF merge)
    static std::filesystem::path makeFastForwardRepo()
    {
        git_libgit2_init();
        auto dir = std::filesystem::temp_directory_path()
                   / ("gittide-rcff-" + std::to_string(::QRandomGenerator::global()->generate()));
        std::filesystem::create_directories(dir);

        git_repository* raw = nullptr;
        git_repository_init(&raw, dir.generic_string().c_str(), 0);

        git_config* cfg = nullptr;
        git_repository_config(&cfg, raw);
        git_config_set_string(cfg, "user.name", "T");
        git_config_set_string(cfg, "user.email", "t@e.x");
        git_config_free(cfg);

        auto makeCommit = [&](const char* ref, const char* content, const char* msg,
                              git_oid* out_oid, git_commit* parent)
        {
            std::ofstream(dir / "a.txt") << content;
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

        // 1. Base commit on master
        git_oid base_oid;
        makeCommit("HEAD", "base\n", "base", &base_oid, nullptr);

        // 2. Create "feature" branch at base
        git_commit* base_commit = nullptr;
        git_commit_lookup(&base_commit, raw, &base_oid);
        git_reference* feat_ref = nullptr;
        git_branch_create(&feat_ref, raw, "feature", base_commit, 0);
        git_reference_free(feat_ref);

        // 3. Feature commit — master stays at base → FF merge possible
        git_oid feat_oid;
        makeCommit("refs/heads/feature", "feature-only\n", "feat", &feat_oid, base_commit);
        git_commit_free(base_commit);

        git_repository_free(raw);
        git_libgit2_shutdown();
        return dir;
    }

private slots:
    void conflict_merge_emits_mergeState_with_conflicts()
    {
        const auto dir = makeConflictRepo();
        QVERIFY(!dir.empty());

        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(controller.isOpen());

        QSignalSpy mergeStateSpy(&controller, &RepoController::mergeStateChanged);

        QCoro::waitFor(controller.merge(QStringLiteral("feature")));

        // mergeStateChanged must fire at least once (from refreshAfterMerge)
        QVERIFY(mergeStateSpy.count() >= 1);
        const auto lastState = mergeStateSpy.last().at(0).value<gittide::MergeState>();
        QVERIFY(lastState.inProgress);
        QCOMPARE(static_cast<int>(lastState.conflictedPaths.size()), 1);

        std::filesystem::remove_all(dir);
    }

    void clean_ff_merge_emits_mergeFinished_not_inProgress()
    {
        const auto dir = makeFastForwardRepo();
        QVERIFY(!dir.empty());

        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(controller.isOpen());

        QSignalSpy mergeFinishedSpy(&controller, &RepoController::mergeFinished);
        QSignalSpy mergeStateSpy(&controller, &RepoController::mergeStateChanged);

        QCoro::waitFor(controller.merge(QStringLiteral("feature")));

        // mergeFinished must fire (FF path)
        QCOMPARE(mergeFinishedSpy.count(), 1);
        QVERIFY(!mergeFinishedSpy.at(0).at(0).toString().isEmpty());

        // After the merge the state must NOT be in progress
        QVERIFY(mergeStateSpy.count() >= 1);
        const auto lastState = mergeStateSpy.last().at(0).value<gittide::MergeState>();
        QVERIFY(!lastState.inProgress);

        std::filesystem::remove_all(dir);
    }

    /// I-2 regression guard: calling retryMergeDeinitSubmodules with an empty
    /// name must NOT abort the in-progress merge — it must emit operationFailed
    /// and leave the merge state intact (still inProgress). Without the guard,
    /// the controller would abort the conflicted merge and then call merge(""),
    /// destroying the user's work.
    void retry_with_empty_name_does_not_abort_merge()
    {
        const auto dir = makeConflictRepo();
        QVERIFY(!dir.empty());

        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(controller.isOpen());

        // Put the repo into a conflicted mid-merge state.
        QCoro::waitFor(controller.merge(QStringLiteral("feature")));

        QSignalSpy mergeStateSpy(&controller, &RepoController::mergeStateChanged);
        QVERIFY(mergeStateSpy.count() >= 0); // may be 0 — we just need the baseline

        // Confirm we are actually in a conflicted state before the retry.
        QSignalSpy stateSpy2(&controller, &RepoController::mergeStateChanged);
        QCoro::waitFor(controller.refreshStatus());
        // Find the last emitted merge state to confirm inProgress.
        gittide::MergeState stateBeforeRetry;
        if (stateSpy2.count() > 0)
            stateBeforeRetry = stateSpy2.last().at(0).value<gittide::MergeState>();
        // If we didn't get a new signal, read from a fresh refresh.
        if (!stateBeforeRetry.inProgress)
        {
            // Try once more with the combined spy.
            QSignalSpy stateSpy3(&controller, &RepoController::mergeStateChanged);
            QCoro::waitFor(controller.refreshStatus());
            if (stateSpy3.count() > 0)
                stateBeforeRetry = stateSpy3.last().at(0).value<gittide::MergeState>();
        }
        QVERIFY2(stateBeforeRetry.inProgress,
                 "Precondition: merge must be inProgress before testing retry guard");

        // Now call retry with an empty name — should fail fast without aborting.
        QSignalSpy failedSpy(&controller, &RepoController::operationFailed);
        QSignalSpy abortSpy(&controller, &RepoController::mergeStateChanged);

        QCoro::waitFor(controller.retryMergeDeinitSubmodules(QString{}));

        // operationFailed must have fired exactly once with a non-empty message.
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(!failedSpy.at(0).at(0).toString().isEmpty());

        // The merge state must NOT have changed (no abort-then-fail happened).
        // abortSpy should be empty — or if it fired, the state must still be inProgress.
        bool mergeStillInProgress = stateBeforeRetry.inProgress;
        if (abortSpy.count() > 0)
        {
            const auto lastAfter = abortSpy.last().at(0).value<gittide::MergeState>();
            mergeStillInProgress = lastAfter.inProgress;
        }
        QVERIFY2(mergeStillInProgress,
                 "retryMergeDeinitSubmodules with empty name aborted the merge — I-2 regression");

        std::filesystem::remove_all(dir);
    }
};

#include "test_repocontroller_merge.moc"
