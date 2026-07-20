#include <gittide/credentialsstore.hpp>
#include <gittide/ui/hostlistmodel.hpp>

namespace gittide::ui {

HostListModel::HostListModel(gittide::CredentialsStore* store, QObject* parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
}

int HostListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_store->hosts().size());
}

QVariant HostListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= m_store->hosts().size())
        return {};
    const auto& h = m_store->hosts()[row];
    switch (role)
    {
    case Qt::DisplayRole:
    case HostRole:
        return QString::fromStdString(h.host);
    case IdRole:
        return QString::fromStdString(h.id);
    case KindRole:
        return QString::fromStdString(h.kind);
    case UsernameRole:
        return QString::fromStdString(h.username);
    case ApiBaseRole:
        return QString::fromStdString(h.apiBase);
    default:
        return {};
    }
}

QHash<int, QByteArray> HostListModel::roleNames() const
{
    auto roles          = QAbstractListModel::roleNames();
    roles[IdRole]       = "hostId";
    roles[HostRole]     = "host";
    roles[KindRole]     = "kind";
    roles[UsernameRole] = "username";
    roles[ApiBaseRole]  = "apiBase";
    return roles;
}

void HostListModel::refresh()
{
    beginResetModel();
    endResetModel();
}

} // namespace gittide::ui
