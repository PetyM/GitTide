#include <gittide/credentialsstore.hpp>
#include <gittide/ui/sshkeylistmodel.hpp>

namespace gittide::ui {

SshKeyListModel::SshKeyListModel(gittide::CredentialsStore* store, QObject* parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
}

int SshKeyListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_store->sshKeys().size());
}

QVariant SshKeyListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= m_store->sshKeys().size())
        return {};
    const auto& k = m_store->sshKeys()[row];
    switch (role)
    {
    case Qt::DisplayRole:
    case LabelRole:
        return QString::fromStdString(k.label);
    case IdRole:
        return QString::fromStdString(k.id);
    case PrivateKeyPathRole:
        return QString::fromStdString(k.privateKeyPath);
    case HasPassphraseRole:
        return k.hasPassphrase;
    default:
        return {};
    }
}

QHash<int, QByteArray> SshKeyListModel::roleNames() const
{
    auto roles                 = QAbstractListModel::roleNames();
    roles[IdRole]              = "sshKeyId";
    roles[LabelRole]           = "label";
    roles[PrivateKeyPathRole]  = "privateKeyPath";
    roles[HasPassphraseRole]   = "hasPassphrase";
    return roles;
}

void SshKeyListModel::refresh()
{
    beginResetModel();
    endResetModel();
}

} // namespace gittide::ui
