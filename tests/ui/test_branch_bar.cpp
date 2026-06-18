#include "gittide/ui/branchbar.hpp"
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

using gittide::ui::BranchBar;

class TestBranchBar : public QObject
{
    Q_OBJECT
private slots:
    void shows_branch_name()
    {
        BranchBar bar;
        bar.setHead(gittide::HeadState{"main", "abc", false, false});
        auto* btn = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn);
        QVERIFY(btn->text().contains(QStringLiteral("main")));
    }

    void shows_detached_state()
    {
        BranchBar bar;
        bar.setHead(gittide::HeadState{"", "abc1234deadbeef", true, false});
        auto* btn = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn->text().contains(QStringLiteral("detached")));
        QVERIFY(btn->text().contains(QStringLiteral("abc1234"))); // short oid
    }
};
#include "test_branch_bar.moc"
