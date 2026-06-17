#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"

#include <QDockWidget>
#include <QLabel>
#include <QTabWidget>

namespace gitgui::ui {

MainWindow::MainWindow(gitgui::ProjectStore* store, QWidget* parent)
    : QMainWindow(parent),
      controller_(new ProjectController(store, this)),
      sidebar_(new ProjectSidebar(controller_, this)) {
    setWindowTitle(QStringLiteral("GitGUI"));

    auto* dock = new QDockWidget(QStringLiteral("Projects"), this);
    dock->setObjectName(QStringLiteral("projectsDock"));
    dock->setWidget(sidebar_);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("mainTabs"));
    // Placeholder tabs; content lands in Plans 3 (Changes) and 4 (History).
    tabs->addTab(new QLabel(QStringLiteral("Changes — Plan 3")), QStringLiteral("Changes"));
    tabs->addTab(new QLabel(QStringLiteral("History — Plan 4")), QStringLiteral("History"));
    tabs->addTab(new QLabel(QStringLiteral("Dashboard")), QStringLiteral("Dashboard"));
    setCentralWidget(tabs);

    connect(sidebar_, &ProjectSidebar::openInNewWindowRequested,
            this, &MainWindow::openInNewWindowRequested);
}

QString MainWindow::currentProjectId() const {
    return controller_->activeProjectId();
}

void MainWindow::showProject(const QString& projectId) {
    controller_->activate(projectId);
}

}  // namespace gitgui::ui
