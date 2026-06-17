#include <QObject>
#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>
#include <git2.h>
#include <qcorotask.h>

#include "gittide/filestatus.hpp"
#include "gittide/ui/asyncrepo.hpp"

using gittide::ui::AsyncRepo;

namespace {
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
        QVERIFY(gittide::has_flag((*result)[0].flags, gittide::StatusFlag::WtModified));

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
        QVERIFY(gittide::has_flag((*result)[0].flags, gittide::StatusFlag::IndexModified));

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
};

#include "test_async_repo.moc"
