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
};

#include "test_repocontroller_stash.moc"
