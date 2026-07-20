#pragma once
#include <QAbstractListModel>

namespace gittide {
class CredentialsStore;
}

namespace gittide::ui {

// Read model over CredentialsStore::identities(). The store is owned by
// CredentialManager; this model only references it. Call refresh() after the
// underlying identity list (or the global assignment) changes.
class IdentityListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        EmailRole,
        IsGlobalRole
    };

    explicit IdentityListModel(gittide::CredentialsStore* store, QObject* parent = nullptr);

    int                    rowCount(const QModelIndex& parent = {}) const override;
    QVariant               data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();

private:
    gittide::CredentialsStore* m_store;
};

} // namespace gittide::ui
