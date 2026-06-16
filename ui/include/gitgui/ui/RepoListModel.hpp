#pragma once
#include <QAbstractListModel>
#include <vector>
#include "gitgui/ProjectStore.hpp"

namespace gitgui::ui {

// Read model over a single project's repos. setRepos() resets the model and
// recomputes the on-disk "missing" flag for each entry.
class RepoListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { PathRole = Qt::UserRole + 1, MissingRole };

    explicit RepoListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRepos(const std::vector<gitgui::RepoRef>& repos);

private:
    struct Row { QString alias; QString path; bool missing; };
    std::vector<Row> rows_;
};

}  // namespace gitgui::ui
