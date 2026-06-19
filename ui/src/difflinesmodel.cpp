#include "gittide/ui/difflinesmodel.hpp"

#include <algorithm>

#include <QString>

namespace gittide::ui {

namespace {
QString hunkHeader(const gittide::DiffHunk& h)
{
    return QStringLiteral("@@ -%1,%2 +%3,%4 @@")
        .arg(h.oldStart)
        .arg(h.oldLines)
        .arg(h.newStart)
        .arg(h.newLines);
}
}

int DiffLinesModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant DiffLinesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case KindRole:
        return r.kind;
    case OldNoRole:
        return r.oldNo;
    case NewNoRole:
        return r.newNo;
    case TextRole:
        return r.text;
    case CheckableRole:
        return r.checkable;
    case CheckedRole:
        return r.checked;
    case HunkRole:
        return r.hunkIndex;
    case LineRole:
        return r.lineIndex;
    default:
        return {};
    }
}

QHash<int, QByteArray> DiffLinesModel::roleNames() const
{
    return {
        {KindRole, "lineKind"},
        {OldNoRole, "oldNo"},
        {NewNoRole, "newNo"},
        {TextRole, "lineText"},
        {CheckableRole, "checkable"},
        {CheckedRole, "lineChecked"},
        {HunkRole, "hunkIndex"},
        {LineRole, "lineIndex"},
    };
}

void DiffLinesModel::setDiff(const gittide::DiffResult& result, const std::map<int, std::vector<int>>& checkedLines, bool wholeChecked)
{
    beginResetModel();
    m_rows.clear();
    for (int h = 0; h < static_cast<int>(result.hunks.size()); ++h)
    {
        const gittide::DiffHunk& hunk = result.hunks[static_cast<std::size_t>(h)];

        Row header;
        header.kind      = QStringLiteral("hunk");
        header.text      = hunkHeader(hunk);
        header.hunkIndex = h;
        m_rows.push_back(std::move(header));

        for (int l = 0; l < static_cast<int>(hunk.lines.size()); ++l)
        {
            const gittide::DiffLine& line = hunk.lines[static_cast<std::size_t>(l)];
            Row r;
            r.oldNo     = line.oldLineno;
            r.newNo     = line.newLineno;
            r.text      = QString::fromStdString(line.text);
            r.hunkIndex = h;
            r.lineIndex = l;
            switch (line.origin)
            {
            case gittide::DiffLineOrigin::Added:
                r.kind      = QStringLiteral("added");
                r.checkable = true;
                break;
            case gittide::DiffLineOrigin::Removed:
                r.kind      = QStringLiteral("removed");
                r.checkable = true;
                break;
            case gittide::DiffLineOrigin::Context:
            default:
                r.kind      = QStringLiteral("context");
                r.checkable = false;
                break;
            }
            if (r.checkable)
            {
                if (wholeChecked)
                {
                    r.checked = true;
                }
                else
                {
                    const auto it = checkedLines.find(h);
                    r.checked     = it != checkedLines.end() && std::find(it->second.begin(), it->second.end(), l) != it->second.end();
                }
            }
            m_rows.push_back(std::move(r));
        }
    }
    endResetModel();
}

void DiffLinesModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void DiffLinesModel::setLineChecked(int row, bool checked)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    Row& r = m_rows[static_cast<std::size_t>(row)];
    if (!r.checkable || r.checked == checked)
        return;
    r.checked             = checked;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {CheckedRole});
    emit lineToggled(r.hunkIndex, r.lineIndex, checked);
}

void DiffLinesModel::setAllChecked(bool checked)
{
    bool any = false;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        Row& r = m_rows[static_cast<std::size_t>(i)];
        if (r.checkable && r.checked != checked)
        {
            r.checked             = checked;
            const QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {CheckedRole});
            any = true;
        }
    }
    Q_UNUSED(any);
}

int DiffLinesModel::checkableCount() const
{
    int n = 0;
    for (const auto& r : m_rows)
        if (r.checkable)
            ++n;
    return n;
}

int DiffLinesModel::checkedCount() const
{
    int n = 0;
    for (const auto& r : m_rows)
        if (r.checkable && r.checked)
            ++n;
    return n;
}

std::map<int, std::vector<int>> DiffLinesModel::checkedLines() const
{
    std::map<int, std::vector<int>> out;
    for (const auto& r : m_rows)
        if (r.checkable && r.checked)
            out[r.hunkIndex].push_back(r.lineIndex);
    return out;
}

} // namespace gittide::ui
