#include "gitgui/ui/RepoListModel.hpp"
#include <filesystem>

namespace gitgui::ui {

RepoListModel::RepoListModel(QObject* parent) : QAbstractItemModel(parent) {}

void RepoListModel::setRepos(const std::vector<gitgui::RepoRef>& repos) {
    beginResetModel();
    rows_.clear();
    rows_.reserve(repos.size());
    for (const auto& r : repos) {
        const std::filesystem::path p = std::filesystem::path(r.path);
        std::error_code ec;
        const bool present = std::filesystem::exists(p, ec) && !ec;
        rows_.push_back(Row{
            .alias = QString::fromStdString(r.alias),
            .path  = QString::fromStdString(r.path),
            .missing = !present,
        });
    }
    endResetModel();
}

QModelIndex RepoListModel::index(int row, int column,
                                  const QModelIndex& parent) const {
    if (parent.isValid()) return {};
    if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
    if (column != 0) return {};
    return createIndex(row, column, nullptr);
}

QModelIndex RepoListModel::parent(const QModelIndex&) const {
    return {};
}

int RepoListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

int RepoListModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return 1;
}

QVariant RepoListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= rows_.size()) return {};
    const auto& r = rows_[row];
    switch (role) {
        case Qt::DisplayRole: return r.alias.isEmpty() ? r.path : r.alias;
        case PathRole:        return r.path;
        case MissingRole:     return r.missing;
        default:              return {};
    }
}

QHash<int, QByteArray> RepoListModel::roleNames() const {
    auto roles = QAbstractItemModel::roleNames();
    roles[PathRole]   = "repoPath";
    roles[MissingRole] = "missing";
    return roles;
}

}  // namespace gitgui::ui
