#include <gittide/credentialsstore.hpp>
#include <gittide/ui/identitylistmodel.hpp>

namespace gittide::ui {

IdentityListModel::IdentityListModel(gittide::CredentialsStore* store, QObject* parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
}

int IdentityListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_store->identities().size());
}

QVariant IdentityListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= m_store->identities().size())
        return {};
    const auto& id = m_store->identities()[row];
    switch (role)
    {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(id.name);
    case IdRole:
        return QString::fromStdString(id.id);
    case EmailRole:
        return QString::fromStdString(id.email);
    case IsGlobalRole:
        return id.id == m_store->globalIdentity();
    default:
        return {};
    }
}

QHash<int, QByteArray> IdentityListModel::roleNames() const
{
    auto roles         = QAbstractListModel::roleNames();
    roles[IdRole]       = "identityId";
    roles[NameRole]     = "name";
    roles[EmailRole]    = "email";
    roles[IsGlobalRole] = "isGlobal";
    return roles;
}

void IdentityListModel::refresh()
{
    beginResetModel();
    endResetModel();
}

} // namespace gittide::ui
