#include "gittide/ui/historyview.hpp"

#include <QHeaderView>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

#include "gittide/ui/graphdelegate.hpp"
#include "gittide/ui/historymodel.hpp"

namespace gittide::ui {

HistoryView::HistoryView(QWidget* parent)
    : QWidget(parent)
    , model_(new HistoryModel(this))
    , delegate_(new GraphDelegate(this))
    , view_(new QTableView(this))
{
    view_->setModel(model_);
    view_->setItemDelegateForColumn(HistoryModel::ColGraph, delegate_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setShowGrid(false);
    view_->verticalHeader()->hide();
    view_->horizontalHeader()->setStretchLastSection(false);
    view_->horizontalHeader()->setSectionResizeMode(HistoryModel::ColSummary, QHeaderView::Stretch);
    view_->setObjectName(QStringLiteral("historyTable"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);
}

void HistoryView::setHistory(const gittide::GraphLayout& layout)
{
    model_->setLayout(layout);
    delegate_->setLaneCount(layout.laneCount);
    // Size the graph column to fit the lanes; ColSummary stretches to fill.
    view_->horizontalHeader()->resizeSection(
        HistoryModel::ColGraph, std::max(1, layout.laneCount) * GraphDelegate::kLaneWidth + GraphDelegate::kLaneWidth);
}

} // namespace gittide::ui
