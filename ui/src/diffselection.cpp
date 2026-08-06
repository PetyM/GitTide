#include "gittide/ui/diffselection.hpp"

namespace {
bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}
} // namespace

namespace gittide::ui {

DiffSelection::DiffSelection(QObject* parent)
    : QObject(parent)
{
}

QAbstractItemModel* DiffSelection::model() const
{
    return m_model;
}

void DiffSelection::setModel(QAbstractItemModel* model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model)
    {
        // Any structural change invalidates row indices — drop the selection
        // rather than let it point at rows that mean something else now.
        connect(m_model, &QAbstractItemModel::modelReset, this, &DiffSelection::clear);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &DiffSelection::clear);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &DiffSelection::clear);
        connect(m_model, &QAbstractItemModel::layoutChanged, this, &DiffSelection::clear);
    }
    cacheRoles();
    clear();
    emit modelChanged();
}

bool DiffSelection::hasSelection() const
{
    if (m_anchor.row < 0 || m_cursor.row < 0)
        return false;
    return m_anchor.row != m_cursor.row || m_anchor.col != m_cursor.col;
}

int DiffSelection::anchorRow() const
{
    return m_anchor.row;
}

void DiffSelection::begin(int row, int col)
{
    m_anchor = Pos{row, col};
    m_cursor = m_anchor;
    emit selectionChanged();
}

void DiffSelection::extendTo(int row, int col)
{
    if (m_anchor.row < 0)
    {
        begin(row, col);
        return;
    }
    m_cursor = Pos{row, col};
    emit selectionChanged();
}

void DiffSelection::selectAll()
{
    const int rows = m_model ? m_model->rowCount() : 0;
    if (rows == 0)
    {
        clear();
        return;
    }
    m_anchor = Pos{0, 0};
    m_cursor = Pos{rows - 1, rowLength(rows - 1)};
    emit selectionChanged();
}

void DiffSelection::clear()
{
    m_anchor = Pos{};
    m_cursor = Pos{};
    emit selectionChanged();
}

void DiffSelection::selectWord(int row, int col)
{
    const QString text = rowText(row);
    if (text.isEmpty())
    {
        begin(row, 0);
        return;
    }

    const int c     = qBound(0, col, static_cast<int>(text.size()) - 1);
    int       start = c;
    int       end   = c + 1;
    if (isWordChar(text.at(c)))
    {
        while (start > 0 && isWordChar(text.at(start - 1)))
            --start;
        end = c;
        while (end < text.size() && isWordChar(text.at(end)))
            ++end;
    }

    m_anchor = Pos{row, start};
    m_cursor = Pos{row, end};
    emit selectionChanged();
}

void DiffSelection::selectLine(int row)
{
    m_anchor = Pos{row, 0};
    m_cursor = Pos{row, rowLength(row)};
    emit selectionChanged();
}

int DiffSelection::startInRow(int row) const
{
    if (!hasSelection())
        return -1;
    const Pos s = orderedStart();
    const Pos e = orderedEnd();
    if (row < s.row || row > e.row)
        return -1;
    return row == s.row ? qBound(0, s.col, rowLength(row)) : 0;
}

int DiffSelection::endInRow(int row) const
{
    if (!hasSelection())
        return -1;
    const Pos s = orderedStart();
    const Pos e = orderedEnd();
    if (row < s.row || row > e.row)
        return -1;
    const int len = rowLength(row);
    return row == e.row ? qBound(0, e.col, len) : len;
}

int DiffSelection::rowLength(int row) const
{
    return static_cast<int>(rowText(row).size());
}

bool DiffSelection::cursorIsFirst() const
{
    return m_cursor.row < m_anchor.row ||
           (m_cursor.row == m_anchor.row && m_cursor.col < m_anchor.col);
}

DiffSelection::Pos DiffSelection::orderedStart() const
{
    return cursorIsFirst() ? m_cursor : m_anchor;
}

DiffSelection::Pos DiffSelection::orderedEnd() const
{
    return cursorIsFirst() ? m_anchor : m_cursor;
}

void DiffSelection::cacheRoles()
{
    m_lineTextRole = -1;
    m_lineKindRole = -1;
    if (!m_model)
        return;
    const auto roles = m_model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
    {
        if (it.value() == "lineText")
            m_lineTextRole = it.key();
        else if (it.value() == "lineKind")
            m_lineKindRole = it.key();
    }
}

QVariant DiffSelection::dataFor(int row, int role) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount() || role < 0)
        return {};
    return m_model->data(m_model->index(row, 0), role);
}

QString DiffSelection::rowText(int row) const
{
    return dataFor(row, m_lineTextRole).toString();
}

QString DiffSelection::rowKind(int row) const
{
    return dataFor(row, m_lineKindRole).toString();
}

QString DiffSelection::copyText(bool withMarkers) const
{
    if (!hasSelection())
        return {};

    const Pos s = orderedStart();
    const Pos e = orderedEnd();
    QString   out;
    for (int row = s.row; row <= e.row; ++row)
    {
        const QString kind = rowKind(row);
        if (kind == QLatin1String("block"))
            continue;
        const int from = startInRow(row);
        const int to   = endInRow(row);
        if (from < 0 || to < from)
            continue;

        QString slice = rowText(row).mid(from, to - from);
        if (withMarkers && kind != QLatin1String("hunk"))
        {
            const QChar sign = kind == QLatin1String("added")     ? QLatin1Char('+')
                               : kind == QLatin1String("removed") ? QLatin1Char('-')
                                                                  : QLatin1Char(' ');
            slice.prepend(QString(sign) + QLatin1Char(' '));
        }
        out += slice;
        out += QLatin1Char('\n');
    }
    return out;
}

} // namespace gittide::ui
