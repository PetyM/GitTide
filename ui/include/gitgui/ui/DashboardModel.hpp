#pragma once
#include <QAbstractListModel>
#include <vector>
#include "gitgui/ProjectStore.hpp"

namespace gitgui::ui {

// Read-only aggregated status for the project Dashboard tab.
// Plan 2: refresh() opens each repo and reads status synchronously.
// Plan 3: the per-repo work moves to parallel QCoro tasks; the model's
// row layout and roles stay the same.
class DashboardModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { PathRole = Qt::UserRole + 1, ChangeCountRole, MissingRole };

    explicit DashboardModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Recompute rows from the given repos (synchronous).
    void refresh(const std::vector<gitgui::RepoRef>& repos);

private:
    struct Row { QString alias; QString path; int changeCount; bool missing; };
    std::vector<Row> rows_;
};

}  // namespace gitgui::ui
