#include "gittide/ui/diffselection.hpp"

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

DiffSelection::Pos DiffSelection::orderedStart() const
{
    const bool cursorFirst = m_cursor.row < m_anchor.row ||
                             (m_cursor.row == m_anchor.row && m_cursor.col < m_anchor.col);
    return cursorFirst ? m_cursor : m_anchor;
}

DiffSelection::Pos DiffSelection::orderedEnd() const
{
    const bool cursorFirst = m_cursor.row < m_anchor.row ||
                             (m_cursor.row == m_anchor.row && m_cursor.col < m_anchor.col);
    return cursorFirst ? m_anchor : m_cursor;
}

int DiffSelection::roleOf(const QByteArray& name) const
{
    if (!m_model)
        return -1;
    const auto roles = m_model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}

QString DiffSelection::rowText(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return {};
    const int role = roleOf("lineText");
    if (role < 0)
        return {};
    return m_model->data(m_model->index(row, 0), role).toString();
}

QString DiffSelection::rowKind(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return {};
    const int role = roleOf("lineKind");
    if (role < 0)
        return {};
    return m_model->data(m_model->index(row, 0), role).toString();
}

} // namespace gittide::ui
