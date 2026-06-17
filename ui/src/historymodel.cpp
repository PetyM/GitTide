#include "gittide/ui/historymodel.hpp"
#include "gittide/ui/metatypes.hpp"

#include <QDateTime>

namespace gittide::ui {

HistoryModel::HistoryModel(QObject* parent) : QAbstractTableModel(parent) {
    qRegisterMetaType<gittide::GraphLayout>();
    qRegisterMetaType<gittide::GraphRow>();
}

void HistoryModel::setLayout(const gittide::GraphLayout& layout) {
    beginResetModel();
    layout_ = layout;
    endResetModel();
}

int HistoryModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(layout_.rows.size());
}

int HistoryModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return ColCount;
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const auto row = static_cast<std::size_t>(index.row());
    if (row >= layout_.rows.size()) return {};
    const auto& gr = layout_.rows[row];

    if (role == GraphRowRole && index.column() == ColGraph)
        return QVariant::fromValue(gr);

    if (role != Qt::DisplayRole) return {};

    switch (index.column()) {
        case ColGraph:
            return {};
        case ColSummary:
            return QString::fromStdString(gr.commit.summary);
        case ColAuthor:
            return QString::fromStdString(gr.commit.author);
        case ColDate: {
            const QDateTime dt = QDateTime::fromSecsSinceEpoch(gr.commit.time);
            return dt.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        }
        default:
            return {};
    }
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case ColGraph:
            return QStringLiteral("Graph");
        case ColSummary:
            return QStringLiteral("Summary");
        case ColAuthor:
            return QStringLiteral("Author");
        case ColDate:
            return QStringLiteral("Date");
        default:
            return {};
    }
}

}  // namespace gittide::ui
