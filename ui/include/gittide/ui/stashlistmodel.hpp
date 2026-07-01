#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "gittide/gitrepo.hpp"

namespace gittide::ui {

/// QML list model of the stash stack. One row per StashEntry (index 0 = newest).
/// Exposes three roles to QML: `label` ("stash@{<index>}"), `message`, and `oid`.
class StashListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        LabelRole   = Qt::UserRole + 1,
        MessageRole,
        OidRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const std::vector<gittide::StashEntry>& entries);

    /// Returns the OID string for the stash at @p row, or an empty string if
    /// @p row is out of range.
    Q_INVOKABLE QString oidAt(int row) const;

    /// Returns the number of stash entries currently in the model.
    int count() const;

private:
    struct Row
    {
        QString label;
        QString message;
        QString oid;
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
