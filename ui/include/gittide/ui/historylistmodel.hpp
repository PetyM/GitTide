#pragma once
#include <QAbstractListModel>
#include <QString>

#include "gittide/graph.hpp"

namespace gittide::ui {

/// QML list model backing the History tab. One row per GraphRow from a
/// GraphLayout (Plan 5a / GraphBuilder). Unlike the QWidget-era HistoryModel
/// (a table model painted by GraphDelegate), this is a single-column list whose
/// roles feed a QML ListView delegate directly: graphRow carries the GraphRow
/// for GraphColumn to paint; the rest are pre-formatted display strings. Knows
/// nothing about colour — the delegate maps lane index → theme.laneColors.
class HistoryListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int laneCount READ laneCount NOTIFY changed)
public:
    enum Roles
    {
        GraphRole = Qt::UserRole + 1, // QVariant<gittide::GraphRow>
        SummaryRole,
        AuthorRole,
        DateRole,     // pre-formatted "yyyy-MM-dd hh:mm"
        OidRole,      // full 40-char SHA
        ShortOidRole, // first 7 chars
        IsHeadRole,   // true when oid == the layout's HEAD oid
    };

    using QAbstractListModel::QAbstractListModel;

    /// Replace all rows. headOid is the full SHA of HEAD; the matching row's
    /// IsHeadRole is true (drives the white HEAD node in the graph).
    void setLayout(const gittide::GraphLayout& layout, const QString& headOid);

    int laneCount() const
    {
        return m_layout.laneCount;
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void changed();

private:
    gittide::GraphLayout m_layout;
    QString              m_headOid;
};

} // namespace gittide::ui
