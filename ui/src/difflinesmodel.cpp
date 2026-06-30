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
    case ConflictRegionRole:
        return r.conflictRegion;
    case HtmlRole:
        return r.html;
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
        {ConflictRegionRole, "conflictRegion"},
        {HtmlRole, "lineHtml"},
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
                             bool blocks,
                             const QString& filePath)
{
    beginResetModel();
    m_rows.clear();
    m_filePath = filePath;
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
    rehighlightRows();
    endResetModel();
}

void DiffLinesModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void DiffLinesModel::rehighlightRows()
{
    for (Row& r : m_rows)
        r.html.clear();
    if (m_filePath.isEmpty() || !m_highlighter.hasDefinition(m_filePath))
        return;

    const QString kContext = QStringLiteral("context");
    const QString kAdded   = QStringLiteral("added");
    const QString kRemoved = QStringLiteral("removed");

    auto run = [&](const std::vector<int>& idxs)
    {
        if (idxs.empty())
            return;
        std::vector<QString> texts;
        texts.reserve(idxs.size());
        for (int i : idxs)
            texts.push_back(m_rows[static_cast<std::size_t>(i)].text);
        const std::vector<QString> html =
            m_highlighter.highlightLines(m_filePath, texts, m_syntaxDark);
        if (html.size() != idxs.size())
            return; // defensive: highlighter gave an unexpected count
        for (std::size_t k = 0; k < idxs.size(); ++k)
            m_rows[static_cast<std::size_t>(idxs[k])].html = html[k];
    };

    // Single pass: bucket rows by hunkIndex into (oldRows, newRows) pairs.
    // context rows land in both lists; removed→old; added→new; others skipped.
    // State resets per hunk (gaps between hunks are unobservable).
    std::map<int, std::pair<std::vector<int>, std::vector<int>>> byHunk;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        const Row& r = m_rows[static_cast<std::size_t>(i)];
        if (r.kind == kContext)
        {
            byHunk[r.hunkIndex].first.push_back(i);
            byHunk[r.hunkIndex].second.push_back(i);
        }
        else if (r.kind == kRemoved)
            byHunk[r.hunkIndex].first.push_back(i);
        else if (r.kind == kAdded)
            byHunk[r.hunkIndex].second.push_back(i);
    }
    for (auto& [h, lists] : byHunk)
    {
        run(lists.first);  // old side: context + removed
        run(lists.second); // new side: context + added
    }
}

void DiffLinesModel::setSyntaxDark(bool dark)
{
    if (m_syntaxDark == dark)
        return;
    m_syntaxDark = dark;
    rehighlightRows();
    if (!m_rows.empty())
        emit dataChanged(index(0, 0),
                         index(static_cast<int>(m_rows.size()) - 1, 0),
                         {HtmlRole});
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
    // Capture blockRow before refreshBlock() potentially reallocates or modifies rows.
    const int blockRow = r.blockRow;
    if (blockRow >= 0)
        refreshBlock(blockRow);
}

void DiffLinesModel::setBlockChecked(int row, bool checked)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    if (m_rows[static_cast<std::size_t>(row)].kind != QStringLiteral("block"))
        return;
    // Copy: setLineChecked() refreshes the block row mid-loop.
    const std::vector<int> covered = m_rows[static_cast<std::size_t>(row)].coveredRows;
    for (int cr : covered)
        setLineChecked(cr, checked); // emits lineToggled per changed line, refreshes block
    refreshBlock(row);               // ensure correct state even if nothing changed
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
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        if (m_rows[static_cast<std::size_t>(i)].kind == QStringLiteral("block"))
            refreshBlock(i);
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


// ---------------------------------------------------------------------------
// Conflict region support
// ---------------------------------------------------------------------------

