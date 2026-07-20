#include <QtTest>
#include <fstream>
#include <system_error>
#include <qcoro/qcorotask.h>
#include <git2.h>

#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/repocontroller.hpp"

using gittide::ui::RepoController;

class TestRepoControllerRebase : public QObject
{
    Q_OBJECT

private:
    // Build a repo where feature diverged from master on a different file
    // so that rebasing feature onto master produces no conflicts.
    static std::filesystem::path makeCleanRebaseRepo()
    {
        git_libgit2_init();
        auto dir = std::filesystem::temp_directory_path()
                   / ("gittide-rcrb-" + std::to_string(::QRandomGenerator::global()->generate()));
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

private slots:
    void clean_rebase_emits_finished_and_idle_state()
    {
        const auto dir = makeCleanRebaseRepo();
        QVERIFY(!dir.empty());

        RepoController ctrl;
        ctrl.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(ctrl.isOpen());

        QSignalSpy finishedSpy(&ctrl, &RepoController::rebaseFinished);
        QSignalSpy stateSpy(&ctrl, &RepoController::rebaseStateChanged);

        QCoro::waitFor(ctrl.startRebase(QStringLiteral("master")));

        // rebaseFinished must have fired exactly once
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(!finishedSpy.at(0).at(0).toString().isEmpty());

        // Last rebaseStateChanged reports not-in-progress
        QVERIFY(stateSpy.count() >= 1);
        const auto last = stateSpy.last().at(0).value<gittide::RebaseState>();
        QVERIFY(!last.inProgress);

        { std::error_code rec; std::filesystem::remove_all(dir, rec); }
    }
};

#include "test_repocontroller_rebase.moc"
