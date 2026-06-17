#include "gitgui/ui/DashboardModel.hpp"
#include "gitgui/GitRepo.hpp"
#include <filesystem>
#include <QtConcurrent>
#include <core/qcorofuture.h>
#include <vector>

namespace gitgui::ui {

DashboardModel::DashboardModel(QObject* parent) : QAbstractListModel(parent) {}

void DashboardModel::refresh(const std::vector<gitgui::RepoRef>& repos) {
    beginResetModel();
    rows_.clear();
    rows_.reserve(repos.size());
    for (const auto& r : repos) {
        Row row{
            .alias = QString::fromStdString(r.alias),
            .path = QString::fromStdString(r.path),
            .changeCount = 0,
            .missing = false,
        };
        auto repo = gitgui::GitRepo::open(std::filesystem::path(r.path));
        if (!repo) {
            row.missing = true;
        } else if (auto status = repo->status()) {
            row.changeCount = static_cast<int>(status->size());
        } else {
            row.missing = true;  // opened but status failed -> treat as unavailable
        }
        rows_.push_back(std::move(row));
    }
    endResetModel();
}

QCoro::Task<void> DashboardModel::refreshAsync(std::vector<gitgui::RepoRef> repos) {
    std::vector<QFuture<Row>> futures;
    futures.reserve(repos.size());
    for (const auto& r : repos) {
        futures.push_back(QtConcurrent::run([r]() -> Row {
            Row row{
                .alias = QString::fromStdString(r.alias),
                .path = QString::fromStdString(r.path),
                .changeCount = 0,
                .missing = false,
            };
            auto repo = gitgui::GitRepo::open(std::filesystem::path(r.path));
            if (!repo) {
                row.missing = true;
            } else if (auto status = repo->status()) {
                row.changeCount = static_cast<int>(status->size());
            } else {
                row.missing = true;
            }
            return row;
        }));
    }

    std::vector<Row> rows;
    rows.reserve(futures.size());
    for (auto& f : futures) {
        rows.push_back(co_await f);
    }

    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    emit refreshed();
}

int DashboardModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

QVariant DashboardModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= rows_.size()) return {};
    const auto& r = rows_[row];
    switch (role) {
        case Qt::DisplayRole:  return r.alias.isEmpty() ? r.path : r.alias;
        case PathRole:         return r.path;
        case ChangeCountRole:  return r.changeCount;
        case MissingRole:      return r.missing;
        default:               return {};
    }
}

QHash<int, QByteArray> DashboardModel::roleNames() const {
    auto roles = QAbstractListModel::roleNames();
    roles[PathRole] = "repoPath";
    roles[ChangeCountRole] = "changeCount";
    roles[MissingRole] = "missing";
    return roles;
}

}  // namespace gitgui::ui
