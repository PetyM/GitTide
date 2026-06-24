#include <QtTest>
#include <qcoro/qcorotask.h>
#include "gittide/ui/asyncrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

class TestAsyncRebase : public QObject
{
    Q_OBJECT
private slots:
    void start_clean_then_state_idle()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("base.txt", "base\n");
        tmp.commitAll("c0");

        // Create feature branch diverged from master on a different file (no conflict).
        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r.has_value());
            QVERIFY(r->createBranch("feature", "").has_value());
            QVERIFY(r->checkoutBranch("feature").has_value());
        }
        tmp.writeFile("f.txt", "feature\n");
        tmp.commitAll("c1");

        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r.has_value());
            QVERIFY(r->checkoutBranch("master").has_value());
        }
        tmp.writeFile("m.txt", "main\n");
        tmp.commitAll("c2");

        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r.has_value());
            QVERIFY(r->checkoutBranch("feature").has_value());
        }

        auto async = ui::AsyncRepo::open(tmp.path());
        QVERIFY(async.has_value());

        auto out = QCoro::waitFor(async->startRebase("master"));
        QVERIFY(out.has_value());
        QVERIFY(!out->conflicted);

        auto st = QCoro::waitFor(async->rebaseState());
        QVERIFY(st.has_value());
        QVERIFY(!st->inProgress);
    }
};
#include "test_async_rebase.moc"
