#include <QObject>
#include <QtTest/QtTest>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <git2.h>
#include <qcorotask.h>

#include "gittide/filestatus.hpp"
#include "gittide/ui/asyncrepo.hpp"
#include "support/temprepo.hpp"

using gittide::ui::AsyncRepo;

namespace {
// Repo with two commits: c1 adds a.txt, c2 adds b.txt.
std::filesystem::path make_repo_with_two_commits()
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-ar2-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);

    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    git_signature* sig = nullptr;
    git_signature_now(&sig, "T", "t@e.x");

    // Commit 1: add a.txt
    {
        std::ofstream(dir / "a.txt") << "one\n";
    }
    git_index* idx = nullptr;
    git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt");
    git_index_write(idx);
    git_oid tree_oid1;
    git_index_write_tree(&tree_oid1, idx);
    git_tree* tree1 = nullptr;
    git_tree_lookup(&tree1, raw, &tree_oid1);
    git_oid c1_oid;
    git_commit_create_v(&c1_oid, raw, "HEAD", sig, sig, nullptr, "first", tree1, 0);
    git_tree_free(tree1);

    // Commit 2: add b.txt
    {
        std::ofstream(dir / "b.txt") << "two\n";
    }
    git_index_add_bypath(idx, "b.txt");
    git_index_write(idx);
    git_oid tree_oid2;
    git_index_write_tree(&tree_oid2, idx);
    git_tree* tree2 = nullptr;
    git_tree_lookup(&tree2, raw, &tree_oid2);
    git_commit* c1 = nullptr;
    git_commit_lookup(&c1, raw, &c1_oid);
    git_oid c2_oid;
    git_commit_create_v(&c2_oid, raw, "HEAD", sig, sig, nullptr, "second", tree2, 1, c1);
    git_commit_free(c1);
    git_tree_free(tree2);

    git_index_free(idx);
    git_signature_free(sig);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

// Repo with one committed file "a.txt", then locally modified (1 unstaged change).
std::filesystem::path make_dirty_repo()
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-ar-" + std::to_string(::QRandomGenerator::global()->generate()));
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
    } // unstaged modification
    git_libgit2_shutdown();
    return dir;
}
} // namespace

class TestAsyncRepo : public QObject
{
    Q_OBJECT
private slots:
    void open_missing_fails()
    {
        auto r = AsyncRepo::open("/no/such/gittide-async-repo");
        QVERIFY(!r.has_value());
    }

    void status_runs_on_pool_and_reports_change()
    {
        const auto dir = make_dirty_repo();
        auto repo      = AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        auto result = QCoro::waitFor(repo->status());
        QVERIFY(result.has_value());
        QCOMPARE(static_cast<int>(result->size()), 1);
        QCOMPARE((*result)[0].path, std::filesystem::path("a.txt"));
        QVERIFY(gittide::hasFlag((*result)[0].flags, gittide::StatusFlag::WtModified));

        std::filesystem::remove_all(dir);
    }

    void stage_whole_file_then_status_shows_staged()
    {
        const auto dir = make_dirty_repo();
        auto repo      = AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        auto staged = QCoro::waitFor(repo->stage(gittide::StageSelection{.path = "a.txt"}));
        QVERIFY(staged.has_value());

        auto result = QCoro::waitFor(repo->status());
        QVERIFY(result.has_value());
        QCOMPARE(static_cast<int>(result->size()), 1);
        QVERIFY(gittide::hasFlag((*result)[0].flags, gittide::StatusFlag::IndexModified));

        std::filesystem::remove_all(dir);
    }

    void commit_after_staging_clears_status()
    {
        const auto dir = make_dirty_repo();
        auto repo      = AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        QCoro::waitFor(repo->stage(gittide::StageSelection{.path = "a.txt"}));
        auto oid = QCoro::waitFor(repo->commit(gittide::CommitRequest{.message = "second"}));
        QVERIFY(oid.has_value());
        QVERIFY(!oid->empty());

        auto result = QCoro::waitFor(repo->status());
        QVERIFY(result.has_value());
        QCOMPARE(static_cast<int>(result->size()), 0);

        std::filesystem::remove_all(dir);
    }

    void branches_lists_and_creates()
    {
        const auto dir = make_dirty_repo();
        auto repo      = gittide::ui::AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        auto created = QCoro::waitFor(repo->createBranch(QStringLiteral("feature"), QString()));
        QVERIFY(created.has_value());
        auto list = QCoro::waitFor(repo->branches());
        QVERIFY(list.has_value());
        QVERIFY(list->size() == 2);
        std::filesystem::remove_all(dir);
    }

    void watch_targets_runs_on_pool()
    {
        const auto dir = make_dirty_repo();
        auto repo      = AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        auto t = QCoro::waitFor(repo->watchTargets());
        QVERIFY(t.has_value());
        QVERIFY(!t->workdir.empty());
        QVERIFY(!t->gitDir.empty());
        const bool hasGitDir = std::find(t->dirs.begin(), t->dirs.end(), t->gitDir) != t->dirs.end();
        const bool hasWorkdir = std::find(t->dirs.begin(), t->dirs.end(), t->workdir) != t->dirs.end();
        QVERIFY(hasGitDir);
        QVERIFY(hasWorkdir);

        std::filesystem::remove_all(dir);
    }

    void commit_files_lists_changed_paths()
    {
        const auto dir = make_repo_with_two_commits(); // helper: c1 adds a.txt, c2 adds b.txt
        auto repo      = gittide::ui::AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        auto log = QCoro::waitFor(repo->log());
        QVERIFY(log.has_value());
        const QString newest = QString::fromStdString(log->front().oid);

        auto files = QCoro::waitFor(repo->commitFiles(newest));
        QVERIFY(files.has_value());
        QVERIFY(!files->empty());
        std::filesystem::remove_all(dir);
    }

    void localOnlyOidsReportsUnpushedCommits()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("a.txt", "one\n");
        tmp.commitAll("c1");
        tmp.addBareRemote("origin");
        tmp.pushBranch("origin", "master"); // origin/master at c1
        tmp.writeFile("a.txt", "two\n");
        tmp.commitAll("c2"); // local-only

        auto repo = gittide::ui::AsyncRepo::open(tmp.path());
        QVERIFY(repo.has_value());

        auto localOnly = QCoro::waitFor(repo->localOnlyOids());
        QVERIFY(localOnly.has_value());
        QCOMPARE(localOnly->size(), size_t(1));

        auto log = QCoro::waitFor(repo->log());
        QVERIFY(log.has_value());
        QCOMPARE(localOnly->front(), log->front().oid); // the newest (c2) is the unpushed one
    }

    void stashListRoundTrips()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");

        auto repo = gittide::ui::AsyncRepo::open(tmp.path());
        QVERIFY(repo.has_value());

        tmp.writeFile("a.txt", "dirty\n");
        QVERIFY(QCoro::waitFor(repo->stashSave(QStringLiteral("wip"))).value());

        auto list = QCoro::waitFor(repo->stashList());
        QVERIFY(list.has_value());
        QCOMPARE(int(list->size()), 1);
        QCOMPARE(int((*list)[0].index), 0);

        QVERIFY(QCoro::waitFor(repo->stashPopAt(0)).has_value());
        QCOMPARE(QCoro::waitFor(repo->stashCount()).value(), 0);
    }
};

#include "test_async_repo.moc"
