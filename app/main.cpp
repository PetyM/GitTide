#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "gitgui/LibGit2Context.hpp"
#include "gitgui/ProjectStore.hpp"
#include "gitgui/ui/WindowManager.hpp"

using gitgui::ui::WindowManager;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("gitgui"));
    QApplication::setOrganizationName(QStringLiteral("gitgui"));

    // Initialise libgit2 once for the whole process lifetime. GitRepo-backed
    // code paths (DashboardModel, RepoController) are wired up in Plan 3, but
    // a correct bootstrap owns the global init here.
    const gitgui::LibGit2Context git_ctx;

    WindowManager manager;

    // Load the project registry (projects.json) into the shared store.
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString projectsFile = QDir(configDir).filePath(QStringLiteral("projects.json"));
    if (auto loaded = gitgui::ProjectStore::load(std::filesystem::path(projectsFile.toStdString()))) {
        *manager.store() = std::move(*loaded);
    }

    // Restore prior windows; if none, open the last-focused project (if any).
    manager.restoreSession();
    if (manager.windowCount() == 0) {
        const QString active = QString::fromStdString(manager.store()->activeProject());
        if (!active.isEmpty()) {
            manager.openProject(active);
        } else if (!manager.store()->projects().empty()) {
            manager.openProject(QString::fromStdString(manager.store()->projects().front().id));
        }
    }

    // Persist window session on quit.
    QObject::connect(&app, &QApplication::aboutToQuit, &manager,
                     [&manager]() { manager.saveSession(); });

    return app.exec();
}
