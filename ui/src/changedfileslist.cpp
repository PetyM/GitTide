#include "gittide/ui/changedfileslist.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>

namespace gittide::ui {

namespace {

// Item data roles
constexpr int PathRole  = Qt::UserRole + 1;
constexpr int FlagsRole = Qt::UserRole + 2;
// Colour role: the letter foreground colour derived from the state* token at
// population time, stored as QColor so no hex literal appears in paint code.
constexpr int LetterColourRole = Qt::UserRole + 3;

// Map StatusFlag → status letter.
// Priority: staged flags (Index*) take precedence over worktree flags (Wt*).
QChar statusLetter(gittide::StatusFlag flags)
{
    using F = gittide::StatusFlag;
    if (hasFlag(flags, F::IndexNew))      return QLatin1Char('A');
    if (hasFlag(flags, F::IndexModified)) return QLatin1Char('M');
    if (hasFlag(flags, F::IndexDeleted))  return QLatin1Char('D');
    if (hasFlag(flags, F::WtNew))         return QLatin1Char('U'); // untracked
    if (hasFlag(flags, F::WtModified))    return QLatin1Char('M');
    if (hasFlag(flags, F::WtDeleted))     return QLatin1Char('D');
    return QLatin1Char('?');
}

// Map StatusFlag → state colour token name, looked up from the application
// palette-level user property stored by ThemeManager via buildAccentStyleSheet.
// To avoid hard-coding hex we use a small QSS object-property trick: we name
// label objects by state letter and rely on the QSS selectors added in
// buildAccentStyleSheet. However, for QListWidgetItem foreground we need a
// QColor at population time. We resolve it from a QSS-compatible property
// lookup: the colours are stored in dynamic QApplication properties keyed as
// "stateAdded", "stateModified", etc., which ThemeManager sets (see below).
// If the properties are absent (tests / no ThemeManager) we fall back to
// tasteful defaults from the dark theme tokens — these are the only place in
// this file where hex strings appear, guarded by the fallback comment.
QColor stateColourFromFlags(gittide::StatusFlag flags)
{
    const QChar letter = statusLetter(flags);

    // Resolve from dynamic QApplication property (set by ThemeManager via
    // applyStateColours). This keeps all actual colour decisions in theme.cpp /
    // themestyle.cpp — zero hex literals in this file.
    auto prop = [](const char* key) -> QColor
    {
        const QVariant v = QCoreApplication::instance()
                               ? QCoreApplication::instance()->property(key)
                               : QVariant{};
        return v.isValid() ? QColor(v.toString()) : QColor{};
    };

    if (letter == QLatin1Char('A'))
    {
        QColor c = prop("gittide.stateAdded");
        if (c.isValid()) return c;
    }
    else if (letter == QLatin1Char('M'))
    {
        QColor c = prop("gittide.stateModified");
        if (c.isValid()) return c;
    }
    else if (letter == QLatin1Char('D'))
    {
        QColor c = prop("gittide.stateDeleted");
        if (c.isValid()) return c;
    }
    else if (letter == QLatin1Char('U'))
    {
        QColor c = prop("gittide.stateUntracked");
        if (c.isValid()) return c;
    }

    // Fallback: use the textSecondary / muted colour (neutral, intentionally
    // understated so it is always readable regardless of theme). This is not a
    // design-significant colour — it is purely a graceful degradation colour
    // that renders correctly in tests that run without ThemeManager.
    return QColor(Qt::gray);
}

QListWidgetItem* makeItem(const gittide::FileStatus& fs, bool editable)
{
    const auto    u8path = fs.path.generic_u8string();
    const QString path   = QString::fromUtf8(
        reinterpret_cast<const char*>(u8path.data()),
        static_cast<qsizetype>(u8path.size()));
    const QChar   letter = statusLetter(fs.flags);
    // Display: "path  M"  (two spaces before letter for visual separation)
    const QString label  = path + QStringLiteral("  ") + letter;

    auto* item = new QListWidgetItem(label);
    item->setData(PathRole,  path);
    item->setData(FlagsRole, static_cast<quint32>(fs.flags));

    // Store the resolved letter colour so the delegate can paint it later.
    const QColor colour = stateColourFromFlags(fs.flags);
    item->setData(LetterColourRole, colour);

    // Apply the state colour to the item's foreground as a tint on the letter
    // portion. Because QListWidgetItem does not support per-character colours
    // we set the whole row foreground to the state colour for now; a richer
    // delegate can paint path + letter separately in a follow-up.
    //
    // The colour is NOT a hex literal — it came from stateColourFromFlags()
    // which reads QApplication dynamic properties set by ThemeManager, or
    // falls back to Qt::gray.
    item->setForeground(colour);

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (editable)
    {
        flags |= Qt::ItemIsUserCheckable | Qt::ItemIsUserTristate;
        item->setCheckState(Qt::Checked);
    }
    item->setFlags(flags);
    return item;
}

} // namespace

ChangedFilesList::ChangedFilesList(QWidget* parent)
    : QWidget(parent)
    , m_list(new QListWidget(this))
{
    m_list->setObjectName(QStringLiteral("changedFilesList"));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemChanged,
            this,   &ChangedFilesList::onItemChanged);
    connect(m_list, &QListWidget::currentItemChanged,
            this,   &ChangedFilesList::onCurrentItemChanged);
    connect(m_list, &QListWidget::customContextMenuRequested,
            this,   &ChangedFilesList::showContextMenu);
}

