#pragma once
#include <QAbstractListModel>
#include <qcorotask.h>
#include <vector>

#include "gittide/projectstore.hpp"

namespace gittide::ui {

// Read-only aggregated status for the project Dashboard tab.
// Plan 2: refresh() opens each repo and reads status synchronously.
// Plan 3: the per-repo work moves to parallel QCoro tasks; the model's
// row layout and roles stay the same.
class DashboardModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        PathRole = Qt::UserRole + 1,
        ChangeCountRole,
        MissingRole
    };

    explicit DashboardModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Recompute rows from the given repos (synchronous).
    void refresh(const std::vector<gittide::RepoRef>& repos);

    // Recompute rows in parallel: one pool task per repo, each opening its OWN
    // GitRepo (no shared state). Emits refreshed() when all results are gathered.
    QCoro::Task<void> refreshAsync(std::vector<gittide::RepoRef> repos);

signals:
    void refreshed();

private:
    struct Row
    {
        QString alias;
        QString path;
        int changeCount;
        bool missing;
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
