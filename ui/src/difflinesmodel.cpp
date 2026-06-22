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
    case BlockStateRole:
        return r.blockState;
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
        {BlockStateRole, "blockState"},
    };
}

DiffLinesModel::Row DiffLinesModel::makeLineRow(
    const gittide::DiffLine& line, int hunkIndex, int lineIndex, bool wholeChecked,
    const std::map<int, std::vector<int>>& checkedLines) const
{
    Row r;
    r.oldNo     = line.oldLineno;
    r.newNo     = line.newLineno;
    r.text      = QString::fromStdString(line.text);
    r.hunkIndex = hunkIndex;
    r.lineIndex = lineIndex;
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
            const auto it = checkedLines.find(hunkIndex);
            r.checked     = it != checkedLines.end() &&
                        std::find(it->second.begin(), it->second.end(), lineIndex) != it->second.end();
        }
    }
    return r;
}

int DiffLinesModel::computeBlockState(int blockRow)
{
    const Row& b = m_rows[static_cast<std::size_t>(blockRow)];
    int total = 0, checked = 0;
    for (int cr : b.coveredRows)
    {
        const Row& r = m_rows[static_cast<std::size_t>(cr)];
        if (!r.checkable)
            continue;
        ++total;
        if (r.checked)
            ++checked;
    }
    const int state = checked == 0 ? int(Qt::Unchecked)
                      : checked == total ? int(Qt::Checked)
                                         : int(Qt::PartiallyChecked);
    m_rows[static_cast<std::size_t>(blockRow)].blockState = state;
    return state;
}

void DiffLinesModel::refreshBlock(int blockRow)
{
    computeBlockState(blockRow);
    const QModelIndex idx = index(blockRow, 0);
    emit dataChanged(idx, idx, {BlockStateRole});
}

void DiffLinesModel::setDiff(const gittide::DiffResult& result,
                             const std::map<int, std::vector<int>>& checkedLines,
                             bool wholeChecked,
                             bool blocks)
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

        auto isChanged = [&](int l)
        {
            const auto o = hunk.lines[static_cast<std::size_t>(l)].origin;
            return o == gittide::DiffLineOrigin::Added || o == gittide::DiffLineOrigin::Removed;
        };

        const int n = static_cast<int>(hunk.lines.size());
        int       l = 0;
        while (l < n)
        {
            if (blocks && isChanged(l))
            {
                int e = l;
                while (e < n && isChanged(e))
                    ++e;

                int blockRow = -1;
                if (e - l >= 2)
                {
                    blockRow      = static_cast<int>(m_rows.size());
                    Row b;
                    b.kind        = QStringLiteral("block");
                    b.hunkIndex   = h;
                    m_rows.push_back(std::move(b));
                }

                for (int k = l; k < e; ++k)
                {
                    Row r     = makeLineRow(hunk.lines[static_cast<std::size_t>(k)], h, k,
                                            wholeChecked, checkedLines);
                    r.blockRow = blockRow;
                    m_rows.push_back(std::move(r));
                    if (blockRow >= 0)
                        m_rows[static_cast<std::size_t>(blockRow)].coveredRows.push_back(
                            static_cast<int>(m_rows.size()) - 1);
                }
                if (blockRow >= 0)
                    computeBlockState(blockRow); // no signal during model reset

                l = e;
            }
            else
            {
                m_rows.push_back(makeLineRow(hunk.lines[static_cast<std::size_t>(l)], h, l,
                                             wholeChecked, checkedLines));
                ++l;
            }
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
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        Row& r = m_rows[static_cast<std::size_t>(i)];
        if (r.checkable && r.checked != checked)
        {
            r.checked             = checked;
            const QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {CheckedRole});
        }
    }
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
