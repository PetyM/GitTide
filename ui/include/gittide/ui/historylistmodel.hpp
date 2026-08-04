#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
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
        AuthorEmailRole,    // author email; drives avatar resolution (may be empty)
        DateRole,           // pre-formatted "yyyy-MM-dd hh:mm"
        OidRole,            // full 40-char SHA
        ShortOidRole,       // first 7 chars
        IsHeadRole,         // true when oid == the layout's HEAD oid
        LocalBranchNameRole, // short name of a local branch whose tip is this commit; empty otherwise
        RefLabelsRole,       // QVariantList of {name, kind} maps for refs tipped here; graph chips
        IsLocalOnlyRole,     // true when this commit is not yet on any remote (unpushed)
    };

    using QAbstractListModel::QAbstractListModel;

    /// Replace all rows. headOid is the full SHA of HEAD; the matching row's
    /// IsHeadRole is true (drives the white HEAD node in the graph).
    void setLayout(const gittide::GraphLayout& layout, const QString& headOid);

    /// Update the oid → local-branch-name map used by LocalBranchNameRole.
    /// Call after setLayout or whenever branches change.
    void setLocalBranchTips(const QHash<QString, QString>& oidToName);

    /// Update the oid → ref-chip-list map used by RefLabelsRole (graph chips).
    /// Each chip is a QVariantMap {"name": QString, "kind": int} where kind
    /// mirrors gittide::RefTipKind (0 Branch, 1 Remote, 2 Tag) so the QML can
    /// style local branches, remote-tracking refs and tags distinctly.
    void setRefTips(const QHash<QString, QVariantList>& oidToChips);

    /// Update the set of local-only (unpushed) commit OIDs used by IsLocalOnlyRole.
    /// Call after setLayout or whenever what is pushed changes (fetch/pull/push).
    void setLocalOnlyOids(const QSet<QString>& oids);

    int laneCount() const
    {
        return m_layout.laneCount;
    }

    /// Row carrying commit @p oid, or -1 when it is not in this layout. Backs the
    /// view model's selectedCommitRow / selectedGraphRow, which the History and
    /// Graph lists bind their highlight to. Linear scan; the layout is capped at
    /// the history limit (1000 rows) and this runs once per selection change.
    int rowForOid(const QString& oid) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void changed();

private:
    gittide::GraphLayout      m_layout;
    QString                   m_headOid;
    QHash<QString, QString>   m_oidToLocalBranch; // tip oid → local branch name
    QHash<QString, QVariantList> m_oidToRefLabels; // tip oid → [{name, kind} chips]
    QSet<QString>             m_localOnly;        // oids of unpushed commits
};

} // namespace gittide::ui
