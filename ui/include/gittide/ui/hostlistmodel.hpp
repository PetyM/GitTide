#pragma once
#include <QAbstractListModel>

namespace gittide {
class CredentialsStore;
}

namespace gittide::ui {

// Read model over CredentialsStore::hosts(). Owned by CredentialManager; call
// refresh() after the host list changes.
class HostListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        HostRole,
        KindRole,
        UsernameRole,
        ApiBaseRole
    };

    explicit HostListModel(gittide::CredentialsStore* store, QObject* parent = nullptr);

    int                    rowCount(const QModelIndex& parent = {}) const override;
    QVariant               data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();

private:
    gittide::CredentialsStore* m_store;
};

} // namespace gittide::ui
