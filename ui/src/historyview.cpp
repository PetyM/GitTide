#include "gittide/ui/historyview.hpp"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QPoint>
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
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &HistoryView::onContextMenuRequested);

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

QString HistoryView::oidForRow(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};
    // Normalise to column 0 so GraphRowRole is available regardless of which
    // column the caller happened to use.
    auto graphIdx = m_model->index(index.row(), HistoryModel::ColGraph);
    auto val      = m_model->data(graphIdx, HistoryModel::GraphRowRole);
    if (!val.canConvert<gittide::GraphRow>())
        return {};
    return QString::fromStdString(val.value<gittide::GraphRow>().commit.oid);
}

void HistoryView::emitNewBranchFor(const QModelIndex& index)
{
    const QString oid = oidForRow(index);
    if (!oid.isEmpty())
        emit newBranchFromCommitRequested(oid);
}

void HistoryView::emitCheckoutFor(const QModelIndex& index)
{
    const QString oid = oidForRow(index);
    if (!oid.isEmpty())
        emit checkoutCommitRequested(oid);
}

void HistoryView::onContextMenuRequested(const QPoint& pos)
{
    const QModelIndex index = m_view->indexAt(pos);
    if (!index.isValid())
        return;
    const QString oid = oidForRow(index);
    if (oid.isEmpty())
        return;

    QMenu menu(this);
    auto* branchAction   = menu.addAction(tr("New branch from here…"));
    auto* checkoutAction = menu.addAction(tr("Checkout this commit"));

    connect(branchAction, &QAction::triggered, this,
        [this, oid]()
        {
            emit newBranchFromCommitRequested(oid);
        });
    connect(checkoutAction, &QAction::triggered, this,
        [this, oid]()
        {
            emit checkoutCommitRequested(oid);
        });

    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

} // namespace gittide::ui
