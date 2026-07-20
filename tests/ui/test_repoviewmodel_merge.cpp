// tests/ui/test_repoviewmodel_merge.cpp
// Tests for RepoViewModel's MergeState properties and merge invokables.
#include <QtTest>
#include <QSignalSpy>
#include <QRandomGenerator>

#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/ui/repoviewmodel.hpp"

using gittide::ui::RepoViewModel;

namespace repo_view_model_merge_test {

/// Build a repo where master and feature both diverged from a common base —
/// merging feature into master will produce a conflict on a.txt.
inline std::filesystem::path makeConflictRepo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path()
               / ("gittide-vmm-" + std::to_string(::QRandomGenerator::global()->generate()));
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

} // namespace repo_view_model_merge_test

class TestRepoViewModelMerge : public QObject
{
    Q_OBJECT

private slots:
    /// After startMerge triggers a conflicting merge the VM must report
    /// mergeInProgress == true and conflictedCount == 1.
    void startMerge_conflict_sets_mergeInProgress_and_conflictedCount()
    {
        const auto dir = repo_view_model_merge_test::makeConflictRepo();
        QVERIFY(!dir.empty());

        RepoViewModel vm;
        QSignalSpy openSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(openSpy.wait(15000));

        QSignalSpy mergeSpy(&vm, &RepoViewModel::mergeStateChanged);
        vm.startMerge(QStringLiteral("feature"));

        // Wait until the merge actually registers as in-progress. An early
        // mergeStateChanged emission can precede the in-progress state, so poll
        // the property itself rather than the first signal (avoids a race).
        QTRY_VERIFY_WITH_TIMEOUT(vm.property("mergeInProgress").toBool(), 15000);
        QCOMPARE(vm.property("conflictedCount").toInt(), 1);
        QCOMPARE(vm.property("hasSubmoduleConflicts").toBool(), false);

        { std::error_code rec; std::filesystem::remove_all(dir, rec); }
    }

    /// Initial state (no merge in progress) must report mergeInProgress == false.
    void initial_merge_state_is_not_in_progress()
    {
        const auto dir = repo_view_model_merge_test::makeConflictRepo();

        RepoViewModel vm;
        vm.open(QString::fromStdString(dir.generic_string()));

        QCOMPARE(vm.property("mergeInProgress").toBool(), false);
        QCOMPARE(vm.property("conflictedCount").toInt(), 0);
        QCOMPARE(vm.property("mergedRef").toString(), QString());
        QCOMPARE(vm.property("hasSubmoduleConflicts").toBool(), false);

        { std::error_code rec; std::filesystem::remove_all(dir, rec); }
    }

    /// After acceptConflict(0, 0) (keep ours), the on-disk file must have no
    /// conflict markers, and DiffLinesModel::isResolved() must return true.
    /// Also verifies Bug I-1 fix: the active file must NOT be blanked by the
    /// async refreshStatus that follows acceptConflict (previously onStatus()
    /// always cleared m_activeFile, blanking the diff panel mid-resolution).
    void accept_resolves_conflict()
    {
        const auto dir = repo_view_model_merge_test::makeConflictRepo();
        QVERIFY(!dir.empty());

        RepoViewModel vm;
        QSignalSpy openSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(openSpy.wait(15000));

        // Start the conflicting merge and wait until it registers as in-progress
        // (poll the property, not the first signal — see the note above).
        vm.startMerge(QStringLiteral("feature"));
        QTRY_VERIFY_WITH_TIMEOUT(vm.property("mergeInProgress").toBool(), 15000);
        QCOMPARE(vm.property("conflictedCount").toInt(), 1);

        // Select the conflicted file — this should load conflict content into diffLines.
        QSignalSpy fileChangedSpy(&vm, &RepoViewModel::activeFileChanged);
        vm.selectFile(QStringLiteral("a.txt"));
        // Wait for the activeFile signal (selectFile sets it synchronously, but
        // the conflict content load may also trigger it; either way we just check).
        QTRY_VERIFY_WITH_TIMEOUT(fileChangedSpy.count() >= 1, 15000);

        // The diff model should now be in conflict mode (isResolved() == false).
        QVERIFY(!vm.diffLines()->isResolved());

        // Reset the spy so we can precisely detect the post-accept refresh cycle.
        fileChangedSpy.clear();

        // Accept "ours" side (which: 0) for region 0.
        vm.acceptConflict(0, 0);

        // The file on disk must no longer contain conflict markers.
        const std::filesystem::path filePath = dir / "a.txt";
        std::ifstream ifs(filePath);
        QVERIFY(ifs.is_open());
        const std::string diskContent((std::istreambuf_iterator<char>(ifs)),
                                       std::istreambuf_iterator<char>());
        QVERIFY2(!diskContent.contains("<<<<<<<"),
                 "File still contains conflict markers after acceptConflict");

        // After the re-select triggered by acceptConflict, the model must be resolved.
        QVERIFY(vm.diffLines()->isResolved());

        // I-1 regression guard: spin the event loop long enough for the async
        // refreshStatus (fired by acceptConflict) to complete and call onStatus.
        // With the bug present, onStatus would clear m_activeFile → activeFile
        // becomes empty and diffLines is cleared. With the fix, the active file
        // stays set because a.txt is still in the refreshed status list.
        // Allow a few status/active-file cycles and then check final state.
        QTest::qWait(500); // let the async refresh settle
        // Process any remaining queued events (controller signals, etc.)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        QVERIFY2(!vm.property("activeFile").toString().isEmpty(),
                 "activeFile was cleared by refreshStatus — I-1 regression: "
                 "diff panel would blank mid-conflict-resolution");
        QVERIFY2(vm.diffLines()->rowCount(QModelIndex()) > 0,
                 "diffLines was cleared by refreshStatus — I-1 regression: "
                 "diff panel would blank mid-conflict-resolution");

        { std::error_code rec; std::filesystem::remove_all(dir, rec); }
    }
};

#include "test_repoviewmodel_merge.moc"
