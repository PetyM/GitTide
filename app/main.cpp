#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "gittide/libgit2context.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/ui/thememanager.hpp"
#include "gittide/ui/windowmanager.hpp"

using gittide::ui::WindowManager;

int main(int argc, char** argv)
{
    Q_INIT_RESOURCE(icons);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("gittide"));
    QApplication::setOrganizationName(QStringLiteral("gittide"));

    // Initialise libgit2 once for the whole process lifetime. GitRepo-backed
    // code paths are wired up in the UI layer, but a correct bootstrap owns
    // the global init here.
    const gittide::LibGit2Context git_ctx;

    gittide::ui::ThemeManager theme;
    theme.setMode(gittide::ui::ThemeManager::Mode::System);
    theme.applyTo(&app);

    WindowManager manager;

    // Load the project registry (projects.json) into the shared store.
    const QString configDir    = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString projectsFile = QDir(configDir).filePath(QStringLiteral("projects.json"));
    if (auto loaded = gittide::ProjectStore::load(std::filesystem::path(projectsFile.toStdString())))
    {
        *manager.store() = std::move(*loaded);
    }

    // Restore prior windows; if none, open the last-focused project, else the
    // first project, else an empty shell window. Always end with >=1 window so
    // a fresh launch (no projects.json yet) shows the UI instead of nothing.
    manager.restoreSession();
    if (manager.windowCount() == 0)
    {
        const QString active = QString::fromStdString(manager.store()->activeProject());
        if (!active.isEmpty())
        {
            manager.openProject(active);
        }
        else if (!manager.store()->projects().empty())
        {
            manager.openProject(QString::fromStdString(manager.store()->projects().front().id));
        }
        else
        {
            // No project yet: open an empty shell so the window is visible.
            // (activate("") is a no-op; the sidebar/combo simply start empty.)
            manager.openProject(QString());
        }
    }

    // Persist window session on quit.
    QObject::connect(&app,
                     &QApplication::aboutToQuit,
                     &manager,
                     [&manager]()
                     {
                         manager.saveSession();
                     });

    return app.exec();
}
