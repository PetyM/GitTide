#include "gittide/ui/mainwindow.hpp"

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
#include <filesystem>
#include <qcorotask.h>
#include <vector>

#include "gittide/ui/addrepodialogs.hpp"
#include "gittide/ui/changesview.hpp"
#include "gittide/ui/dashboardmodel.hpp"
#include "gittide/ui/historyview.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/projectsidebar.hpp"
#include "gittide/ui/repocontroller.hpp"

namespace gittide::ui {

// ---- helpers ----
namespace {

// Builds a centered branded card: icon + headline + subtext + the given buttons.
QWidget* makeEmptyStatePage(QWidget* parent, const QString& pageName, const QString& headline, const QString& subtext,
                            const QList<QPushButton*>& buttons)
{
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
    for (auto* b : buttons)
    {
        b->setParent(card);
        cardLayout->addWidget(b);
    }

    auto* outer = new QVBoxLayout(w);
    outer->addStretch();
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(card);
    row->addStretch();
    outer->addLayout(row);
    outer->addStretch();
    return w;
}

QWidget* makeNoProjectsPage(QWidget* parent)
{
    auto* btn = new QPushButton(QStringLiteral("Create Project"));
    btn->setObjectName(QStringLiteral("createProjectCta"));
    return makeEmptyStatePage(parent,
                              QStringLiteral("noProjectsPage"),
                              QStringLiteral("Welcome to GitTide"),
                              QStringLiteral("Create a project to group the repositories you work on."),
                              {btn});
}

QWidget* makeNoReposPage(QWidget* parent)
{
    auto* addBtn = new QPushButton(QStringLiteral("Add Existing Repository"));
    addBtn->setObjectName(QStringLiteral("addExistingCta"));
    auto* initBtn = new QPushButton(QStringLiteral("Initialize New Repository"));
    initBtn->setObjectName(QStringLiteral("initRepoCta"));
    auto* cloneBtn = new QPushButton(QStringLiteral("Clone Repository"));
    cloneBtn->setObjectName(QStringLiteral("cloneCta"));
    return makeEmptyStatePage(parent,
                              QStringLiteral("noReposPage"),
                              QStringLiteral("No repositories yet"),
                              QStringLiteral("Add, initialize, or clone a repository to get started."),
                              {addBtn, initBtn, cloneBtn});
}

} // namespace

// ---- MainWindow ----
MainWindow::MainWindow(gittide::ProjectStore* store, std::filesystem::path storePath, QWidget* parent)
    : QMainWindow(parent)
    , m_store(store)
    , m_controller(new ProjectController(store, std::move(storePath), this))
    , m_sidebar(new ProjectSidebar(m_controller, this))
    , m_repoController(new RepoController(this))
    , m_changesView(new ChangesView(this))
    , m_historyView(new HistoryView(this))
    , m_dashboardModel(new DashboardModel(this))
    , m_centralStack(new QStackedWidget(this))
{
    setWindowTitle(QStringLiteral("GitTide"));

    // Left dock
    auto* dock = new QDockWidget(QStringLiteral("Projects"), this);
    dock->setObjectName(QStringLiteral("projectsDock"));
    dock->setWidget(m_sidebar);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // Central stack
    m_centralStack->setObjectName(QStringLiteral("centralStack"));
    auto* noProjectsPage = makeNoProjectsPage(this);
    auto* noReposPage    = makeNoReposPage(this);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("mainTabs"));
    tabs->addTab(m_changesView, QStringLiteral("Changes"));
    tabs->addTab(m_historyView, QStringLiteral("History"));
    auto* dashboardView = new QListView(this);
    dashboardView->setObjectName(QStringLiteral("dashboardList"));
    dashboardView->setModel(m_dashboardModel);
    tabs->addTab(dashboardView, QStringLiteral("Dashboard"));

    m_centralStack->addWidget(noProjectsPage); // index 0
    m_centralStack->addWidget(noReposPage);    // index 1
    m_centralStack->addWidget(tabs);           // index 2
    setCentralWidget(m_centralStack);

    // Wire existing repo/sidebar connections (unchanged from before)
    connect(m_sidebar, &ProjectSidebar::openInNewWindowRequested, this, &MainWindow::openInNewWindowRequested);
    connect(m_sidebar,
            &ProjectSidebar::repoSelected,
            this,
            [this](const QString& path)
            {
                m_repoController->open(path);
            });
    // A coroutine slot returns a QCoro::Task that QCoro destroys as soon as the
    // handle dies — a discarded fire-and-forget task awaiting a QFuture is a
    // use-after-free when that future completes. QCoro::connect anchors the task
    // (tied to `this`) until it finishes, which is how these are launched.
    connect(m_repoController,
            &RepoController::repoOpened,
            this,
            [this](const QString& path)
            {
                emit repoOpened(path);
                QCoro::connect(m_repoController->refreshStatus(), this, [] {});
                QCoro::connect(m_repoController->refreshHistory(), this, [] {});
            });
    connect(m_repoController,
            &RepoController::historyReady,
            this,
            [this](const gittide::GraphLayout& layout)
            {
                m_historyView->setHistory(layout);
            });

