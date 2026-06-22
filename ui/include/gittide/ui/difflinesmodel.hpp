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
        BlockStateRole, // int Qt::CheckState; meaningful only on "block" rows
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // checkedLines: hunkIndex -> line indices (within the hunk) that are checked.
    // wholeChecked: when true, every changed line is checked regardless of checkedLines.
    // blocks: when true, insert a "block" row before each run of 2+ consecutive
    //         changed (added/removed) lines. Defaults false (history/read-only diff).
    void setDiff(const gittide::DiffResult& result,
                 const std::map<int, std::vector<int>>& checkedLines,
                 bool wholeChecked,
                 bool blocks = false);
    void clear();

    Q_INVOKABLE void setLineChecked(int row, bool checked);
    Q_INVOKABLE void setBlockChecked(int row, bool checked);
    void setAllChecked(bool checked);

    int checkableCount() const;
    int checkedCount() const;
    std::map<int, std::vector<int>> checkedLines() const;

signals:
    void lineToggled(int hunkIndex, int lineIndex, bool checked);

private:
    struct Row
    {
        QString kind; // "hunk" | "context" | "added" | "removed" | "block"
        int     oldNo = -1;
        int     newNo = -1;
        QString text;
        bool    checkable = false;
        bool    checked   = false;
        int     hunkIndex = -1;
        int     lineIndex = -1; // index within the hunk's lines (changed+context)
        // Block support:
        int              blockRow   = -1; // on a line row: index of its block row, or -1
        std::vector<int> coveredRows;     // on a "block" row: the line rows it controls
        int              blockState = 0;  // on a "block" row: Qt::CheckState as int
    };
    std::vector<Row> m_rows;

    Row  makeLineRow(const gittide::DiffLine& line, int hunkIndex, int lineIndex,
                     bool wholeChecked,
                     const std::map<int, std::vector<int>>& checkedLines) const;
    int  computeBlockState(int blockRow);   // sets m_rows[blockRow].blockState, no signal
    void refreshBlock(int blockRow);        // computeBlockState + emit dataChanged
};

} // namespace gittide::ui
