#include "gittide/ui/diffview.hpp"

#include <QAction>
#include <QFont>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>
#include <algorithm>

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

namespace {
constexpr int HunkRole   = Qt::UserRole + 1;
constexpr int LineRole   = Qt::UserRole + 2;
constexpr int OriginRole = Qt::UserRole + 3;
} // namespace

DiffView::DiffView(QWidget* parent)
    : QWidget(parent)
    , m_lines(new QListWidget(this))
{
    qRegisterMetaType<gittide::StageSelection>();

    m_lines->setObjectName(QStringLiteral("diffLines"));
    m_lines->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_lines->setContextMenuPolicy(Qt::CustomContextMenu);

    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_lines->setFont(mono);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_lines);

    // itemChanged: emit lineCheckToggled when the user flips a checkbox.
    connect(m_lines,
            &QListWidget::itemChanged,
            this,
            [this](QListWidgetItem* item)
            {
                if (m_filling || m_mode != Mode::Editable)
                    return;
                const auto origin =
                    static_cast<gittide::DiffLineOrigin>(item->data(OriginRole).toInt());
                if (origin == gittide::DiffLineOrigin::Context)
                    return;
                const int hunk    = item->data(HunkRole).toInt();
                const int lineIdx = item->data(LineRole).toInt();
                const bool checked = (item->checkState() == Qt::Checked);
                emit lineCheckToggled(
                    pathToQString(m_file),
                    hunk,
                    lineIdx,
                    checked);
            });

    // Context menu: Discard action.
    connect(m_lines,
            &QListWidget::customContextMenuRequested,
            this,
            [this](const QPoint& pos)
            {
                QMenu menu(this);
                auto* discard = menu.addAction(QStringLiteral("Discard"));
                if (menu.exec(m_lines->viewport()->mapToGlobal(pos)) != discard)
                    return;

                // Build selection from checkable (added/removed) items in the
                // right-clicked row's hunk, if any; fall back to whole-file.
                QListWidgetItem* clickedItem = m_lines->itemAt(pos);
                if (clickedItem)
                {
                    const auto origin =
                        static_cast<gittide::DiffLineOrigin>(clickedItem->data(OriginRole).toInt());
                    if (origin != gittide::DiffLineOrigin::Context)
                    {
                        const int hunk = clickedItem->data(HunkRole).toInt();
                        // Collect all checked lines in that hunk, or just this one.
                        std::vector<int> lineIndices;
                        for (int r = 0; r < m_lines->count(); ++r)
                        {
                            auto* it = m_lines->item(r);
                            if (it->data(HunkRole).toInt() != hunk)
                                continue;
                            const auto orig =
                                static_cast<gittide::DiffLineOrigin>(it->data(OriginRole).toInt());
                            if (orig == gittide::DiffLineOrigin::Context)
                                continue;
                            if (m_mode == Mode::Editable && it->checkState() == Qt::Checked)
                                lineIndices.push_back(it->data(LineRole).toInt());
                        }
                        if (lineIndices.empty())
                            lineIndices.push_back(clickedItem->data(LineRole).toInt());
                        std::sort(lineIndices.begin(), lineIndices.end());
                        emit discardRequested(gittide::StageSelection{
                            .path        = m_file,
                            .hunkIndex   = hunk,
                            .lineIndices = std::move(lineIndices)});
                        return;
                    }
                }
                // Whole-file discard.
                emit discardRequested(gittide::StageSelection{.path = m_file});
            });
}

void DiffView::setMode(Mode mode)
{
    m_mode = mode;
}

void DiffView::clear()
{
    m_lines->clear();
    m_file.clear();
}

void DiffView::setDiff(const gittide::DiffResult& result,
                       const std::filesystem::path& file,
                       bool wholeChecked,
                       const std::map<int, std::vector<int>>& checkedLines)
{
    m_filling = true;
    m_lines->clear();
    m_file = file;

    for (int h = 0; h < static_cast<int>(result.hunks.size()); ++h)
    {
        const auto& hunk = result.hunks[h];

        // Per-hunk: if checkedLines has an entry for this hunk, use it;
        // otherwise fall back to wholeChecked.
        auto hunkIt         = checkedLines.find(h);
        bool hunkHasExplicit = (hunkIt != checkedLines.end());

        for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i)
        {
            const auto& ln     = hunk.lines[i];
            const QChar prefix = ln.origin == gittide::DiffLineOrigin::Added     ? QChar('+')
                                 : ln.origin == gittide::DiffLineOrigin::Removed ? QChar('-')
                                                                                 : QChar(' ');
            auto* item = new QListWidgetItem(prefix + QString::fromStdString(ln.text), m_lines);
            item->setData(HunkRole, h);
            item->setData(LineRole, i);
            item->setData(OriginRole, static_cast<int>(ln.origin));

            // Qt::ItemIsUserCheckable is in the DEFAULT QListWidgetItem flags.
            // We must explicitly remove it for non-checkable items; remove it
            // for context lines always, and for all items in ReadOnly mode.
            const bool isChangedLine = (ln.origin == gittide::DiffLineOrigin::Added ||
                                        ln.origin == gittide::DiffLineOrigin::Removed);
            const bool wantCheckable = (m_mode == Mode::Editable && isChangedLine);

            if (wantCheckable)
            {
                // Flag is already set by default; ensure it and set initial state.
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

                bool checked;
                if (hunkHasExplicit)
                {
                    const auto& indices = hunkIt->second;
                    checked = std::find(indices.begin(), indices.end(), i) != indices.end();
                }
                else
                {
                    checked = wholeChecked;
                }
                item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            }
            else
            {
                // Explicitly strip the default checkable flag.
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            }

            // Store a diffRole value for future custom delegate / QSS use.
            // No hex literals here; a delegate wired to the theme handles colour.
            if (ln.origin == gittide::DiffLineOrigin::Added)
                item->setData(Qt::UserRole + 10, QStringLiteral("added"));
            else if (ln.origin == gittide::DiffLineOrigin::Removed)
                item->setData(Qt::UserRole + 10, QStringLiteral("removed"));
            else
                item->setData(Qt::UserRole + 10, QStringLiteral("context"));
        }
    }

    m_filling = false;
}

} // namespace gittide::ui
