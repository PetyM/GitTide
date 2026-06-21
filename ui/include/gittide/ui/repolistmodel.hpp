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
    enum Roles
    {
        PathRole = Qt::UserRole + 1,
        MissingRole,
        IsSubmoduleRole,
        ShortOidRole,
        StatusRole,
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

private:
    struct Node
    {
        QString                            displayName;
        QString                            path;
        bool                               isSubmodule = false;
        bool                               missing     = false;
        QString                            shortOid;
        gittide::SubmoduleStatus           status = gittide::SubmoduleStatus::Clean;
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
