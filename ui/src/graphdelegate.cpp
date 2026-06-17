#include "gittide/ui/graphdelegate.hpp"

#include <QPainter>
#include <QStyleOptionViewItem>

#include "gittide/graph.hpp"
#include "gittide/ui/historymodel.hpp"
#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

GraphDelegate::GraphDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize GraphDelegate::sizeHint(const QStyleOptionViewItem& option,
                              const QModelIndex& /*index*/) const {
    return QSize(laneCount_ * kLaneWidth + kLaneWidth, option.rect.height());
}

void GraphDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const {
    // Only handle the graph column; delegate everything else to the base.
    if (index.column() != HistoryModel::ColGraph) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    auto val = index.data(HistoryModel::GraphRowRole);
    if (!val.canConvert<gittide::GraphRow>()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const auto row = val.value<gittide::GraphRow>();

    painter->save();
    painter->setClipRect(option.rect);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QColor lineColor = painter->pen().color();
    painter->setPen(QPen(lineColor, 1.5));

    const int top = option.rect.top();
    const int bot = option.rect.bottom();
    const int mid = option.rect.center().y();
    const int cx = option.rect.left() + laneX(row.commit.lane);

    auto x = [&](int lane) { return option.rect.left() + laneX(lane); };

    // 1. Pass-through lines span the full cell height.
    for (int lane : row.passThroughs) painter->drawLine(x(lane), top, x(lane), bot);

    // 2. Incoming line to the circle (top half of the cell).
    if (row.lineFromAbove) painter->drawLine(cx, top, cx, mid);

    // 3. Commit circle.
    painter->setBrush(lineColor);
    painter->drawEllipse(QPoint(cx, mid), kDotRadius, kDotRadius);
    painter->setBrush(Qt::NoBrush);

    // 4. Outgoing edges to parent lanes (bottom half of the cell).
    for (const auto& e : row.outEdges) painter->drawLine(x(e.fromLane), mid, x(e.toLane), bot);

    painter->restore();
}

}  // namespace gittide::ui