    // Async wiring between controller and ChangesView.
    connect(m_repoController, &RepoController::statusChanged, m_changesView, &ChangesView::setStatus);
    connect(m_repoController,
            &RepoController::diffReady,
            this,
            [this](const QString& path, const gittide::DiffResult& result)
            {
                m_changesView->setDiff(result, std::filesystem::path(path.toStdString()));
            });
    connect(m_changesView,
            &ChangesView::fileSelected,
            this,
            [this](const QString& path, gittide::DiffTarget target)
            {
                QCoro::connect(m_repoController->refreshDiff(path, target), this, [] {});
            });
    connect(m_changesView,
            &ChangesView::stageRequested,
            this,
            [this](const gittide::StageSelection& sel)
            {
                QCoro::connect(m_repoController->stage(sel), this, [] {});
            });
    connect(m_changesView,
            &ChangesView::unstageRequested,
            this,
            [this](const gittide::StageSelection& sel)
            {
                QCoro::connect(m_repoController->unstage(sel), this, [] {});
            });
    connect(m_changesView,
            &ChangesView::discardRequested,
            this,
            [this](const gittide::StageSelection& sel)
            {
                QCoro::connect(m_repoController->discard(sel), this, [] {});
            });
    connect(m_changesView,
            &ChangesView::commitRequested,
            this,
            [this](const gittide::CommitRequest& req)
            {
                QCoro::connect(m_repoController->commit(req), this, [] {});
            });

    // Activating a project refreshes the dashboard from its repos.
    connect(m_controller,
            &ProjectController::projectActivated,
            this,
            [this](const QString&)
            {
                QCoro::connect(m_dashboardModel->refreshAsync(m_controller->activeRepos()), this, [] {});
            });

    // Empty-state page switching
    connect(m_controller, &ProjectController::projectActivated, this, &MainWindow::updateCentralPage);
    connect(m_controller, &ProjectController::projectCreated, this, &MainWindow::updateCentralPage);
    connect(m_controller, &ProjectController::repoAdded, this, &MainWindow::updateCentralPage);
    connect(m_controller, &ProjectController::repoRemoved, this, &MainWindow::updateCentralPage);
    connect(m_controller, &ProjectController::projectRemoved, this, &MainWindow::updateCentralPage);
    connect(m_controller,
            &ProjectController::repoAddFailed,
            this,
            [this](const QString& message)
            {
                QMessageBox::warning(this, QStringLiteral("Repository Error"), message);
            });

    // Sidebar mutation signals → handlers
    connect(m_sidebar, &ProjectSidebar::createProjectRequested, this, &MainWindow::onCreateProjectRequested);
    connect(m_sidebar, &ProjectSidebar::addExistingRequested, this, &MainWindow::onAddExistingRequested);
    connect(m_sidebar, &ProjectSidebar::initRepoRequested, this, &MainWindow::onInitRepoRequested);
    connect(m_sidebar, &ProjectSidebar::cloneRepoRequested, this, &MainWindow::onCloneRepoRequested);

    // CTA buttons on the no-projects and no-repos pages
    connect(noProjectsPage->findChild<QPushButton*>(QStringLiteral("createProjectCta")),
            &QPushButton::clicked,
            this,
            &MainWindow::onCreateProjectRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("addExistingCta")),
            &QPushButton::clicked,
            this,
            &MainWindow::onAddExistingRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("initRepoCta")),
            &QPushButton::clicked,
            this,
            &MainWindow::onInitRepoRequested);
    connect(noReposPage->findChild<QPushButton*>(QStringLiteral("cloneCta")),
            &QPushButton::clicked,
            this,
            &MainWindow::onCloneRepoRequested);

    updateCentralPage();
}

void MainWindow::updateCentralPage()
{
    if (m_store->projects().empty())
    {
        m_centralStack->setCurrentIndex(0);
    }
    else if (m_controller->activeRepos().empty())
    {
        m_centralStack->setCurrentIndex(1);
    }
    else
    {
        m_centralStack->setCurrentIndex(2);
    }
}

void MainWindow::onCreateProjectRequested()
{
    bool ok            = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New Project"), QStringLiteral("Project name:"), QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty())
    {
        m_controller->createProject(name.trimmed());
    }
}

void MainWindow::onAddExistingRequested()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Git Repository"));
    if (dir.isEmpty())
        return;
    m_controller->addExistingRepo(dir);
}

void MainWindow::onInitRepoRequested()
{
    InitRepoDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.parentDir().isEmpty() || dlg.repoName().isEmpty())
        return;
    m_controller->initRepo(dlg.parentDir(), dlg.repoName());
}

void MainWindow::onCloneRepoRequested()
{
    CloneRepoDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString url  = dlg.url().trimmed();
    const QString dest = dlg.dest().trimmed();
    if (url.isEmpty() || dest.isEmpty())
        return;

    auto* progress = new QProgressDialog(QStringLiteral("Cloning…"), QStringLiteral("Cancel"), 0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->show();

    connect(m_controller,
            &ProjectController::cloneProgress,
            progress,
            [progress](int r, int t)
            {
                if (t > 0)
                    progress->setValue(r * 100 / t);
            });
    connect(progress, &QProgressDialog::canceled, m_controller, &ProjectController::cancelClone);

    QCoro::connect(m_controller->cloneRepo(url, dest),
                   this,
                   [progress]
                   {
                       progress->close();
                       progress->deleteLater();
                   });
}

QString MainWindow::currentProjectId() const
{
    return m_controller->activeProjectId();
}

void MainWindow::showProject(const QString& projectId)
{
    m_controller->activate(projectId);
}

} // namespace gittide::ui
