#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "gittide/graph.hpp"
#include "gittide/ui/historymodel.hpp"
#include "gittide/ui/historyview.hpp"

namespace {

gittide::GraphLayout make_one_commit_layout(const std::string& oid, const std::string& summary)
{
    gittide::CommitNode c;
    c.oid     = oid;
    c.summary = summary;
    c.author  = "Alice";
    c.time    = 1000;
    c.lane    = 0;

    gittide::GraphRow r;
    r.commit        = c;
    r.lineFromAbove = false;

    gittide::GraphLayout layout;
    layout.rows      = {r};
    layout.laneCount = 1;
    return layout;
}

} // namespace

class TestHistoryView : public QObject
{
    Q_OBJECT
private slots:
    void oidForRow_returns_correct_oid()
    {
        gittide::ui::HistoryView view;
        view.setHistory(make_one_commit_layout("deadbeef1234", "Initial commit"));

        auto* model = view.findChild<gittide::ui::HistoryModel*>();
        QVERIFY(model != nullptr);
        auto idx = model->index(0, gittide::ui::HistoryModel::ColGraph);
        QCOMPARE(view.oidForRow(idx), QStringLiteral("deadbeef1234"));
    }

    void emitCheckoutFor_emits_checkoutCommitRequested_with_correct_oid()
    {
        gittide::ui::HistoryView view;
        view.setHistory(make_one_commit_layout("cafebabe5678", "Second commit"));

        auto* model = view.findChild<gittide::ui::HistoryModel*>();
        QVERIFY(model != nullptr);
        auto idx = model->index(0, gittide::ui::HistoryModel::ColGraph);

        QSignalSpy spy(&view, &gittide::ui::HistoryView::checkoutCommitRequested);
        view.emitCheckoutFor(idx);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("cafebabe5678"));
    }

    void emitNewBranchFor_emits_newBranchFromCommitRequested_with_correct_oid()
    {
        gittide::ui::HistoryView view;
        view.setHistory(make_one_commit_layout("f00d4567abcd", "Third commit"));

        auto* model = view.findChild<gittide::ui::HistoryModel*>();
        QVERIFY(model != nullptr);
        auto idx = model->index(0, gittide::ui::HistoryModel::ColGraph);

        QSignalSpy spy(&view, &gittide::ui::HistoryView::newBranchFromCommitRequested);
        view.emitNewBranchFor(idx);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("f00d4567abcd"));
    }

    void invalid_index_emits_nothing()
    {
        gittide::ui::HistoryView view;
        view.setHistory(make_one_commit_layout("abc123", "Commit"));

        QSignalSpy spyCheckout(&view, &gittide::ui::HistoryView::checkoutCommitRequested);
        QSignalSpy spyBranch(&view, &gittide::ui::HistoryView::newBranchFromCommitRequested);
        view.emitCheckoutFor(QModelIndex{});
        view.emitNewBranchFor(QModelIndex{});
        QCOMPARE(spyCheckout.count(), 0);
        QCOMPARE(spyBranch.count(), 0);
    }
};

#include "test_history_view.moc"
