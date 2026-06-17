#pragma once
#include <QWidget>

#include "gittide/Graph.hpp"

class QTableView;

namespace gittide::ui {

class HistoryModel;
class GraphDelegate;

// History tab widget: a QTableView wired to a HistoryModel with a GraphDelegate
// painting the lane graph in column 0. setHistory() loads a new GraphLayout.
class HistoryView : public QWidget {
    Q_OBJECT
public:
    explicit HistoryView(QWidget* parent = nullptr);

    // Loads a new GraphLayout into the model and resizes the graph column.
    // Named setHistory (not setLayout) to avoid shadowing QWidget::setLayout.
    void setHistory(const gittide::GraphLayout& layout);

private:
    HistoryModel* model_;
    GraphDelegate* delegate_;
    QTableView* view_;
};

}  // namespace gittide::ui
