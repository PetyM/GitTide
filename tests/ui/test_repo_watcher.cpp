#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>
#include <fstream>

#include "gittide/gitrepo.hpp"
#include "gittide/ui/repowatcher.hpp"
#include "gittide/watch.hpp"
#include "support/temprepo.hpp"

using gittide::ui::RepoWatcher;

namespace {
// Build the watch targets for a fresh repo with one committed file.
gittide::WatchTargets targetsFor(gittide::test::TempRepo& repo)
{
    repo.writeFile("a.txt", "hello");
    repo.commitAll("init");
    auto r = gittide::GitRepo::open(repo.path());
    auto t = r->watchTargets();
    return *t;
}
} // namespace

class TestRepoWatcher : public QObject
{
    Q_OBJECT
private slots:
    void worktree_edit_emits_worktree_scope()
    {
        gittide::test::TempRepo repo;
        const auto targets = targetsFor(repo);

        RepoWatcher watcher(30);
        watcher.watch(targets);
        // Drain residual FSEvents from the commitAll setup (which wrote .git).
        QTest::qWait(400);
        QSignalSpy workSpy(&watcher, &RepoWatcher::worktreeChanged);

        // A new file in the working tree is a directory change in the watched root.
        std::ofstream(targets.workdir / "b.txt") << "new\n";

        // The worktree edit produces a worktree-scope signal. (We deliberately do
        // not assert the *absence* of a gitDir signal: the OS batches unrelated
        // .git setup churn non-deterministically, which would make that flaky.)
        QVERIFY(workSpy.wait(15000));
    }

    // An in-place content edit of an existing file is missed by directory watches
    // (Linux inotify dir-watch ignores child IN_MODIFY). setActiveFile adds a
    // per-file watch so the on-screen diff still refreshes.
    void active_file_content_edit_emits_worktree_scope()
    {
        gittide::test::TempRepo repo;
        const auto targets = targetsFor(repo); // commits a.txt

        RepoWatcher watcher(30);
        watcher.watch(targets);
        watcher.setActiveFile(QString::fromStdString((targets.workdir / "a.txt").generic_string()));
        QTest::qWait(400); // drain residual setup FSEvents
        QSignalSpy workSpy(&watcher, &RepoWatcher::worktreeChanged);

        // Overwrite a.txt in place — no directory-listing change, pure content edit.
        {
            std::ofstream(targets.workdir / "a.txt") << "hello changed\n";
        }

        QVERIFY(workSpy.wait(15000));
    }

    void gitdir_change_emits_gitdir_scope()
    {
        gittide::test::TempRepo repo;
        const auto targets = targetsFor(repo);

        RepoWatcher watcher(30);
        watcher.watch(targets);
        QTest::qWait(400); // drain residual setup FSEvents
        QSignalSpy gitSpy(&watcher, &RepoWatcher::gitDirChanged);

        // A new entry under .git (as an external commit/checkout would produce).
        std::ofstream(targets.gitDir / "gittide-probe") << "x\n";

        QVERIFY(gitSpy.wait(15000));
    }

    void burst_coalesces_into_one_emission()
    {
        gittide::test::TempRepo repo;
        const auto targets = targetsFor(repo);

        RepoWatcher watcher(50);
        watcher.watch(targets);
        QTest::qWait(400); // drain residual setup FSEvents
        QSignalSpy workSpy(&watcher, &RepoWatcher::worktreeChanged);

        for (int i = 0; i < 5; ++i)
            std::ofstream(targets.workdir / ("burst" + std::to_string(i) + ".txt")) << "x\n";

        QVERIFY(workSpy.wait(15000));
        // Let any straggler debounce windows elapse, then assert it coalesced.
        QTest::qWait(200);
        QCOMPARE(workSpy.count(), 1);
    }

    void mute_suppresses_emissions()
    {
        gittide::test::TempRepo repo;
        const auto targets = targetsFor(repo);

        RepoWatcher watcher(30);
        watcher.watch(targets);
        QTest::qWait(400); // drain residual setup FSEvents
        QSignalSpy workSpy(&watcher, &RepoWatcher::worktreeChanged);

        watcher.mute();
        std::ofstream(targets.workdir / "c.txt") << "muted\n";

        QTest::qWait(800);
        QCOMPARE(workSpy.count(), 0);
    }
};

#include "test_repo_watcher.moc"