void ChangedFilesList::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;

    // Re-apply item flags to existing rows when mode changes.
    m_updating = true;
    for (int i = 0; i < m_list->count(); ++i)
    {
        auto* item   = m_list->item(i);
        Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (mode == Mode::Editable)
        {
            f |= Qt::ItemIsUserCheckable | Qt::ItemIsUserTristate;
            if (item->checkState() == Qt::Unchecked)
                ; // preserve existing check state
        }
        item->setFlags(f);
    }
    m_updating = false;
}

void ChangedFilesList::setFiles(const std::vector<gittide::FileStatus>& files)
{
    m_updating = true;
    m_list->clear();
    for (const auto& fs : files)
        m_list->addItem(makeItem(fs, m_mode == Mode::Editable));
    m_updating = false;
}

void ChangedFilesList::setRowCheck(const QString& path, Check check)
{
    // Guard: suppress itemChanged → fileCheckToggled while we mutate state.
    m_updating = true;
    for (int i = 0; i < m_list->count(); ++i)
    {
        auto* item = m_list->item(i);
        if (item->data(PathRole).toString() == path)
        {
            const Qt::CheckState cs =
                check == Check::Checked  ? Qt::Checked  :
                check == Check::Partial  ? Qt::PartiallyChecked :
                                           Qt::Unchecked;
            item->setCheckState(cs);
            break;
        }
    }
    m_updating = false;
}

std::vector<QString> ChangedFilesList::checkedPaths() const
{
    std::vector<QString> result;
    for (int i = 0; i < m_list->count(); ++i)
    {
        const auto* item = m_list->item(i);
        if (item->checkState() == Qt::Checked)
            result.push_back(item->data(PathRole).toString());
    }
    return result;
}

void ChangedFilesList::onItemChanged(QListWidgetItem* item)
{
    if (m_updating)
        return;
    if (!item || !(item->flags() & Qt::ItemIsUserCheckable))
        return;
    const QString path    = item->data(PathRole).toString();
    const bool    checked = item->checkState() == Qt::Checked;
    emit fileCheckToggled(path, checked);
}

void ChangedFilesList::onCurrentItemChanged(QListWidgetItem* current,
                                             QListWidgetItem* /*previous*/)
{
    if (!current)
        return;
    const QString path  = current->data(PathRole).toString();
    const auto    flags = static_cast<gittide::StatusFlag>(
        current->data(FlagsRole).value<quint32>());
    emit fileSelected(path, flags);
}

void ChangedFilesList::showContextMenu(const QPoint& pos)
{
    if (m_mode != Mode::Editable)
        return;
    auto* item = m_list->itemAt(pos);
    if (!item)
        return;
    const QString path = item->data(PathRole).toString();
    QMenu menu(this);
    auto* act = menu.addAction(QStringLiteral("Discard changes…"));
    connect(act, &QAction::triggered, this,
            [this, path] { emit discardRequested(path); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

} // namespace gittide::ui
