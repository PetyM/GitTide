#pragma once
#include <QColor>
#include <QQuickPaintedItem>
#include <QVariant>
#include <QVariantList>

#include "gittide/graph.hpp"

namespace gittide::ui {

/// Scene-graph item that paints ONE commit row's lane graph: pass-through
/// verticals, the incoming line from above, the commit dot, and outgoing edges
/// down to parent lanes. Ports GraphDelegate::paint (the QWidget delegate) to
/// Qt Quick. Each lane is drawn in laneColors[lane % count]; the dot is the
/// commit's lane colour unless `head` is set, in which case it is headColor
/// (white). Used inside the History ListView delegate, one instance per row.
class GraphColumn : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariant graphRow READ graphRow WRITE setGraphRow NOTIFY changed)
    Q_PROPERTY(QVariantList laneColors READ laneColors WRITE setLaneColors NOTIFY changed)
    Q_PROPERTY(QColor headColor READ headColor WRITE setHeadColor NOTIFY changed)
    Q_PROPERTY(int laneCount READ laneCount WRITE setLaneCount NOTIFY changed)
    Q_PROPERTY(bool head READ head WRITE setHead NOTIFY changed)
public:
    static constexpr int kLaneWidth = 16;
    static constexpr int kDotRadius = 4;

    explicit GraphColumn(QQuickItem* parent = nullptr);

    QVariant graphRow() const { return m_graphRow; }
    QVariantList laneColors() const { return m_laneColors; }
    QColor headColor() const { return m_headColor; }
    int laneCount() const { return m_laneCount; }
    bool head() const { return m_head; }

    void setGraphRow(const QVariant& row);
    void setLaneColors(const QVariantList& colors);
    void setHeadColor(const QColor& color);
    void setLaneCount(int count);
    void setHead(bool head);

    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    QColor laneColor(int lane) const;
    static int laneX(int lane)
    {
        return lane * kLaneWidth + kLaneWidth / 2;
    }

    QVariant         m_graphRow;
    QVariantList     m_laneColors;
    QColor           m_headColor = Qt::white;
    int              m_laneCount = 1;
    bool             m_head      = false;
};

} // namespace gittide::ui
