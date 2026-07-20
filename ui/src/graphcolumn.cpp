#include "gittide/ui/graphcolumn.hpp"

#include <QPainter>

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

GraphColumn::GraphColumn(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    qRegisterMetaType<gittide::GraphRow>();
    setImplicitWidth(m_laneCount * kLaneWidth);
}

void GraphColumn::setGraphRow(const QVariant& row)
{
    m_graphRow = row;
    emit changed();
    update();
}

void GraphColumn::setLaneColors(const QVariantList& colors)
{
    m_laneColors = colors;
    emit changed();
    update();
}

void GraphColumn::setHeadColor(const QColor& color)
{
    m_headColor = color;
    emit changed();
    update();
}

void GraphColumn::setLaneCount(int count)
{
    m_laneCount = count < 1 ? 1 : count;
    setImplicitWidth(m_laneCount * kLaneWidth);
    emit changed();
    update();
}

void GraphColumn::setHead(bool head)
{
    m_head = head;
    emit changed();
    update();
}

void GraphColumn::setLocalOnly(bool localOnly)
{
    m_localOnly = localOnly;
    emit changed();
    update();
}

QColor GraphColumn::laneColor(int lane) const
{
    if (m_laneColors.isEmpty())
        return Qt::gray;
    return m_laneColors.at(lane % m_laneColors.size()).value<QColor>();
}

void GraphColumn::paint(QPainter* painter)
{
    if (!m_graphRow.canConvert<gittide::GraphRow>())
        return;
    const auto row = m_graphRow.value<gittide::GraphRow>();

    painter->setRenderHint(QPainter::Antialiasing, true);

    const int top = 0;
    const int bot = static_cast<int>(height());
    const int mid = bot / 2;
    const int cx  = laneX(row.commit.lane);

    auto pen = [&](int lane)
    {
        painter->setPen(QPen(laneColor(lane), 1.5));
    };

    // 1. Pass-through verticals span the full cell, each in its own lane colour.
    for (int lane : row.passThroughs)
    {
        pen(lane);
        painter->drawLine(laneX(lane), top, laneX(lane), bot);
    }

    // 2. Incoming line to the circle (top half), in the commit's lane colour.
    if (row.lineFromAbove)
    {
        pen(row.commit.lane);
        painter->drawLine(cx, top, cx, mid);
    }

    // 3. Outgoing edges to parent lanes (bottom half), coloured by destination lane.
    for (const auto& e : row.outEdges)
    {
        pen(e.toLane);
        painter->drawLine(laneX(e.fromLane), mid, laneX(e.toLane), bot);
    }

    // 4. Commit dot — lane colour, or white for HEAD. A local-only (not-yet-pushed)
    //    commit is drawn hollow: stroked outline, transparent fill — a shape cue
    //    paired with the row's badge so state is never signalled by colour alone.
    const QColor dot = m_head ? m_headColor : laneColor(row.commit.lane);
    if (m_localOnly)
    {
        painter->setPen(QPen(dot, 1.5));
        painter->setBrush(Qt::NoBrush);
    }
    else
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(dot);
    }
    painter->drawEllipse(QPoint(cx, mid), kDotRadius, kDotRadius);
}

} // namespace gittide::ui
