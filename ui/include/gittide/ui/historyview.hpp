#pragma once
#include <QModelIndex>
#include <QString>
#include <QWidget>

#include "gittide/graph.hpp"

class QTableView;

namespace gittide::ui {

class HistoryModel;
class GraphDelegate;

// History tab widget: a QTableView wired to a HistoryModel with a GraphDelegate
// painting the lane graph in column 0. setHistory() loads a new GraphLayout.
class HistoryView : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryView(QWidget* parent = nullptr);

    // Loads a new GraphLayout into the model and resizes the graph column.
    // Named setHistory (not setLayout) to avoid shadowing QWidget::setLayout.
    void setHistory(const gittide::GraphLayout& layout);

    // Returns the full SHA-1 hex OID for the given model index (any column),
    // or an empty string if the index is invalid.
    QString oidForRow(const QModelIndex& index) const;

    // Programmatic signal triggers — usable from tests without exec()ing a QMenu.
    void emitNewBranchFor(const QModelIndex& index);
    void emitCheckoutFor(const QModelIndex& index);

signals:
    // Emitted when the user requests a new branch from the commit at this OID.
    void newBranchFromCommitRequested(const QString& oid);
    // Emitted when the user requests a detached-HEAD checkout of this commit.
    void checkoutCommitRequested(const QString& oid);

private slots:
    // Connected to QTableView::customContextMenuRequested.
    void onContextMenuRequested(const QPoint& pos);

private:
    HistoryModel* m_model;
    GraphDelegate* m_delegate;
    QTableView* m_view;
};

} // namespace gittide::ui
