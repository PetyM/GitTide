#include <QtTest>
#include <QAbstractItemModel>
#include <QStringList>

#include "gittide/graph.hpp"
#include "gittide/ui/historylistmodel.hpp"

using gittide::CommitNode;
using gittide::GraphLayout;
using gittide::GraphRow;
using gittide::ui::HistoryListModel;

class TestHistoryModelRefLabels : public QObject
{
    Q_OBJECT
private slots:
    void reflabels_at_tip_oid()
    {
        GraphLayout layout;
        CommitNode n;
        n.oid     = "abc123";
        n.summary = "tip";
        layout.rows.push_back(GraphRow{n, false, {}, {}});
        layout.laneCount = 1;

        HistoryListModel m;
        m.setLayout(layout, QString());

        QHash<QString, QStringList> tips;
        tips.insert(QStringLiteral("abc123"), QStringList{QStringLiteral("main"), QStringLiteral("v1.0")});
        m.setRefTips(tips);

        const QModelIndex idx    = m.index(0, 0);
        const QStringList labels = m.data(idx, HistoryListModel::RefLabelsRole).toStringList();
        QVERIFY(labels.contains(QStringLiteral("main")));
        QVERIFY(labels.contains(QStringLiteral("v1.0")));
    }

    void reflabels_missing_oid_returns_empty()
    {
        // A model that never had setRefTips called must return an empty list.
        GraphLayout layout;
        CommitNode n;
        n.oid     = "def456";
        n.summary = "not a tip";
        layout.rows.push_back(GraphRow{n, false, {}, {}});
        layout.laneCount = 1;

        HistoryListModel m;
        m.setLayout(layout, QString());
        // intentionally no setRefTips call

        const QModelIndex idx    = m.index(0, 0);
        const QStringList labels = m.data(idx, HistoryListModel::RefLabelsRole).toStringList();
        QVERIFY(labels.isEmpty());
    }
};

#include "test_history_model_reflabels.moc"
