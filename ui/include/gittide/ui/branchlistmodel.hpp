#pragma once
#include <vector>

#include <QAbstractListModel>
#include <QString>
#include <QStringList>

#include "gittide/branchinfo.hpp"

namespace gittide::ui {

/// QML list model over the repository's branches. Each BranchInfo becomes one
/// row tagged with a section ("Local" | "Worktrees" | "Remote") so a QML
/// ListView can render grouped sections. Rows are ordered by section
/// (Local < Worktrees < Remote) then case-insensitively by name. A case-
/// insensitive substring `filter` narrows the visible rows by name.
class BranchListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        SectionRole,
        IsHeadRole,
        UpstreamRole,
        WorktreePathRole,
        RemoteRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBranches(const std::vector<gittide::BranchInfo>& branches);

    QString filter() const;
    void setFilter(const QString& f);

    // Visible-row branch name; empty when out of range.
    Q_INVOKABLE QString nameAt(int row) const;
    // Local (non-remote) branch names, for base-ref / rename pickers.
    QStringList localBranchNames() const;

signals:
    void filterChanged();
    void countChanged();

private:
    struct Row
    {
        QString name;
        QString section; // "Local" | "Worktrees" | "Remote"
        bool    isHead = false;
        QString upstream;
        QString worktreePath;
        bool    remote = false;
        int     order  = 0; // 0 Local, 1 Worktrees, 2 Remote (sort key)
    };

    void rebuild();

    std::vector<Row> m_all;     // every branch, sorted
    std::vector<Row> m_visible; // m_all narrowed by m_filter
    QString          m_filter;
};

} // namespace gittide::ui
