#include <QtTest>
#include <QSignalSpy>
#include <qcorotask.h>

#include "gittide/ui/repocontroller.hpp"
#include "support/temprepo.hpp"

using namespace gittide::ui;

class TestRepoControllerDiscardAll : public QObject
{
    Q_OBJECT
private slots:
    void discard_all_clears_tracked_and_untracked()
    {
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");
        tmp.writeFile("a.txt", "dirty\n"); // modified tracked
        tmp.writeFile("b.txt", "new\n");   // untracked

        RepoController ctrl;
        QSignalSpy     statusSpy(&ctrl, &RepoController::statusChanged);
        ctrl.open(QString::fromStdString(tmp.path().generic_string()));
        QVERIFY(QTest::qWaitFor([&] { return ctrl.isOpen(); }, 2000));

        QCoro::waitFor(ctrl.discardAll());

        // Final statusChanged carries an empty list (clean tree).
        QVERIFY(QTest::qWaitFor(
            [&]
            {
                return !statusSpy.isEmpty()
                    && statusSpy.last().at(0).value<std::vector<gittide::FileStatus>>().empty();
            },
            2000));
        QVERIFY(QFile::exists(QString::fromStdString((tmp.path() / "a.txt").generic_string())));
        QVERIFY(!QFile::exists(QString::fromStdString((tmp.path() / "b.txt").generic_string())));
    }
};

#include "test_repocontroller_discard_all.moc"
