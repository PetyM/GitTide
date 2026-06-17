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
    , m_model(new HistoryModel(this))
    , m_delegate(new GraphDelegate(this))
    , m_view(new QTableView(this))
{
    m_view->setModel(m_model);
    m_view->setItemDelegateForColumn(HistoryModel::ColGraph, m_delegate);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setShowGrid(false);
    m_view->verticalHeader()->hide();
    m_view->horizontalHeader()->setStretchLastSection(false);
    m_view->horizontalHeader()->setSectionResizeMode(HistoryModel::ColSummary, QHeaderView::Stretch);
    m_view->setObjectName(QStringLiteral("historyTable"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
}

void HistoryView::setHistory(const gittide::GraphLayout& layout)
{
    m_model->setLayout(layout);
    m_delegate->setLaneCount(layout.laneCount);
    // Size the graph column to fit the lanes; ColSummary stretches to fill.
    m_view->horizontalHeader()->resizeSection(
        HistoryModel::ColGraph, std::max(1, layout.laneCount) * GraphDelegate::kLaneWidth + GraphDelegate::kLaneWidth);
}

} // namespace gittide::ui
