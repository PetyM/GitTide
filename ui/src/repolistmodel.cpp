#include "gittide/ui/repolistmodel.hpp"

#include <filesystem>

#include "gittide/gitrepo.hpp"

namespace gittide::ui {

RepoListModel::RepoListModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

void RepoListModel::appendSubmodules(Node& parent, const std::vector<gittide::SubmoduleNode>& subs)
{
    for (const auto& s : subs)
    {
        auto node         = std::make_unique<Node>();
        node->displayName = QString::fromStdString(s.name);
        node->path        = QString::fromStdString(s.path.generic_string());
        node->isSubmodule = true;
        node->missing     = s.status == gittide::SubmoduleStatus::Uninitialized;
        node->shortOid    = QString::fromStdString(s.shortOid);
        node->status      = s.status;
        node->parent      = &parent;
        appendSubmodules(*node, s.children);
        parent.children.push_back(std::move(node));
    }
}

QString RepoListModel::firstRepoPath() const
{
    if (m_roots.empty())
        return {};
    return m_roots.front()->path;
}

void RepoListModel::setRepos(const std::vector<gittide::RepoRef>& repos)
{
    beginResetModel();
    m_roots.clear();
    for (const auto& r : repos)
    {
        const std::filesystem::path p(r.path);
        std::error_code ec;
        const bool present = std::filesystem::exists(p, ec) && !ec;

        auto root         = std::make_unique<Node>();
        root->displayName = r.alias.empty() ? QString::fromStdString(r.path) : QString::fromStdString(r.alias);
        root->path        = QString::fromStdString(r.path);
        root->isSubmodule = false;
        root->missing     = !present;

        if (present)
        {
            auto repo = gittide::GitRepo::open(p);
            if (repo)
            {
                if (auto tree = repo->submoduleTree())
                    appendSubmodules(*root, *tree);
            }
        }
        m_roots.push_back(std::move(root));
    }
    endResetModel();
}

RepoListModel::Node* RepoListModel::nodeFor(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : nullptr;
}

int RepoListModel::rowOf(const Node* node) const
{
    const auto& siblings = node->parent ? node->parent->children : m_roots;
    for (std::size_t i = 0; i < siblings.size(); ++i)
        if (siblings[i].get() == node)
            return static_cast<int>(i);
    return 0;
}

QModelIndex RepoListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
        return {};
    const Node* parentNode = nodeFor(parent);
    const auto& siblings   = parentNode ? parentNode->children : m_roots;
    if (row >= static_cast<int>(siblings.size()))
        return {};
    return createIndex(row, 0, siblings[row].get());
}

QModelIndex RepoListModel::parent(const QModelIndex& child) const
{
    const Node* node = nodeFor(child);
    if (!node || !node->parent)
        return {};
    Node* p = node->parent;
    return createIndex(rowOf(p), 0, p);
}

int RepoListModel::rowCount(const QModelIndex& parent) const
{
    const Node* node     = nodeFor(parent);
    const auto& siblings = node ? node->children : m_roots;
    return static_cast<int>(siblings.size());
}

int RepoListModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant RepoListModel::data(const QModelIndex& index, int role) const
{
    const Node* node = nodeFor(index);
    if (!node)
        return {};
    switch (role)
    {
    case Qt::DisplayRole:
        return node->displayName;
    case PathRole:
        return node->path;
    case MissingRole:
        return node->missing;
    case IsSubmoduleRole:
        return node->isSubmodule;
    case ShortOidRole:
        return node->shortOid;
    case StatusRole:
        return static_cast<int>(node->status);
    default:
        return {};
    }
}

QHash<int, QByteArray> RepoListModel::roleNames() const
{
    auto roles             = QAbstractItemModel::roleNames();
    roles[PathRole]        = "repoPath";
    roles[MissingRole]     = "missing";
    roles[IsSubmoduleRole] = "isSubmodule";
    roles[ShortOidRole]    = "shortOid";
    roles[StatusRole]      = "status";
    return roles;
}

} // namespace gittide::ui
