#include <gittide/projectstore.hpp>
#include <gittide/ui/projectlistmodel.hpp>

namespace gittide::ui {

ProjectListModel::ProjectListModel(gittide::ProjectStore* store, QObject* parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
}

int ProjectListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_store->projects().size());
}

QVariant ProjectListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= m_store->projects().size())
        return {};
    const auto& p = m_store->projects()[row];
    switch (role)
    {
    case Qt::DisplayRole:
        return QString::fromStdString(p.name);
    case IdRole:
        return QString::fromStdString(p.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> ProjectListModel::roleNames() const
{
    auto roles    = QAbstractListModel::roleNames();
    roles[IdRole] = "projectId";
    return roles;
}

void ProjectListModel::refresh()
{
    beginResetModel();
    endResetModel();
}

} // namespace gittide::ui
