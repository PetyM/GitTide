#pragma once
#include <QAbstractTableModel>

#include "gittide/graph.hpp"

namespace gittide::ui {

// Table model backing the History tab. Rows come from a GraphLayout (Plan 5a);
// column 0 carries the GraphRow (via GraphRowRole) for GraphDelegate to paint,
// columns 1-3 expose summary/author/date as plain DisplayRole text. Qt's
// model/view layer virtualizes rendering, so only visible rows are queried —
// a 100k-commit log never stalls the UI.
class HistoryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column
    {
        ColGraph = 0,
        ColSummary,
        ColAuthor,
        ColDate,
        ColCount
    };
    enum Role
    {
        GraphRowRole = Qt::UserRole + 1
    };

    explicit HistoryModel(QObject* parent = nullptr);

    void setLayout(const gittide::GraphLayout& layout);
    int laneCount() const
    {
        return layout_.laneCount;
    }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    gittide::GraphLayout layout_;
};

} // namespace gittide::ui
