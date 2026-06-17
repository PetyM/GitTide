#include <gittide/ui/ProjectListModel.hpp>
#include <gittide/ProjectStore.hpp>

namespace gittide::ui {

ProjectListModel::ProjectListModel(gittide::ProjectStore* store, QObject* parent)
    : QAbstractListModel(parent), store_(store) {}

int ProjectListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(store_->projects().size());
}

QVariant ProjectListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= store_->projects().size()) return {};
    const auto& p = store_->projects()[row];
    switch (role) {
        case Qt::DisplayRole: return QString::fromStdString(p.name);
        case IdRole:          return QString::fromStdString(p.id);
        default:              return {};
    }
}

QHash<int, QByteArray> ProjectListModel::roleNames() const {
    auto roles = QAbstractListModel::roleNames();
    roles[IdRole] = "projectId";
    return roles;
}

void ProjectListModel::refresh() {
    beginResetModel();
    endResetModel();
}

}  // namespace gittide::ui
