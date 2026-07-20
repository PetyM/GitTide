#include "gittide/ui/repolistmodel.hpp"

#include <algorithm>
#include <filesystem>

#include "gittide/gitrepo.hpp"

namespace {
// The 7-hex OID the delegate shows for a submodule: the CURRENT checkout when
// initialised, else the pinned OID (empty for uninitialised).
QString submoduleDisplayOid(const gittide::SubmoduleNode& s)
{
    return QString::fromStdString(s.headShortOid.empty() ? s.shortOid : s.headShortOid);
}
} // namespace

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
        node->status      = s.status;
        node->parent      = &parent;
        node->shortOid    = submoduleDisplayOid(s);
        node->branch      = QString::fromStdString(s.branch);
        node->detached    = s.detached;
        node->dirtyCount  = s.dirtyCount;
        node->ahead       = s.ahead;
        node->behind      = s.behind;
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

QModelIndex RepoListModel::indexForRepoPath(const QString& path) const
{
    if (path.isEmpty())
        return {};
    // Depth-first search for the node carrying this exact path.
    const Node* match = nullptr;
    auto search = [&](auto&& self, const std::vector<std::unique_ptr<Node>>& nodes) -> void
    {
        for (const auto& n : nodes)
        {
            if (match)
                return;
            if (n->path == path)
            {
                match = n.get();
                return;
            }
            self(self, n->children);
        }
    };
    search(search, m_roots);
    if (!match)
        return {};
    // createIndex with the node pointer mirrors how index() builds indices, so
    // parent() resolves the ancestor chain for expandToIndex().
    return createIndex(rowOf(match), 0, const_cast<Node*>(match));
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

        // Display name: an explicit alias wins; otherwise the directory's own
        // name. A path may carry a trailing separator (e.g. "/home/u/api/"),
        // which leaves path::filename() empty — fall back to the parent's name
        // so the row never renders blank.
        std::filesystem::path base = p.has_filename() ? p.filename() : p.parent_path().filename();
        auto root                  = std::make_unique<Node>();
        root->displayName          = !r.alias.empty()           ? QString::fromStdString(r.alias)
                                     : !base.generic_string().empty() ? QString::fromStdString(base.generic_string())
                                                                       : QString::fromStdString(r.path);
        root->path                 = QString::fromStdString(r.path);
        root->isSubmodule = false;
        root->missing     = !present;

        if (present)
        {
            auto repo = gittide::GitRepo::open(p);
            if (repo)
            {
                if (auto tree = repo->submoduleTree())
                    appendSubmodules(*root, *tree);

                if (auto hs = repo->head())
                {
                    root->branch   = QString::fromStdString(hs->branch);
                    root->detached = hs->detached;
                    root->shortOid = QString::fromStdString(
                        hs->oid.substr(0, std::min<std::size_t>(7, hs->oid.size())));
                }
                if (auto st = repo->status())
                    root->dirtyCount = static_cast<int>(st->size());
                if (auto sy = repo->syncStatus())
                {
                    root->ahead       = sy->ahead;
                    root->behind      = sy->behind;
                    root->hasUpstream = sy->hasUpstream;
                }
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
    case FetchStateRole:
        return static_cast<int>(node->fetchState);
    case FetchErrorRole:
        return node->fetchError;
    case AheadRole:
        return node->ahead;
    case BehindRole:
        return node->behind;
    case BranchRole:
        return node->branch;
    case DetachedRole:
        return node->detached;
    case DirtyCountRole:
        return node->dirtyCount;
    case HasUpstreamRole:
        return node->hasUpstream;
    case BusyRole:
        return node->busy;
    case OwnerRepoPathRole:
        return node->parent ? node->parent->path : node->path;
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
    roles[FetchStateRole]  = "fetchState";
    roles[FetchErrorRole]  = "fetchError";
    roles[AheadRole]          = "ahead";
    roles[BehindRole]         = "behind";
    roles[BranchRole]         = "branch";
    roles[DetachedRole]       = "detached";
    roles[DirtyCountRole]     = "dirtyCount";
    roles[HasUpstreamRole]    = "hasUpstream";
    roles[BusyRole]           = "submoduleBusy";
    roles[OwnerRepoPathRole]  = "ownerRepoPath";
    return roles;
}

int RepoListModel::topLevelCount() const
{
    return static_cast<int>(m_roots.size());
}

void RepoListModel::resetFetchStates()
{
    for (std::size_t i = 0; i < m_roots.size(); ++i)
    {
        Node& n     = *m_roots[i];
        n.fetchState = FetchState::Idle;
        n.fetchError.clear();
        n.ahead  = 0;
        n.behind = 0;
        const QModelIndex idx = createIndex(static_cast<int>(i), 0, &n);
        emit dataChanged(idx, idx, {FetchStateRole, FetchErrorRole, AheadRole, BehindRole});
    }
}

void RepoListModel::setFetchState(int rootRow, FetchState state, const QString& error)
{
    if (rootRow < 0 || rootRow >= static_cast<int>(m_roots.size()))
        return;
    Node& n      = *m_roots[rootRow];
    n.fetchState = state;
    n.fetchError = error;
    const QModelIndex idx = createIndex(rootRow, 0, &n);
    emit dataChanged(idx, idx, {FetchStateRole, FetchErrorRole});
}

void RepoListModel::setSyncCounts(int rootRow, int ahead, int behind, bool hasUpstream)
{
    if (rootRow < 0 || rootRow >= static_cast<int>(m_roots.size()))
        return;
    Node& n        = *m_roots[rootRow];
    n.ahead        = ahead;
    n.behind       = behind;
    n.hasUpstream  = hasUpstream;
    const QModelIndex idx = createIndex(rootRow, 0, &n);
    emit dataChanged(idx, idx, {AheadRole, BehindRole, HasUpstreamRole});
}

void RepoListModel::setRepoHead(int rootRow, const QString& branch, bool detached,
                                const QString& shortOid, int dirtyCount)
{
    if (rootRow < 0 || rootRow >= static_cast<int>(m_roots.size()))
        return;
    Node& n       = *m_roots[rootRow];
    n.branch      = branch;
    n.detached    = detached;
    n.shortOid    = shortOid;
    n.dirtyCount  = dirtyCount;
    const QModelIndex idx = createIndex(rootRow, 0, &n);
    emit dataChanged(idx, idx, {BranchRole, DetachedRole, ShortOidRole, DirtyCountRole});
}

RepoListModel::Node* RepoListModel::findByPath(const QString& path)
{
    Node* match = nullptr;
    auto walk = [&](auto&& self, std::vector<std::unique_ptr<Node>>& nodes) -> void
    {
        for (auto& n : nodes)
        {
            if (match)
                return;
            if (n->path == path)
            {
                match = n.get();
                return;
            }
            self(self, n->children);
        }
    };
    walk(walk, m_roots);
    return match;
}

bool RepoListModel::submodulesEqual(const Node& node,
                                    const std::vector<gittide::SubmoduleNode>& subs) const
{
    if (node.children.size() != subs.size())
        return false;
    for (std::size_t i = 0; i < subs.size(); ++i)
    {
        const Node& c = *node.children[i];
        const auto& s = subs[i];
        if (!c.isSubmodule
            || c.path != QString::fromStdString(s.path.generic_string())
            || c.status != s.status
            || c.shortOid != submoduleDisplayOid(s)
            || c.branch != QString::fromStdString(s.branch)
            || c.detached != s.detached
            || c.dirtyCount != s.dirtyCount
            || c.ahead != s.ahead
            || c.behind != s.behind
            || !submodulesEqual(c, s.children))
            return false;
    }
    return true;
}

void RepoListModel::applySubmodules(const QString& repoPath,
                                    const std::vector<gittide::SubmoduleNode>& subs)
{
    Node* root = findByPath(repoPath);
    if (!root || submodulesEqual(*root, subs))
        return;

    reconcileChildren(*root, createIndex(rowOf(root), 0, root), subs);
}

void RepoListModel::reconcileChildren(Node& parent, const QModelIndex& parentIdx,
                                      const std::vector<gittide::SubmoduleNode>& subs)
{
    // Same path-set in the same order → in-place field updates, preserving node
    // identity so an expanded subtree does not collapse on a status/OID change
    // (the common case when switching branches or navigating submodules).
    bool sameShape = parent.children.size() == subs.size();
    for (std::size_t i = 0; sameShape && i < subs.size(); ++i)
        if (parent.children[i]->path != QString::fromStdString(subs[i].path.generic_string()))
            sameShape = false;

    if (sameShape)
    {
        for (std::size_t i = 0; i < subs.size(); ++i)
        {
            Node&       c       = *parent.children[i];
            const auto& s       = subs[i];
            const QString oid     = submoduleDisplayOid(s);
            const bool    missing = s.status == gittide::SubmoduleStatus::Uninitialized;
            const QString branch  = QString::fromStdString(s.branch);
            if (c.status != s.status || c.shortOid != oid || c.missing != missing
                || c.branch != branch || c.detached != s.detached
                || c.dirtyCount != s.dirtyCount || c.ahead != s.ahead || c.behind != s.behind)
            {
                c.status              = s.status;
                c.shortOid            = oid;
                c.missing             = missing;
                c.branch              = branch;
                c.detached            = s.detached;
                c.dirtyCount          = s.dirtyCount;
                c.ahead               = s.ahead;
                c.behind              = s.behind;
                const QModelIndex idx = index(static_cast<int>(i), 0, parentIdx);
                emit dataChanged(idx, idx, {StatusRole, ShortOidRole, MissingRole,
                                            BranchRole, DetachedRole, DirtyCountRole,
                                            AheadRole, BehindRole});
            }
            reconcileChildren(c, index(static_cast<int>(i), 0, parentIdx), s.children);
        }
        return;
    }

    // The submodule set or order genuinely changed (rare: .gitmodules edited) —
    // rebuild just this level.
    if (!parent.children.empty())
    {
        beginRemoveRows(parentIdx, 0, static_cast<int>(parent.children.size()) - 1);
        parent.children.clear();
        endRemoveRows();
    }
    if (!subs.empty())
    {
        beginInsertRows(parentIdx, 0, static_cast<int>(subs.size()) - 1);
        appendSubmodules(parent, subs);
        endInsertRows();
    }
}

void RepoListModel::setSubmoduleBusy(const QString& submodulePath, bool busy)
{
    Node* n = findByPath(submodulePath);
    if (!n || n->busy == busy)
        return;
    n->busy               = busy;
    const QModelIndex idx = createIndex(rowOf(n), 0, n);
    emit dataChanged(idx, idx, {BusyRole});
}

} // namespace gittide::ui
