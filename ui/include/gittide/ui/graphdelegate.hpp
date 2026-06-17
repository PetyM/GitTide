#pragma once
#include <QStyledItemDelegate>
#include <algorithm>

namespace gittide::ui {

// Custom painter for HistoryModel::ColGraph. Reads the GraphRow (GraphRowRole)
// and draws the lane graph for one row: pass-through verticals, the incoming
// line from above, the commit dot, and outgoing edges down to parent lanes.
// All other columns fall through to the base delegate.
class GraphDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    static constexpr int kLaneWidth = 16;  // pixels per lane column
    static constexpr int kDotRadius = 4;   // commit circle radius in pixels

    explicit GraphDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    void setLaneCount(int count) { laneCount_ = std::max(1, count); }

private:
    int laneCount_ = 1;

    static int laneX(int lane) { return lane * kLaneWidth + kLaneWidth / 2; }
};

}  // namespace gittide::ui
