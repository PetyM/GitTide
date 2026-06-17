#pragma once
#include <QAbstractItemModel>
#include <vector>

#include "gittide/projectstore.hpp"

namespace gittide::ui {

class RepoListModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles
    {
        PathRole = Qt::UserRole + 1,
        MissingRole
    };

    explicit RepoListModel(QObject* parent = nullptr);

    // QAbstractItemModel overrides
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRepos(const std::vector<gittide::RepoRef>& repos);

private:
    struct SubRow
    {
        QString path;
        QString displayName;
        bool missing;
    };
    struct Row
    {
        QString alias;
        QString path;
        bool missing;
        std::vector<SubRow> children;
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
