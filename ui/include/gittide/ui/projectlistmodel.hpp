#pragma once
#include <QAbstractListModel>

namespace gittide { class ProjectStore; }

namespace gittide::ui {

// Read model over ProjectStore::projects(). The store is owned elsewhere
// (WindowManager); this model only references it. Call refresh() after the
// underlying project list changes.
class ProjectListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1 };

    explicit ProjectListModel(gittide::ProjectStore* store, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();

private:
    gittide::ProjectStore* store_;
};

}  // namespace gittide::ui
