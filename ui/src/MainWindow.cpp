#include "gittide/ui/MainWindow.hpp"
#include "gittide/ui/ProjectController.hpp"
#include "gittide/ui/ProjectSidebar.hpp"
#include "gittide/ui/RepoController.hpp"
#include "gittide/ui/ChangesView.hpp"
#include "gittide/ui/HistoryView.hpp"
#include "gittide/ui/DashboardModel.hpp"
#include "gittide/ui/AddRepoDialogs.hpp"

#include <filesystem>
#include <vector>

#include <qcorotask.h>

#include <QDockWidget>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace gittide::ui {

// ---- helpers ----
namespace {

// Builds a centered branded card: icon + headline + subtext + the given buttons.
QWidget* makeEmptyStatePage(QWidget* parent, const QString& pageName,
                            const QString& headline, const QString& subtext,
                            const QList<QPushButton*>& buttons) {
    auto* w = new QWidget(parent);
    w->setObjectName(pageName);

    // Both empty-state pages share this name intentionally: QSS targets it as a
    // visual "class" (QFrame#emptyStateCard). Only one page is visible at a time
    // via QStackedWidget. Callers that need to distinguish them should search
    // within the specific page widget (findChild on the page, not the window).
    auto* card = new QFrame(w);
    card->setObjectName(QStringLiteral("emptyStateCard"));
    card->setMaximumWidth(420);

    auto* icon = new QLabel(card);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/gittide-icon.svg")).pixmap(72, 72));
    icon->setAlignment(Qt::AlignCenter);

    auto* title = new QLabel(headline, card);
    title->setProperty("role", "headline");
    title->setAlignment(Qt::AlignCenter);

    auto* sub = new QLabel(subtext, card);
    sub->setProperty("role", "subtext");
    sub->setAlignment(Qt::AlignCenter);
    sub->setWordWrap(true);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(12);
    cardLayout->addWidget(icon);
    cardLayout->addWidget(title);
    cardLayout->addWidget(sub);
    for (auto* b : buttons) { b->setParent(card); cardLayout->addWidget(b); }

    auto* outer = new QVBoxLayout(w);
    outer->addStretch();
    auto* row = new QHBoxLayout;
    row->addStretch(); row->addWidget(card); row->addStretch();
    outer->addLayout(row);
    outer->addStretch();
    return w;
}

QWidget* makeNoProjectsPage(QWidget* parent) {
    auto* btn = new QPushButton(QStringLiteral("Create Project"));
    btn->setObjectName(QStringLiteral("createProjectCta"));
    return makeEmptyStatePage(
        parent, QStringLiteral("noProjectsPage"),
        QStringLiteral("Welcome to GitTide"),
        QStringLiteral("Create a project to group the repositories you work on."),
        {btn});
}

QWidget* makeNoReposPage(QWidget* parent) {
    auto* addBtn = new QPushButton(QStringLiteral("Add Existing Repository"));
    addBtn->setObjectName(QStringLiteral("addExistingCta"));
    auto* initBtn = new QPushButton(QStringLiteral("Initialize New Repository"));
    initBtn->setObjectName(QStringLiteral("initRepoCta"));
    auto* cloneBtn = new QPushButton(QStringLiteral("Clone Repository"));
    cloneBtn->setObjectName(QStringLiteral("cloneCta"));
    return makeEmptyStatePage(
        parent, QStringLiteral("noReposPage"),
        QStringLiteral("No repositories yet"),
        QStringLiteral("Add, initialize, or clone a repository to get started."),
        {addBtn, initBtn, cloneBtn});
}

}  // namespace

// ---- MainWindow ----
MainWindow::MainWindow(gittide::ProjectStore* store,
                       std::filesystem::path storePath,
                       QWidget* parent)
    : QMainWindow(parent),
      store_(store),
      controller_(new ProjectController(store, std::move(storePath), this)),
      sidebar_(new ProjectSidebar(controller_, this)),
      repoController_(new RepoController(this)),
      changesView_(new ChangesView(this)),
      historyView_(new HistoryView(this)),
      dashboardModel_(new DashboardModel(this)),
      centralStack_(new QStackedWidget(this)) {
    setWindowTitle(QStringLiteral("GitTide"));

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
    tabs->addTab(historyView_, QStringLiteral("History"));
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
        QCoro::connect(repoController_->refreshHistory(), this, [] {});
    });
    connect(repoController_, &RepoController::historyReady, this,
            [this](const gittide::GraphLayout& layout) {
                historyView_->setHistory(layout);
            });

    // Async wiring between controller and ChangesView.
    connect(repoController_, &RepoController::statusChanged,
            changesView_, &ChangesView::setStatus);
    connect(repoController_, &RepoController::diffReady, this,
            [this](const QString& path, const gittide::DiffResult& result) {
                changesView_->setDiff(result, std::filesystem::path(path.toStdString()));
            });
    connect(changesView_, &ChangesView::fileSelected, this,
            [this](const QString& path, gittide::DiffTarget target) {
                QCoro::connect(repoController_->refreshDiff(path, target), this, [] {});
            });
    connect(changesView_, &ChangesView::stageRequested, this,
            [this](const gittide::StageSelection& sel) {
                QCoro::connect(repoController_->stage(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::unstageRequested, this,
            [this](const gittide::StageSelection& sel) {
                QCoro::connect(repoController_->unstage(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::discardRequested, this,
            [this](const gittide::StageSelection& sel) {
                QCoro::connect(repoController_->discard(sel), this, [] {});
            });
    connect(changesView_, &ChangesView::commitRequested, this,
            [this](const gittide::CommitRequest& req) {
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
    connect(controller_, &ProjectController::repoRemoved,
            this, &MainWindow::updateCentralPage);
    connect(controller_, &ProjectController::projectRemoved,
            this, &MainWindow::updateCentralPage);
    connect(controller_, &ProjectController::repoAddFailed, this,
            [this](const QString& message) {
                QMessageBox::warning(this, QStringLiteral("Repository Error"), message);
            });

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
    CloneRepoDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString url  = dlg.url().trimmed();
    const QString dest = dlg.dest().trimmed();
    if (url.isEmpty() || dest.isEmpty()) return;

    auto* progress = new QProgressDialog(
        QStringLiteral("Cloning…"), QStringLiteral("Cancel"), 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->show();

    connect(controller_, &ProjectController::cloneProgress, progress,
            [progress](int r, int t) {
                if (t > 0) progress->setValue(r * 100 / t);
            });
    connect(progress, &QProgressDialog::canceled, controller_,
            &ProjectController::cancelClone);

    QCoro::connect(controller_->cloneRepo(url, dest), this,
                   [progress] {
                       progress->close();
                       progress->deleteLater();
                   });
}

QString MainWindow::currentProjectId() const {
    return controller_->activeProjectId();
}

void MainWindow::showProject(const QString& projectId) {
    controller_->activate(projectId);
}

}  // namespace gittide::ui
