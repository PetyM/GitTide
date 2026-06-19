#include "gittide/ui/branchlistmodel.hpp"

#include <algorithm>

namespace gittide::ui {

int BranchListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_visible.size());
}

QVariant BranchListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_visible.size()))
        return {};
    const Row& r = m_visible[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case NameRole:
        return r.name;
    case SectionRole:
        return r.section;
    case IsHeadRole:
        return r.isHead;
    case UpstreamRole:
        return r.upstream;
    case WorktreePathRole:
        return r.worktreePath;
    case RemoteRole:
        return r.remote;
    default:
        return {};
    }
}

QHash<int, QByteArray> BranchListModel::roleNames() const
{
    return {
        {NameRole, "branchName"},
        {SectionRole, "section"},
        {IsHeadRole, "isHead"},
        {UpstreamRole, "upstream"},
        {WorktreePathRole, "worktreePath"},
        {RemoteRole, "remote"},
    };
}

void BranchListModel::setBranches(const std::vector<gittide::BranchInfo>& branches)
{
    m_all.clear();
    m_all.reserve(branches.size());
    for (const auto& b : branches)
    {
        Row r;
        r.name         = QString::fromStdString(b.name);
        r.isHead       = b.isHead;
        r.upstream     = QString::fromStdString(b.upstream);
        r.worktreePath = QString::fromStdString(b.worktreePath);
        r.remote       = b.kind == gittide::BranchKind::RemoteTracking;
        if (r.remote)
        {
            r.section = QStringLiteral("Remote");
            r.order   = 2;
        }
        else if (!r.worktreePath.isEmpty() && !r.isHead)
        {
            r.section = QStringLiteral("Worktrees");
            r.order   = 1;
        }
        else
        {
            r.section = QStringLiteral("Local");
            r.order   = 0;
        }
        m_all.push_back(std::move(r));
    }
    std::sort(m_all.begin(), m_all.end(), [](const Row& a, const Row& b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    rebuild();
}

QString BranchListModel::filter() const
{
    return m_filter;
}

void BranchListModel::setFilter(const QString& f)
{
    if (m_filter == f)
        return;
    m_filter = f;
    emit filterChanged();
    rebuild();
}

QString BranchListModel::nameAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_visible.size()))
        return {};
    return m_visible[static_cast<std::size_t>(row)].name;
}

QStringList BranchListModel::localBranchNames() const
{
    QStringList out;
    for (const Row& r : m_all)
        if (!r.remote)
            out.append(r.name);
    return out;
}

void BranchListModel::rebuild()
{
    beginResetModel();
    m_visible.clear();
    for (const Row& r : m_all)
        if (m_filter.isEmpty() || r.name.contains(m_filter, Qt::CaseInsensitive))
            m_visible.push_back(r);
    endResetModel();
    emit countChanged();
}

} // namespace gittide::ui
