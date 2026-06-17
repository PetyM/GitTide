#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/ChangesView.hpp"
#include "gitgui/ui/DashboardModel.hpp"

#include <filesystem>
#include <vector>

#include <QDockWidget>
#include <QLabel>
#include <QListView>
#include <QTabWidget>

namespace gitgui::ui {

MainWindow::MainWindow(gitgui::ProjectStore* store, QWidget* parent)
    : QMainWindow(parent),
      controller_(new ProjectController(store, this)),
      sidebar_(new ProjectSidebar(controller_, this)),
      repoController_(new RepoController(this)),
      changesView_(new ChangesView(this)),
      dashboardModel_(new DashboardModel(this)) {
    setWindowTitle(QStringLiteral("GitGUI"));

    auto* dock = new QDockWidget(QStringLiteral("Projects"), this);
    dock->setObjectName(QStringLiteral("projectsDock"));
    dock->setWidget(sidebar_);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("mainTabs"));
    tabs->addTab(changesView_, QStringLiteral("Changes"));
    tabs->addTab(new QLabel(QStringLiteral("History — Plan 4")), QStringLiteral("History"));
    auto* dashboardView = new QListView(this);
    dashboardView->setObjectName(QStringLiteral("dashboardList"));
    dashboardView->setModel(dashboardModel_);
    tabs->addTab(dashboardView, QStringLiteral("Dashboard"));
    setCentralWidget(tabs);

    connect(sidebar_, &ProjectSidebar::openInNewWindowRequested,
            this, &MainWindow::openInNewWindowRequested);

    // Selecting a repo opens it asynchronously and refreshes the Changes tab.
    connect(sidebar_, &ProjectSidebar::repoSelected, this, [this](const QString& path) {
        repoController_->open(path);
    });
    connect(repoController_, &RepoController::repoOpened, this, [this](const QString& path) {
        emit repoOpened(path);
        repoController_->refreshStatus();
    });

    // Async wiring between controller and ChangesView.
    connect(repoController_, &RepoController::statusChanged,
            changesView_, &ChangesView::setStatus);
    connect(repoController_, &RepoController::diffReady, this,
            [this](const QString& path, const gitgui::DiffResult& result) {
                changesView_->setDiff(result, std::filesystem::path(path.toStdString()));
            });
    connect(changesView_, &ChangesView::fileSelected, this,
            [this](const QString& path, gitgui::DiffTarget target) {
                repoController_->refreshDiff(path, target);
            });
    connect(changesView_, &ChangesView::stageRequested,
            repoController_, &RepoController::stage);
    connect(changesView_, &ChangesView::unstageRequested,
            repoController_, &RepoController::unstage);
    connect(changesView_, &ChangesView::discardRequested,
            repoController_, &RepoController::discard);
    connect(changesView_, &ChangesView::commitRequested,
            repoController_, &RepoController::commit);

    // Activating a project refreshes the dashboard from its repos.
    connect(controller_, &ProjectController::projectActivated, this,
            [this](const QString&) {
                dashboardModel_->refreshAsync(controller_->activeRepos());
            });
}

QString MainWindow::currentProjectId() const {
    return controller_->activeProjectId();
}

void MainWindow::showProject(const QString& projectId) {
    controller_->activate(projectId);
}

}  // namespace gitgui::ui
