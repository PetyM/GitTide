#include <QtTest>
#include <QAbstractItemModel>
#include <QVariantList>
#include <QVariantMap>

#include "gittide/graph.hpp"
#include "gittide/ui/historylistmodel.hpp"

using gittide::CommitNode;
using gittide::GraphLayout;
using gittide::GraphRow;
using gittide::RefTipKind;
using gittide::ui::HistoryListModel;

class TestHistoryModelRefLabels : public QObject
{
    Q_OBJECT
private slots:
    void reflabels_carry_name_and_kind()
    {
        GraphLayout layout;
        CommitNode n;
        n.oid     = "abc123";
        n.summary = "tip";
        layout.rows.push_back(GraphRow{n, false, {}, {}});
        layout.laneCount = 1;

        HistoryListModel m;
        m.setLayout(layout, QString());

        // Each chip carries {name, kind} so the graph can style a local branch,
        // a remote-tracking ref and a tag distinctly (kind mirrors RefTipKind).
        QHash<QString, QVariantList> tips;
        tips.insert(QStringLiteral("abc123"),
                    QVariantList{
                        QVariantMap{{QStringLiteral("name"), QStringLiteral("main")},
                                    {QStringLiteral("kind"), int(RefTipKind::Branch)}},
                        QVariantMap{{QStringLiteral("name"), QStringLiteral("origin/main")},
                                    {QStringLiteral("kind"), int(RefTipKind::Remote)}},
                        QVariantMap{{QStringLiteral("name"), QStringLiteral("v1.0")},
                                    {QStringLiteral("kind"), int(RefTipKind::Tag)}}});
        m.setRefTips(tips);

        const QModelIndex idx    = m.index(0, 0);
        const QVariantList chips = m.data(idx, HistoryListModel::RefLabelsRole).toList();
        QCOMPARE(chips.size(), 3);

        // Collect name → kind for order-independent assertions.
        QHash<QString, int> byName;
        for (const QVariant& c : chips)
        {
            const QVariantMap chip = c.toMap();
            byName.insert(chip.value(QStringLiteral("name")).toString(),
                          chip.value(QStringLiteral("kind")).toInt());
        }
        QCOMPARE(byName.value(QStringLiteral("main")),        int(RefTipKind::Branch));
        QCOMPARE(byName.value(QStringLiteral("origin/main")), int(RefTipKind::Remote));
        QCOMPARE(byName.value(QStringLiteral("v1.0")),        int(RefTipKind::Tag));
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
        const QVariantList chips = m.data(idx, HistoryListModel::RefLabelsRole).toList();
        QVERIFY(chips.isEmpty());
    }
};

#include "test_history_model_reflabels.moc"
