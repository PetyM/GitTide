#include <QtTest>
#include <QSignalSpy>
#include <qcorotask.h>

#include "gittide/ui/repocontroller.hpp"
#include "support/temprepo.hpp"

using namespace gittide::ui;

class TestRepoControllerStash : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<gittide::StashEntry>();
        qRegisterMetaType<std::vector<gittide::StashEntry>>();
    }

    void stash_then_pop_round_trips_and_reports_count()
    {
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");
        tmp.writeFile("a.txt", "dirty\n");

        RepoController ctrl;
        QSignalSpy     countSpy(&ctrl, &RepoController::stashCountChanged);
        ctrl.open(QString::fromStdString(tmp.path().generic_string()));
        QVERIFY(QTest::qWaitFor([&] { return ctrl.isOpen(); }, 2000));

        QCoro::waitFor(ctrl.stashChanges());
        QVERIFY(QTest::qWaitFor(
            [&] { return !countSpy.isEmpty() && countSpy.last().at(0).toInt() == 1; }, 2000));

        QCoro::waitFor(ctrl.popStash());
        QVERIFY(QTest::qWaitFor([&] { return countSpy.last().at(0).toInt() == 0; }, 2000));

        // Changes restored after pop.
        QFile f(QString::fromStdString((tmp.path() / "a.txt").generic_string()));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("dirty\n"));
    }

    void dropRemovesChosenEntry()
    {
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");

        RepoController ctrl;
        QSignalSpy     listSpy(&ctrl, &RepoController::stashListReady);
        QSignalSpy     countSpy(&ctrl, &RepoController::stashCountChanged);
        ctrl.open(QString::fromStdString(tmp.path().generic_string()));
        QVERIFY(QTest::qWaitFor([&] { return ctrl.isOpen(); }, 2000));

        tmp.writeFile("a.txt", "x\n");
        QCoro::waitFor(ctrl.stashChanges());
        // stashChanges calls refreshStashState which emits stashListReady
        QVERIFY(QTest::qWaitFor([&] { return !listSpy.isEmpty(); }, 2000));

        auto entries = listSpy.last().at(0).value<std::vector<gittide::StashEntry>>();
        QCOMPARE(int(entries.size()), 1);

        QCoro::waitFor(ctrl.dropStash(0));
        // dropStash calls refreshStashState which emits stashCountChanged(0)
        QVERIFY(
            QTest::qWaitFor([&] { return !countSpy.isEmpty() && countSpy.last().at(0).toInt() == 0; }, 2000));
    }

    void popConflictReportsAndKeeps()
    {
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "base\n");
        tmp.commitAll("init");

        RepoController ctrl;
        QSignalSpy     failSpy(&ctrl, &RepoController::operationFailed);
        QSignalSpy     countSpy(&ctrl, &RepoController::stashCountChanged);
        ctrl.open(QString::fromStdString(tmp.path().generic_string()));
        QVERIFY(QTest::qWaitFor([&] { return ctrl.isOpen(); }, 2000));

        tmp.writeFile("a.txt", "stashed\n");
        QCoro::waitFor(ctrl.stashChanges());
        // Wait for stash count to reach 1
        QVERIFY(QTest::qWaitFor(
            [&] { return !countSpy.isEmpty() && countSpy.last().at(0).toInt() == 1; }, 2000));

        // Dirty the working tree with a conflicting change
        tmp.writeFile("a.txt", "conflicting\n");

        QCoro::waitFor(ctrl.popStashAt(0));
        // operationFailed must be emitted (conflict detected, stash preserved)
        QCOMPARE(failSpy.count(), 1);
        // The stash count must never have been emitted as 0 — the stash is preserved
        for (const auto& args : countSpy)
            QVERIFY(args.at(0).toInt() >= 1);
    }
};

#include "test_repocontroller_stash.moc"
