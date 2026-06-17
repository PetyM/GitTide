#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/ChangesView.hpp"
#include "gitgui/ui/DashboardModel.hpp"
#include "gitgui/ui/AddRepoDialogs.hpp"

#include <filesystem>
#include <vector>

#include <qcorotask.h>

#include <QDockWidget>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFileDialog>

namespace gitgui::ui {

// ---- helpers ----
namespace {
QWidget* makeNoProjectsPage(QWidget* parent) {
    auto* w = new QWidget(parent);
    w->setObjectName(QStringLiteral("noProjectsPage"));
    auto* btn = new QPushButton(QStringLiteral("Create Project"), w);
    btn->setObjectName(QStringLiteral("createProjectCta"));
    auto* layout = new QVBoxLayout(w);
    layout->addStretch();
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(btn);
    row->addStretch();
    layout->addLayout(row);
    layout->addStretch();
    return w;
}

QWidget* makeNoReposPage(QWidget* parent) {
    auto* w = new QWidget(parent);
    w->setObjectName(QStringLiteral("noReposPage"));
    auto* addBtn  = new QPushButton(QStringLiteral("Add Existing Repository"), w);
    addBtn->setObjectName(QStringLiteral("addExistingCta"));
    auto* initBtn = new QPushButton(QStringLiteral("Initialize New Repository"), w);
    initBtn->setObjectName(QStringLiteral("initRepoCta"));
    auto* cloneBtn = new QPushButton(QStringLiteral("Clone Repository"), w);
    cloneBtn->setObjectName(QStringLiteral("cloneCta"));
    auto* layout = new QVBoxLayout(w);
    layout->addStretch();
    for (auto* b : {addBtn, initBtn, cloneBtn}) {
        auto* row = new QHBoxLayout;
        row->addStretch(); row->addWidget(b); row->addStretch();
        layout->addLayout(row);
    }
    layout->addStretch();
    return w;
}
}  // namespace

// ---- MainWindow ----
MainWindow::MainWindow(gitgui::ProjectStore* store,
                       std::filesystem::path storePath,
                       QWidget* parent)
    : QMainWindow(parent),
      store_(store),
      controller_(new ProjectController(store, std::move(storePath), this)),
      sidebar_(new ProjectSidebar(controller_, this)),
      repoController_(new RepoController(this)),
      changesView_(new ChangesView(this)),
      dashboardModel_(new DashboardModel(this)),
      centralStack_(new QStackedWidget(this)) {
    setWindowTitle(QStringLiteral("GitGUI"));

    // Left dock
    auto* dock = new QDockWidget(QStringLiteral("Projects"), this);
    dock->setObjectName(QStringLiteral("projectsDock"));
    dock->setWidget(sidebar_);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Central stack
    centralStack_->setObjectName(QStringLiteral("centralStack"));
    auto* noProjectsPage = makeNoProjectsPage(this);
    auto* noReposPage    = makeNoReposPage(this);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("mainTabs"));
    tabs->addTab(changesView_, QStringLiteral("Changes"));
    tabs->addTab(new QLabel(QStringLiteral("History — Plan 4")), QStringLiteral("History"));
    auto* dashboardView = new QListView(this);
    dashboardView->setObjectName(QStringLiteral("dashboardList"));
    dashboardView->setModel(dashboardModel_);
    tabs->addTab(dashboardView, QStringLiteral("Dashboard"));

    centralStack_->addWidget(noProjectsPage); // index 0
    centralStack_->addWidget(noReposPage);    // index 1
    centralStack_->addWidget(tabs);           // index 2
    setCentralWidget(centralStack_);

    // Wire existing repo/sidebar connections (unchanged from before)
    connect(sidebar_, &ProjectSidebar::openInNewWindowRequested,
            this, &MainWindow::openInNewWindowRequested);
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

    // Empty-state page switching
    connect(controller_, &ProjectController::projectActivated,
            this, &MainWindow::updateCentralPage);
    connect(controller_, &ProjectController::projectCreated,
            this, &MainWindow::updateCentralPage);
    connect(controller_, &ProjectController::repoAdded,
            this, &MainWindow::updateCentralPage);

    // Sidebar mutation signals → handlers
    connect(sidebar_, &ProjectSidebar::createProjectRequested,
            this, &MainWindow::onCreateProjectRequested);
    connect(sidebar_, &ProjectSidebar::addExistingRequested,
            this, &MainWindow::onAddExistingRequested);
    connect(sidebar_, &ProjectSidebar::initRepoRequested,
            this, &MainWindow::onInitRepoRequested);
    connect(sidebar_, &ProjectSidebar::cloneRepoRequested,
            this, &MainWindow::onCloneRepoRequested);

    // CTA buttons on the no-projects and no-repos pages
    connect(noProjectsPage->findChild<QPushButton*>(QStringLiteral("createProjectCta")),
            &QPushButton::clicked, this, &MainWindow::onCreateProjectRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("addExistingCta")),
            &QPushButton::clicked, this, &MainWindow::onAddExistingRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("initRepoCta")),
            &QPushButton::clicked, this, &MainWindow::onInitRepoRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("cloneCta")),
            &QPushButton::clicked, this, &MainWindow::onCloneRepoRequested);

    updateCentralPage();
}

void MainWindow::updateCentralPage() {
    if (store_->projects().empty()) {
        centralStack_->setCurrentIndex(0);
    } else if (controller_->activeRepos().empty()) {
        centralStack_->setCurrentIndex(1);
    } else {
        centralStack_->setCurrentIndex(2);
    }
}

void MainWindow::onCreateProjectRequested() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New Project"),
        QStringLiteral("Project name:"),
        QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        controller_->createProject(name.trimmed());
    }
}

void MainWindow::onAddExistingRequested() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Git Repository"));
    if (dir.isEmpty()) return;
    controller_->addExistingRepo(dir);
}

void MainWindow::onInitRepoRequested() {
    InitRepoDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (dlg.parentDir().isEmpty() || dlg.repoName().isEmpty()) return;
    controller_->initRepo(dlg.parentDir(), dlg.repoName());
}

void MainWindow::onCloneRepoRequested() {
    // Stub — implemented in Task 5.
}

QString MainWindow::currentProjectId() const {
    return controller_->activeProjectId();
}

void MainWindow::showProject(const QString& projectId) {
    controller_->activate(projectId);
}

}  // namespace gitgui::ui
