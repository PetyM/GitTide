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

/// True when `repoPath` is the folder `sourcePath` itself, or lies inside it.
/// The boundary test matters: "/home/u/proj" must not capture
/// "/home/u/projects/api". Both are generic (forward-slash) paths.
bool containsRepo(const QString& sourcePath, const QString& repoPath)
{
    if (sourcePath.isEmpty())
        return false;
    if (repoPath == sourcePath)
        return true; // a source registered on a folder that is itself a repo
    if (!repoPath.startsWith(sourcePath))
        return false;
    return sourcePath.endsWith(QLatin1Char('/')) || repoPath.at(sourcePath.size()) == QLatin1Char('/');
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
    for (const auto& root : m_roots)
    {
        if (!root->isSource)
            return root->path;
        if (!root->children.empty())
            return root->children.front()->path;
    }
    return {};
}

bool RepoListModel::isSourceRow(int row) const
{
    return row >= 0 && row < static_cast<int>(m_roots.size()) && m_roots[row]->isSource;
}

QModelIndex RepoListModel::indexForRepoPath(const QString& path) const
{
    if (path.isEmpty())
        return {};
    // Depth-first search for the repository node carrying this exact path. A
    // source node is skipped even on an exact match: it is a folder, and one
    // registered on a repository carries that repository's own path.
    const Node* match = nullptr;
    auto search = [&](auto&& self, const std::vector<std::unique_ptr<Node>>& nodes) -> void
    {
        for (const auto& n : nodes)
        {
            if (match)
                return;
            if (!n->isSource && n->path == path)
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

std::unique_ptr<RepoListModel::Node> RepoListModel::makeRepoNode(const gittide::RepoRef& ref) const
{
    const std::filesystem::path p(ref.path);
    std::error_code ec;
    const bool present = std::filesystem::exists(p, ec) && !ec;

    // Display name: an explicit alias wins; otherwise the directory's own
    // name. A path may carry a trailing separator (e.g. "/home/u/api/"),
    // which leaves path::filename() empty — fall back to the parent's name
    // so the row never renders blank.
    std::filesystem::path base = p.has_filename() ? p.filename() : p.parent_path().filename();
    auto node                  = std::make_unique<Node>();
    node->displayName          = !ref.alias.empty()           ? QString::fromStdString(ref.alias)
                                 : !base.generic_string().empty() ? QString::fromStdString(base.generic_string())
                                                                   : QString::fromStdString(ref.path);
    node->path                 = QString::fromStdString(ref.path);
    node->isSubmodule = false;
    node->missing     = !present;

    // Deliberately no git I/O here. setRepos runs on the UI thread on every
    // project switch; opening each repo to read head/status/sync/submodules
    // stalled the window for as long as that took. The rows render at once
    // from the RepoRef alone and ProjectController hydrates them off-thread
    // (see its pollRepos, kicked immediately by activate()).
    return node;
}

void RepoListModel::setRepos(const std::vector<gittide::RepoRef>& repos,
                             const std::vector<gittide::RepoSource>& sources)
{
    beginResetModel();
    m_roots.clear();

    // One group per source, in store order, before any ungrouped repo.
    std::vector<Node*> groups;
    groups.reserve(sources.size());
    for (const auto& s : sources)
    {
        const std::filesystem::path sp(s.path);
        std::error_code             ec;

        auto g = std::make_unique<Node>();
        std::filesystem::path base = sp.has_filename() ? sp.filename() : sp.parent_path().filename();
        g->displayName = base.generic_string().empty() ? QString::fromStdString(s.path)
                                                       : QString::fromStdString(base.generic_string());
        g->path      = QString::fromStdString(s.path);
        g->isSource  = true;
        g->available = std::filesystem::is_directory(sp, ec) && !ec;

        groups.push_back(g.get());
        m_roots.push_back(std::move(g));
    }

    for (const auto& r : repos)
    {
        auto node = makeRepoNode(r);

        // Deepest containing source wins, so a source nested inside another
        // takes its repos rather than both listing them.
        Node* owner = nullptr;
        for (Node* g : groups)
        {
            if (!containsRepo(g->path, node->path))
                continue;
            if (!owner || g->path.size() > owner->path.size())
                owner = g;
        }

        if (owner)
        {
            node->parent = owner;
            owner->children.push_back(std::move(node));
        }
        else
        {
            m_roots.push_back(std::move(node));
        }
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
        // A source-group parent is a folder, not a repository, so a
        // top-level repo grouped under one reports itself as its own owner.
        return (node->parent && !node->parent->isSource) ? node->parent->path : node->path;
    case IsSourceRole:
        return node->isSource;
    case RepoCountRole:
        return node->isSource ? static_cast<int>(node->children.size()) : 0;
    case AvailableRole:
        return node->isSource ? node->available : true;
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
    roles[IsSourceRole]  = "isSource";
    roles[RepoCountRole] = "repoCount";
    roles[AvailableRole] = "available";
    return roles;
}

int RepoListModel::topLevelCount() const
{
    return static_cast<int>(m_roots.size());
}

void RepoListModel::resetFetchStates()
{
    // Repositories, not roots: a root may be a source group, which has no fetch
    // state of its own and holds its repositories one level down. Clearing by
    // root position would both write onto folder rows and leave every grouped
    // repository's stale badge in place — including one that fetchAll skips
    // because it is missing on disk.
    auto reset = [&](Node& n)
    {
        n.fetchState = FetchState::Idle;
        n.fetchError.clear();
        n.ahead  = 0;
        n.behind = 0;
        const QModelIndex idx = createIndex(rowOf(&n), 0, &n);
        emit dataChanged(idx, idx, {FetchStateRole, FetchErrorRole, AheadRole, BehindRole});
    };

    for (const auto& root : m_roots)
    {
        if (!root->isSource)
        {
            reset(*root);
            continue;
        }
        for (const auto& child : root->children)
            reset(*child);
    }
}

bool RepoListModel::setFetchStateByPath(const QString& path, FetchState state, const QString& error)
{
    Node* n = findByPath(path);
    if (!n)
        return false;
    applyFetchState(*n, state, error);
    return true;
}

void RepoListModel::applyFetchState(Node& n, FetchState state, const QString& error)
{
    n.fetchState = state;
    n.fetchError = error;
    const QModelIndex idx = createIndex(rowOf(&n), 0, &n);
    emit dataChanged(idx, idx, {FetchStateRole, FetchErrorRole});
}

bool RepoListModel::setSyncCountsByPath(const QString& path, int ahead, int behind, bool hasUpstream)
{
    Node* n = findByPath(path);
    if (!n)
        return false;
    applySyncCounts(*n, ahead, behind, hasUpstream);
    return true;
}

void RepoListModel::applySyncCounts(Node& n, int ahead, int behind, bool hasUpstream)
{
    n.ahead       = ahead;
    n.behind      = behind;
    n.hasUpstream = hasUpstream;
    const QModelIndex idx = createIndex(rowOf(&n), 0, &n);
    emit dataChanged(idx, idx, {AheadRole, BehindRole, HasUpstreamRole});
}

bool RepoListModel::setRepoHeadByPath(const QString& path, const QString& branch, bool detached,
                                      const QString& shortOid, int dirtyCount)
{
    Node* n = findByPath(path);
    if (!n)
        return false;
    applyRepoHead(*n, branch, detached, shortOid, dirtyCount);
    return true;
}

void RepoListModel::applyRepoHead(Node& n, const QString& branch, bool detached,
                                  const QString& shortOid, int dirtyCount)
{
    n.branch     = branch;
    n.detached   = detached;
    n.shortOid   = shortOid;
    n.dirtyCount = dirtyCount;
    const QModelIndex idx = createIndex(rowOf(&n), 0, &n);
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
            // Source nodes share the namespace but are never repositories, and a
            // source registered on a folder that IS a repository carries the very
            // same path as its child. Skipping them keeps repository state — head,
            // sync counts, fetch state, the submodule subtree — off the folder row
            // and on the repository the caller meant.
            if (!n->isSource && n->path == path)
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
