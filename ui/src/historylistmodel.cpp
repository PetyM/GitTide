#include "gittide/ui/historylistmodel.hpp"

#include <QDateTime>

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

void HistoryListModel::setLayout(const gittide::GraphLayout& layout, const QString& headOid)
{
    beginResetModel();
    m_layout = layout;
    m_headOid = headOid;
    endResetModel();
    emit changed();
}

void HistoryListModel::setLocalBranchTips(const QHash<QString, QString>& oidToName)
{
    m_oidToLocalBranch = oidToName;
    if (!m_layout.rows.empty())
    {
        emit dataChanged(index(0, 0),
                         index(static_cast<int>(m_layout.rows.size()) - 1, 0),
                         {LocalBranchNameRole});
    }
}

void HistoryListModel::setRefTips(const QHash<QString, QStringList>& oidToLabels)
{
    m_oidToRefLabels = oidToLabels;
    if (!m_layout.rows.empty())
    {
        emit dataChanged(index(0, 0),
                         index(static_cast<int>(m_layout.rows.size()) - 1, 0),
                         {RefLabelsRole});
    }
}

void HistoryListModel::setLocalOnlyOids(const QSet<QString>& oids)
{
    m_localOnly = oids;
    if (!m_layout.rows.empty())
    {
        emit dataChanged(index(0, 0),
                         index(static_cast<int>(m_layout.rows.size()) - 1, 0),
                         {IsLocalOnlyRole});
    }
}

int HistoryListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_layout.rows.size());
}

QVariant HistoryListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= m_layout.rows.size())
        return {};
    const auto& gr  = m_layout.rows[row];
    const QString oid = QString::fromStdString(gr.commit.oid);

    switch (role)
    {
    case GraphRole:
        return QVariant::fromValue(gr);
    case SummaryRole:
        return QString::fromStdString(gr.commit.summary);
    case AuthorRole:
        return QString::fromStdString(gr.commit.author);
    case AuthorEmailRole:
        return QString::fromStdString(gr.commit.email);
    case DateRole:
        return QDateTime::fromSecsSinceEpoch(gr.commit.time).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    case OidRole:
        return oid;
    case ShortOidRole:
        return oid.left(7);
    case IsHeadRole:
        return !m_headOid.isEmpty() && oid == m_headOid;
    case LocalBranchNameRole:
        return m_oidToLocalBranch.value(oid);
    case RefLabelsRole:
        return m_oidToRefLabels.value(oid);
    case IsLocalOnlyRole:
        return m_localOnly.contains(oid);
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryListModel::roleNames() const
{
    return {
        {GraphRole, QByteArrayLiteral("graphRow")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {AuthorRole, QByteArrayLiteral("author")},
        {AuthorEmailRole, QByteArrayLiteral("authorEmail")},
        {DateRole, QByteArrayLiteral("date")},
        {OidRole, QByteArrayLiteral("oid")},
        {ShortOidRole, QByteArrayLiteral("shortOid")},
        {IsHeadRole, QByteArrayLiteral("isHead")},
        {LocalBranchNameRole, QByteArrayLiteral("localBranchName")},
        {RefLabelsRole, QByteArrayLiteral("refLabels")},
        {IsLocalOnlyRole, QByteArrayLiteral("isLocalOnly")},
    };
}

} // namespace gittide::ui
