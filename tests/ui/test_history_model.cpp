#include <QObject>
#include <QtTest/QtTest>

#include "gittide/graph.hpp"
#include "gittide/ui/HistoryModel.hpp"

namespace {
gittide::GraphLayout make_linear_layout() {
    gittide::CommitNode c1;
    c1.oid = "aaa";
    c1.summary = "First";
    c1.author = "Alice";
    c1.time = 1000;
    c1.lane = 0;
    gittide::CommitNode c2;
    c2.oid = "bbb";
    c2.summary = "Second";
    c2.author = "Bob";
    c2.time = 2000;
    c2.lane = 0;
    gittide::GraphRow r1;
    r1.commit = c2;
    r1.lineFromAbove = false;
    r1.outEdges = {{0, 0}};
    gittide::GraphRow r2;
    r2.commit = c1;
    r2.lineFromAbove = true;
    gittide::GraphLayout layout;
    layout.rows = {r1, r2};
    layout.laneCount = 1;
    return layout;
}
}  // namespace

class TestHistoryModel : public QObject {
    Q_OBJECT
private slots:
    void empty_model_has_zero_rows() {
        gittide::ui::HistoryModel m;
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.columnCount(),
                 static_cast<int>(gittide::ui::HistoryModel::ColCount));
    }

    void setLayout_updates_row_count() {
        gittide::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.laneCount(), 1);
    }

    void display_role_summary_column() {
        gittide::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gittide::ui::HistoryModel::ColSummary);
        QCOMPARE(m.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Second"));
    }

    void display_role_author_column() {
        gittide::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gittide::ui::HistoryModel::ColAuthor);
        QCOMPARE(m.data(idx, Qt::DisplayRole).toString(), QStringLiteral("Bob"));
    }

    void display_role_date_column_non_empty() {
        gittide::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gittide::ui::HistoryModel::ColDate);
        QVERIFY(!m.data(idx, Qt::DisplayRole).toString().isEmpty());
    }

    void graph_row_role_returns_GraphRow() {
        gittide::ui::HistoryModel m;
        m.setLayout(make_linear_layout());
        auto idx = m.index(0, gittide::ui::HistoryModel::ColGraph);
        auto val = m.data(idx, gittide::ui::HistoryModel::GraphRowRole);
        QVERIFY(val.canConvert<gittide::GraphRow>());
        auto row = val.value<gittide::GraphRow>();
        QCOMPARE(row.commit.lane, 0);
        QCOMPARE(QString::fromStdString(row.commit.summary), QStringLiteral("Second"));
    }

    void header_data_returns_column_names() {
        gittide::ui::HistoryModel m;
        QCOMPARE(m.headerData(gittide::ui::HistoryModel::ColSummary, Qt::Horizontal,
                              Qt::DisplayRole)
                     .toString(),
                 QStringLiteral("Summary"));
    }
};

#include "test_history_model.moc"