namespace {

/// Rewrite region @p target in @p text, keeping the side indicated by @p which
/// (0 = ours, 1 = theirs, 2 = both ours+theirs). Returns the new full file text.
/// This is a pure string transform with no git repo access so it can be unit-tested
/// directly. Untouched regions are re-emitted verbatim with simplified markers.
static QString rewriteRegion(const QString& text, int target, int which)
{
    const bool trailingNl = text.endsWith(u'\n');
    QStringList in = text.split(u'\n');
    if (trailingNl && !in.isEmpty() && in.last().isEmpty())
        in.removeLast();

    QStringList out;
    int region = 0;

    enum State
    {
        Outside,
        Ours,
        Theirs
    } state = Outside;

    QStringList ours, theirs;

    auto flush = [&]()
    {
        if (which == 0)
            out += ours;
        else if (which == 1)
            out += theirs;
        else
        {
            out += ours;
            out += theirs;
        }
        ours.clear();
        theirs.clear();
    };

    for (const QString& ln : in)
    {
        if (ln.startsWith(QStringLiteral("<<<<<<<")))
        {
            state = Ours;
            continue;
        }
        if (ln.startsWith(QStringLiteral("=======")) && state != Outside)
        {
            state = Theirs;
            continue;
        }
        if (ln.startsWith(QStringLiteral(">>>>>>>")))
        {
            if (region == target)
            {
                flush();
            }
            else
            {
                // Untouched region: re-emit with simplified markers.
                out += QStringLiteral("<<<<<<<");
                out += ours;
                out += QStringLiteral("=======");
                out += theirs;
                out += QStringLiteral(">>>>>>>");
                ours.clear();
                theirs.clear();
            }
            state = Outside;
            ++region;
            continue;
        }
        if (state == Ours)
            ours += ln;
        else if (state == Theirs)
            theirs += ln;
        else
            out += ln;
    }

    QString joined = out.join(u'\n');
    if (trailingNl)
        joined += u'\n';
    return joined;
}

} // anonymous namespace

void DiffLinesModel::setConflictContent(const QString& fileText)
{
    beginResetModel();
    m_rows.clear();
    m_conflictText = fileText;

    const QStringList lines = fileText.split(u'\n');
    int  region   = 0;
    bool inOurs   = false;
    bool inTheirs = false;

    for (const QString& ln : lines)
    {
        if (ln.startsWith(QStringLiteral("<<<<<<<")) && !inOurs && !inTheirs)
        {
            inOurs    = true;
            inTheirs  = false;
            Row r;
            r.kind           = QStringLiteral("conflict-start");
            r.text           = ln;
            r.conflictRegion = region;
            m_rows.push_back(std::move(r));
            continue;
        }
        if (ln.startsWith(QStringLiteral("=======")) && (inOurs || inTheirs))
        {
            inOurs   = false;
            inTheirs = true;
            Row r;
            r.kind           = QStringLiteral("conflict-sep");
            r.text           = ln;
            r.conflictRegion = region;
            m_rows.push_back(std::move(r));
            continue;
        }
        if (ln.startsWith(QStringLiteral(">>>>>>>")) && inTheirs)
        {
            inOurs   = false;
            inTheirs = false;
            Row r;
            r.kind           = QStringLiteral("conflict-end");
            r.text           = ln;
            r.conflictRegion = region;
            m_rows.push_back(std::move(r));
            ++region;
            continue;
        }
        Row r;
        r.text = ln;
        if (inOurs)
        {
            r.kind           = QStringLiteral("ours");
            r.conflictRegion = region;
        }
        else if (inTheirs)
        {
            r.kind           = QStringLiteral("theirs");
            r.conflictRegion = region;
        }
        else
        {
            r.kind = QStringLiteral("context");
        }
        m_rows.push_back(std::move(r));
    }

    endResetModel();
}

bool DiffLinesModel::isResolved() const
{
    return !m_conflictText.contains(QStringLiteral("<<<<<<<"))
        && !m_conflictText.contains(QStringLiteral(">>>>>>>"))
        && !m_conflictText.contains(QStringLiteral("\n======="));
}

QString DiffLinesModel::acceptCurrent(int region)
{
    return rewriteRegion(m_conflictText, region, 0);
}

QString DiffLinesModel::acceptIncoming(int region)
{
    return rewriteRegion(m_conflictText, region, 1);
}

QString DiffLinesModel::acceptBoth(int region)
{
    return rewriteRegion(m_conflictText, region, 2);
}

} // namespace gittide::ui
