#include "gittide/ui/stashlistmodel.hpp"

#include <QString>

namespace gittide::ui {

int StashListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant StashListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const Row& row = m_rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case LabelRole:   return row.label;
    case MessageRole: return row.message;
    case OidRole:     return row.oid;
    default:          return {};
    }
}

QHash<int, QByteArray> StashListModel::roleNames() const
{
    return {
        {LabelRole,   "label"},
        {MessageRole, "message"},
        {OidRole,     "oid"},
    };
}

void StashListModel::setEntries(const std::vector<gittide::StashEntry>& entries)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(entries.size());
    for (const auto& e : entries)
    {
        m_rows.push_back({
            QStringLiteral("stash@{%1}").arg(static_cast<qulonglong>(e.index)),
            QString::fromStdString(e.message),
            QString::fromStdString(e.oid),
        });
    }
    endResetModel();
}

QString StashListModel::oidAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return {};
    return m_rows[static_cast<std::size_t>(row)].oid;
}

int StashListModel::count() const
{
    return static_cast<int>(m_rows.size());
}

} // namespace gittide::ui
