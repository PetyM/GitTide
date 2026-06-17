#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectListModel.hpp"
#include "gitgui/ui/RepoListModel.hpp"

#include <QComboBox>
#include <QItemSelectionModel>
#include <QListView>
#include <QVBoxLayout>

namespace gitgui::ui {

ProjectSidebar::ProjectSidebar(ProjectController* controller, QWidget* parent)
    : QWidget(parent),
      controller_(controller),
      switcher_(new QComboBox(this)),
      repoList_(new QListView(this)) {
    switcher_->setObjectName(QStringLiteral("projectSwitcher"));
    switcher_->setModel(controller_->projects());
    switcher_->setCurrentIndex(-1);  // so the first user selection always emits

    repoList_->setObjectName(QStringLiteral("repoList"));
    repoList_->setModel(controller_->repos());
    repoList_->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(switcher_);
    layout->addWidget(repoList_, /*stretch=*/1);

    // Selecting a repo row emits repoSelected.
    connect(repoList_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (!current.isValid()) return;
                emit repoSelected(current.data(RepoListModel::PathRole).toString());
            });

    // Switching the combo activates the matching project.
    connect(switcher_, &QComboBox::currentIndexChanged, this, [this](int row) {
        if (row < 0) return;
        const QString id =
            controller_->projects()->data(controller_->projects()->index(row),
                                           ProjectListModel::IdRole).toString();
        controller_->activate(id);
    });
}

void ProjectSidebar::requestOpenInNewWindow() {
    const QString id = controller_->activeProjectId();
    if (!id.isEmpty()) emit openInNewWindowRequested(id);
}

}  // namespace gitgui::ui
