#pragma once
#include <QAbstractItemModel>
#include <memory>
#include <vector>

#include "gittide/projectstore.hpp"
#include "gittide/submodule.hpp"

namespace gittide::ui {

class RepoListModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum class FetchState
    {
        Idle,
        Running,
        UpToDate,
        Updated,
        Failed,
    };
    Q_ENUM(FetchState)

    enum Roles
    {
        PathRole = Qt::UserRole + 1,
        MissingRole,
        IsSubmoduleRole,
        ShortOidRole,
        StatusRole,
        FetchStateRole,
        FetchErrorRole,
        AheadRole,
        BehindRole,
        BranchRole,
        DetachedRole,
        DirtyCountRole,
        HasUpstreamRole,
        BusyRole,
        OwnerRepoPathRole,
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

    /// Replace the submodule children of the top-level repo node identified by
    /// `repoPath`. When the new subtree is structurally identical (path + status
    /// + shortOid, recursively) to the existing one the call is a no-op and
    /// emits nothing — safe to call from a periodic poll.
    void applySubmodules(const QString& repoPath,
                         const std::vector<gittide::SubmoduleNode>& subs);

    /// Toggle a per-row spinner flag for the submodule identified by its
    /// absolute `submodulePath` and emit `dataChanged` for `BusyRole`.
    void setSubmoduleBusy(const QString& submodulePath, bool busy);

    /// Path of the first top-level repository, or an empty string when the
    /// active project has no repositories. Used to auto-open a repo so the
    /// main area shows working state rather than the empty page.
    Q_INVOKABLE QString firstRepoPath() const;

    /// Index of the repo or submodule node whose path matches `path` (exact
    /// match), searching the whole tree; an invalid index when not found. Lets the
    /// sidebar expand/reveal the active repo — e.g. a restored submodule that sits
    /// collapsed under its parent on launch.
    Q_INVOKABLE QModelIndex indexForRepoPath(const QString& path) const;

    int  topLevelCount() const;
    void resetFetchStates();
    void setFetchState(int rootRow, FetchState state, const QString& error = {});
    void setSyncCounts(int rootRow, int ahead, int behind, bool hasUpstream);
    /// Set the current-branch / dirty state of the top-level repo at `rootRow`.
    /// `shortOid` is used only for the detached-HEAD fallback (reuses ShortOidRole).
    void setRepoHead(int rootRow, const QString& branch, bool detached,
                     const QString& shortOid, int dirtyCount);

private:
    struct Node
    {
        QString                            displayName;
        QString                            path;
        bool                               isSubmodule = false;
        bool                               missing     = false;
        bool                               busy        = false;
        QString                            shortOid;
        gittide::SubmoduleStatus           status = gittide::SubmoduleStatus::Clean;
        FetchState                         fetchState = FetchState::Idle;
        QString                            fetchError;
        int                                ahead  = 0;
        int                                behind = 0;
        QString                            branch;
        bool                               detached    = false;
        int                                dirtyCount   = 0;
        bool                               hasUpstream  = false;
        Node*                              parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    // Build child Nodes from a submodule subtree, linking parent pointers.
    void appendSubmodules(Node& parent, const std::vector<gittide::SubmoduleNode>& subs);
    // The Node behind an index (nullptr → the invisible root / top-level list).
    Node* nodeFor(const QModelIndex& index) const;
    // Row of `node` within its sibling list.
    int rowOf(const Node* node) const;
    // True when `subs` matches `node`'s existing submodule children exactly
    // (path + status + shortOid, recursively) — lets applySubmodules no-op.
    bool submodulesEqual(const Node& node,
                         const std::vector<gittide::SubmoduleNode>& subs) const;
    // Any node by exact path (depth-first), or nullptr.
    Node* findByPath(const QString& path);
    // Minimally update `parent`'s submodule children to match `subs`: when the
    // child path-set/order is unchanged, mutate changed fields in place and emit
    // dataChanged, recursing into grandchildren — this preserves each node's
    // identity (and so the TreeView's expansion state). Only when the set/order
    // actually differs does it rebuild this one level (begin/endRemove+Insert).
    void reconcileChildren(Node& parent, const QModelIndex& parentIdx,
                           const std::vector<gittide::SubmoduleNode>& subs);

    std::vector<std::unique_ptr<Node>> m_roots;
};

} // namespace gittide::ui
