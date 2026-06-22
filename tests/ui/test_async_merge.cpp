#include <QtTest>
#include <qcoro/qcorotask.h>
#include "gittide/ui/asyncrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

class TestAsyncMerge : public QObject
{
    Q_OBJECT
private slots:
    void merge_conflict_reports_state()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("a.txt", "base\n");
        tmp.commitAll("base");
        auto sync = GitRepo::open(tmp.path());
        QVERIFY(sync.has_value());
        QVERIFY(sync->createBranch("feature", "").has_value());
        QVERIFY(sync->checkoutBranch("feature").has_value());
        tmp.writeFile("a.txt", "feature\n");
        tmp.commitAll("feat");
        QVERIFY(sync->checkoutBranch("master").has_value());
        tmp.writeFile("a.txt", "main\n");
        tmp.commitAll("main");

        auto async = ui::AsyncRepo::open(tmp.path());
        QVERIFY(async.has_value());
        auto out = QCoro::waitFor(async->mergeBranch("feature"));
        QVERIFY(out.has_value());
        QVERIFY(out->conflicted);
        auto ms = QCoro::waitFor(async->mergeState());
        QVERIFY(ms.has_value());
        QVERIFY(ms->inProgress);
        QCOMPARE(int(ms->conflictedPaths.size()), 1);
    }
};
#include "test_async_merge.moc"
