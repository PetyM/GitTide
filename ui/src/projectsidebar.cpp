#include "gittide/ui/projectsidebar.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/projectlistmodel.hpp"
#include "gittide/ui/repolistmodel.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace gittide::ui {

static constexpr auto kSentinel = "__new__";

ProjectSidebar::ProjectSidebar(ProjectController* controller, QWidget* parent)
    : QWidget(parent),
      controller_(controller),
      switcher_(new QComboBox(this)),
      removeProjectBtn_(new QToolButton(this)),
      repoList_(new QTreeView(this)),
      addExistingBtn_(new QToolButton(this)),
      initRepoBtn_(new QToolButton(this)),
      cloneBtn_(new QToolButton(this)) {

    switcher_->setObjectName(QStringLiteral("projectSwitcher"));
    switcher_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    removeProjectBtn_->setObjectName(QStringLiteral("removeProjectButton"));
    removeProjectBtn_->setText(QStringLiteral("✕"));
    removeProjectBtn_->setToolTip(QStringLiteral("Remove current project"));

    repoList_->setObjectName(QStringLiteral("repoList"));
    repoList_->setModel(controller_->repos());
    repoList_->header()->hide();
    repoList_->setRootIsDecorated(true);
    repoList_->setContextMenuPolicy(Qt::CustomContextMenu);

    addExistingBtn_->setObjectName(QStringLiteral("addExistingButton"));
    addExistingBtn_->setText(QStringLiteral("Add"));
    addExistingBtn_->setToolTip(QStringLiteral("Add existing repository"));

    initRepoBtn_->setObjectName(QStringLiteral("initRepoButton"));
    initRepoBtn_->setText(QStringLiteral("Init"));
    initRepoBtn_->setToolTip(QStringLiteral("Initialize new repository"));

    cloneBtn_->setObjectName(QStringLiteral("cloneButton"));
    cloneBtn_->setText(QStringLiteral("Clone"));
    cloneBtn_->setToolTip(QStringLiteral("Clone repository from URL"));

    auto* switcherRow = new QWidget(this);
    auto* switcherLayout = new QHBoxLayout(switcherRow);
    switcherLayout->setContentsMargins(0, 0, 0, 0);
    switcherLayout->addWidget(switcher_);
    switcherLayout->addWidget(removeProjectBtn_);

    auto* toolbar = new QWidget(this);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->addWidget(addExistingBtn_);
    toolbarLayout->addWidget(initRepoBtn_);
    toolbarLayout->addWidget(cloneBtn_);
    toolbarLayout->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(switcherRow);
    layout->addWidget(repoList_, /*stretch=*/1);
    layout->addWidget(toolbar);

    // Populate combo and keep it in sync
    syncCombo();
    connect(controller_, &ProjectController::projectCreated, this,
            [this](const QString&) { syncCombo(); });
    connect(controller_, &ProjectController::projectActivated, this,
            [this](const QString&) { syncCombo(); });
    connect(controller_, &ProjectController::projectRemoved, this,
            [this](const QString&) { syncCombo(); });

    // Selecting a repo row emits repoSelected.
    connect(repoList_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (!current.isValid()) return;
                emit repoSelected(
                    current.data(RepoListModel::PathRole).toString());
            });

    // Combo selection — detect sentinel.
    connect(switcher_, &QComboBox::currentIndexChanged, this, [this](int row) {
        if (row < 0) return;
        const QString id = switcher_->itemData(row).toString();
        if (id == QString::fromLatin1(kSentinel)) {
            emit createProjectRequested();
            syncCombo();  // restores selection to current active project
            return;
        }
        controller_->activate(id);
    });

    // Remove project button
    connect(removeProjectBtn_, &QToolButton::clicked, this, [this]() {
        if (controller_->activeProjectId().isEmpty()) return;
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Remove Project"),
            QStringLiteral("Remove the current project? Repositories on disk are not affected."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Yes) controller_->removeProject();
    });

    // Repo list context menu
    connect(repoList_, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const QModelIndex idx = repoList_->indexAt(pos);
                if (!idx.isValid()) return;
                const QString path = idx.data(RepoListModel::PathRole).toString();
                if (path.isEmpty()) return;
                QMenu menu(this);
                auto* removeAction = menu.addAction(QStringLiteral("Remove from project"));
                if (menu.exec(repoList_->viewport()->mapToGlobal(pos)) == removeAction) {
                    controller_->removeRepo(path);
                }
            });

    // Toolbar buttons
    connect(addExistingBtn_, &QToolButton::clicked, this,
            &ProjectSidebar::addExistingRequested);
    connect(initRepoBtn_, &QToolButton::clicked, this,
            &ProjectSidebar::initRepoRequested);
    connect(cloneBtn_, &QToolButton::clicked, this,
            &ProjectSidebar::cloneRepoRequested);
}

void ProjectSidebar::syncCombo() {
    const QSignalBlocker blocker(switcher_);
    switcher_->clear();
    auto* model = controller_->projects();
    for (int i = 0; i < model->rowCount(); ++i) {
        switcher_->addItem(
            model->data(model->index(i), Qt::DisplayRole).toString(),
            model->data(model->index(i), ProjectListModel::IdRole));
    }
    switcher_->addItem(QStringLiteral("New project…"),
                       QString::fromLatin1(kSentinel));

    // Restore active project selection
    const QString activeId = controller_->activeProjectId();
    if (!activeId.isEmpty()) {
        for (int i = 0; i < model->rowCount(); ++i) {
            if (switcher_->itemData(i).toString() == activeId) {
                switcher_->setCurrentIndex(i);
                return;
            }
        }
    }
    switcher_->setCurrentIndex(-1);
}

void ProjectSidebar::requestOpenInNewWindow() {
    const QString id = controller_->activeProjectId();
    if (!id.isEmpty()) emit openInNewWindowRequested(id);
}

}  // namespace gittide::ui
