#pragma once
#include <map>
#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "gittide/diff.hpp"

namespace gittide::ui {

/// QML list model of a single file's diff, flattened: one row for each hunk
/// header followed by one row per line. Added/Removed lines are checkable for
/// line-level staging; Context lines and hunk headers are not. Carries (hunkIndex,
/// lineIndex) on every line so a check maps back to a StageSelection. Emits
/// lineToggled() so the owning RepoViewModel can update its selection state.
class DiffLinesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        KindRole = Qt::UserRole + 1,
        OldNoRole,
        NewNoRole,
        TextRole,
        CheckableRole,
        CheckedRole,
        HunkRole,
        LineRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // checkedLines: hunkIndex -> line indices (within the hunk) that are checked.
    // wholeChecked: when true, every changed line is checked regardless of checkedLines.
    void setDiff(const gittide::DiffResult& result, const std::map<int, std::vector<int>>& checkedLines, bool wholeChecked);
    void clear();

    Q_INVOKABLE void setLineChecked(int row, bool checked);
    void setAllChecked(bool checked);

    int checkableCount() const;
    int checkedCount() const;
    std::map<int, std::vector<int>> checkedLines() const;

signals:
    void lineToggled(int hunkIndex, int lineIndex, bool checked);

private:
    struct Row
    {
        QString kind; // "hunk" | "context" | "added" | "removed"
        int     oldNo = -1;
        int     newNo = -1;
        QString text;
        bool    checkable = false;
        bool    checked   = false;
        int     hunkIndex = -1;
        int     lineIndex = -1; // index within the hunk's lines (changed+context)
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
