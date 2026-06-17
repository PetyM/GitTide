#pragma once
#include <QAbstractListModel>

namespace gitgui { class ProjectStore; }

namespace gitgui::ui {

// Read model over ProjectStore::projects(). The store is owned elsewhere
// (WindowManager); this model only references it. Call refresh() after the
// underlying project list changes.
class ProjectListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1 };

    explicit ProjectListModel(gitgui::ProjectStore* store, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();

private:
    gitgui::ProjectStore* store_;
};

}  // namespace gitgui::ui
