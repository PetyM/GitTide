#include <QtTest>
#include <QAbstractItemModel>
#include <QQmlEngine>
#include <QQmlComponent>
#include <memory>

#include "gittide/graph.hpp"
#include "gittide/ui/historylistmodel.hpp"
#include "gittide/ui/graphcolumn.hpp"
#include "gittide/ui/qmlcontext.hpp"

using namespace gittide::ui;

namespace {
gittide::GraphLayout twoRowLayout()
{
    // Row 0: child (HEAD) at lane 0 with one out-edge to its parent (row 1).
    // Row 1: parent at lane 0, initial commit (no out-edges, line from above).
    gittide::GraphRow head;
    head.commit.oid     = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    head.commit.summary = "second";
    head.commit.author  = "Ada";
    head.commit.time    = 0;
    head.commit.lane    = 0;
    head.lineFromAbove  = false;
    head.outEdges       = {gittide::GraphEdge{0, 0}};

    gittide::GraphRow base;
    base.commit.oid     = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    base.commit.summary = "first";
    base.commit.author  = "Ada";
    base.commit.time    = 0;
    base.commit.lane    = 0;
    base.lineFromAbove  = true;

    gittide::GraphLayout layout;
    layout.rows      = {head, base};
    layout.laneCount = 1;
    return layout;
}
}

class TestQmlHistory : public QObject
{
    Q_OBJECT
private slots:
    void model_exposes_history_rows_via_roles()
    {
        HistoryListModel model;
        model.setLayout(twoRowLayout(), QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

        QCOMPARE(model.rowCount(QModelIndex()), 2);
        QCOMPARE(model.laneCount(), 1);

        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.data(top, HistoryListModel::SummaryRole).toString(), QStringLiteral("second"));
        QCOMPARE(model.data(top, HistoryListModel::AuthorRole).toString(), QStringLiteral("Ada"));
        QCOMPARE(model.data(top, HistoryListModel::ShortOidRole).toString(), QStringLiteral("aaaaaaa"));
        QCOMPARE(model.data(top, HistoryListModel::IsHeadRole).toBool(), true);
        QVERIFY(model.data(top, HistoryListModel::GraphRole).canConvert<gittide::GraphRow>());

        const QModelIndex bottom = model.index(1, 0);
        QCOMPARE(model.data(bottom, HistoryListModel::IsHeadRole).toBool(), false);

        // QML role names are present and spelled as the delegates expect.
        const auto names = model.roleNames();
        QCOMPARE(names.value(HistoryListModel::SummaryRole), QByteArrayLiteral("summary"));
        QCOMPARE(names.value(HistoryListModel::GraphRole), QByteArrayLiteral("graphRow"));
        QCOMPARE(names.value(HistoryListModel::IsHeadRole), QByteArrayLiteral("isHead"));
    }

    void graph_column_unpacks_row_and_sizes_to_lane_count()
    {
        GraphColumn item;

        gittide::GraphRow gr;
        gr.commit.oid  = "cccccccccccccccccccccccccccccccccccccccc";
        gr.commit.lane = 1;
        item.setGraphRow(QVariant::fromValue(gr));
        item.setLaneCount(3);

        QCOMPARE(item.laneCount(), 3);
        // implicitWidth tracks lane count so the ListView can reserve a fixed gutter.
        QCOMPARE(item.implicitWidth(), qreal(3 * GraphColumn::kLaneWidth));

        // The QML type is registered under the GitTide module.
        registerQmlTypes();
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData("import GitTide 1.0\nGraphColumn { laneCount: 2 }", QUrl());
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QCOMPARE(obj->property("laneCount").toInt(), 2);
    }
};

#include "test_qml_history.moc"
