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

    /// Path of the first top-level repository, or an empty string when the
    /// active project has no repositories. Used to auto-open a repo so the
    /// main area shows working state rather than the empty page.
    Q_INVOKABLE QString firstRepoPath() const;

    int  topLevelCount() const;
    void resetFetchStates();
    void setFetchState(int rootRow, FetchState state, const QString& error = {});
    void setSyncCounts(int rootRow, int ahead, int behind);

private:
    struct Node
    {
        QString                            displayName;
        QString                            path;
        bool                               isSubmodule = false;
        bool                               missing     = false;
        QString                            shortOid;
        gittide::SubmoduleStatus           status = gittide::SubmoduleStatus::Clean;
        FetchState                         fetchState = FetchState::Idle;
        QString                            fetchError;
        int                                ahead  = 0;
        int                                behind = 0;
        Node*                              parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    // Build child Nodes from a submodule subtree, linking parent pointers.
    void appendSubmodules(Node& parent, const std::vector<gittide::SubmoduleNode>& subs);
    // The Node behind an index (nullptr → the invisible root / top-level list).
    Node* nodeFor(const QModelIndex& index) const;
    // Row of `node` within its sibling list.
    int rowOf(const Node* node) const;

    std::vector<std::unique_ptr<Node>> m_roots;
};

} // namespace gittide::ui
