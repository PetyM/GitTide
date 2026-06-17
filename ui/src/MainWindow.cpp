#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/ChangesView.hpp"
#include "gitgui/ui/DashboardModel.hpp"

#include <filesystem>
#include <vector>

#include <qcorotask.h>

#include <QDockWidget>
#include <QLabel>
#include <QListView>
#include <QTabWidget>

namespace gitgui::ui {

MainWindow::MainWindow(gitgui::ProjectStore* store,
                       std::filesystem::path storePath,
                       QWidget* parent)
    : QMainWindow(parent),
      controller_(new ProjectController(store, std::move(storePath), this)),
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
    // A coroutine slot returns a QCoro::Task that QCoro destroys as soon as the
    // handle dies — a discarded fire-and-forget task awaiting a QFuture is a
    // use-after-free when that future completes. QCoro::connect anchors the task
    // (tied to `this`) until it finishes, which is how these are launched.
    connect(repoController_, &RepoController::repoOpened, this, [this](const QString& path) {
        emit repoOpened(path);
        QCoro::connect(repoController_->refreshStatus(), this, [] {});
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
                QCoro::connect(repoController_->refreshDiff(path, target), this, [] {});
            });
    connect(changesView_, &ChangesView::stageRequested, this,
            [this](const gitgui::StageSelection& sel) {
                QCoro::connect(repoController_->stage(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::unstageRequested, this,
            [this](const gitgui::StageSelection& sel) {
                QCoro::connect(repoController_->unstage(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::discardRequested, this,
            [this](const gitgui::StageSelection& sel) {
                QCoro::connect(repoController_->discard(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::commitRequested, this,
            [this](const gitgui::CommitRequest& req) {
                QCoro::connect(repoController_->commit(req), this, [] {});
            });

    // Activating a project refreshes the dashboard from its repos.
    connect(controller_, &ProjectController::projectActivated, this,
            [this](const QString&) {
                QCoro::connect(dashboardModel_->refreshAsync(controller_->activeRepos()),
                               this, [] {});
            });
}

QString MainWindow::currentProjectId() const {
    return controller_->activeProjectId();
}

void MainWindow::showProject(const QString& projectId) {
    controller_->activate(projectId);
}

}  // namespace gitgui::ui
