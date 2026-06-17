#include "gittide/ui/repolistmodel.hpp"

#include <filesystem>

#include "gittide/gitrepo.hpp"

namespace gittide::ui {

RepoListModel::RepoListModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

void RepoListModel::setRepos(const std::vector<gittide::RepoRef>& repos)
{
    beginResetModel();
    rows_.clear();
    rows_.reserve(repos.size());
    for (const auto& r : repos)
    {
        const std::filesystem::path p(r.path);
        std::error_code ec;
        const bool present = std::filesystem::exists(p, ec) && !ec;
        Row row{
            .alias   = QString::fromStdString(r.alias),
            .path    = QString::fromStdString(r.path),
            .missing = !present,
        };
        if (present)
        {
            auto repo = gittide::GitRepo::open(p);
            if (repo)
            {
                auto subs = repo->submodules();
                if (subs)
                {
                    for (const auto& subPath : *subs)
                    {
                        std::error_code sec;
                        bool subPresent = std::filesystem::exists(subPath, sec) && !sec;
                        row.children.push_back(SubRow{
                            .path        = QString::fromStdString(subPath.generic_string()),
                            .displayName = QString::fromStdString(subPath.filename().string()),
                            .missing     = !subPresent,
                        });
                    }
                }
            }
        }
        rows_.push_back(std::move(row));
    }
    endResetModel();
}

QModelIndex RepoListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0)
        return {};
    if (!parent.isValid())
    {
        if (row < 0 || row >= static_cast<int>(rows_.size()))
            return {};
        return createIndex(row, 0, nullptr);
    }
    // Child of a top-level item (submodule). Depth is max 1.
    if (parent.internalPointer() != nullptr)
        return {};
    const int pr = parent.row();
    if (pr < 0 || pr >= static_cast<int>(rows_.size()))
        return {};
    if (row < 0 || row >= static_cast<int>(rows_[pr].children.size()))
        return {};
    return createIndex(row, 0, reinterpret_cast<void*>(quintptr(pr + 1)));
}

QModelIndex RepoListModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    const auto ptr = reinterpret_cast<quintptr>(child.internalPointer());
    if (ptr == 0)
        return {};
    return createIndex(static_cast<int>(ptr - 1), 0, nullptr);
}

int RepoListModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return static_cast<int>(rows_.size());
    if (parent.internalPointer() != nullptr)
        return 0;
    const int row = parent.row();
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return 0;
    return static_cast<int>(rows_[row].children.size());
}

int RepoListModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid() && parent.internalPointer() != nullptr)
        return 0;
    return 1;
}

QVariant RepoListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const auto ptr = reinterpret_cast<quintptr>(index.internalPointer());
    if (ptr == 0)
    {
        const auto row = static_cast<std::size_t>(index.row());
        if (row >= rows_.size())
            return {};
        const auto& r = rows_[row];
        switch (role)
        {
        case Qt::DisplayRole:
            return r.alias.isEmpty() ? r.path : r.alias;
        case PathRole:
            return r.path;
        case MissingRole:
            return r.missing;
        default:
            return {};
        }
    }
    else
    {
        const int pr = static_cast<int>(ptr - 1);
        const int cr = index.row();
        if (pr < 0 || pr >= static_cast<int>(rows_.size()))
            return {};
        if (cr < 0 || cr >= static_cast<int>(rows_[pr].children.size()))
            return {};
        const auto& sub = rows_[pr].children[cr];
        switch (role)
        {
        case Qt::DisplayRole:
            return sub.displayName;
        case PathRole:
            return sub.path;
        case MissingRole:
            return sub.missing;
        default:
            return {};
        }
    }
}

QHash<int, QByteArray> RepoListModel::roleNames() const
{
    auto roles         = QAbstractItemModel::roleNames();
    roles[PathRole]    = "repoPath";
    roles[MissingRole] = "missing";
    return roles;
}

} // namespace gittide::ui
