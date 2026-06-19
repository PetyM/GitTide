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
    case DateRole:
        return QDateTime::fromSecsSinceEpoch(gr.commit.time).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    case OidRole:
        return oid;
    case ShortOidRole:
        return oid.left(7);
    case IsHeadRole:
        return !m_headOid.isEmpty() && oid == m_headOid;
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
        {DateRole, QByteArrayLiteral("date")},
        {OidRole, QByteArrayLiteral("oid")},
        {ShortOidRole, QByteArrayLiteral("shortOid")},
        {IsHeadRole, QByteArrayLiteral("isHead")},
    };
}

} // namespace gittide::ui
