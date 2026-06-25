#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include "gittide/libgit2context.hpp"
#include "gittide/log.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/version.hpp"
#include "gittide/ui/logging.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repoviewmodel.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

int main(int argc, char** argv)
{
    Q_INIT_RESOURCE(icons);
    Q_INIT_RESOURCE(qml);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("gittide"));
    QGuiApplication::setOrganizationName(QStringLiteral("gittide"));

    // Dock / taskbar icon. Without this the window manager falls back to a
    // generic icon; setDesktopFileName ties the running window to the installed
    // .desktop entry so the WM groups them under the same icon.
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/gittide-icon.svg")));
    QGuiApplication::setDesktopFileName(QStringLiteral("gittide"));

    // Wire diagnostics first: bridge core's Qt-free log facade onto Qt's category
    // machinery and start logging to the console + a rotating file. Verbosity is
    // controlled via QT_LOGGING_RULES (e.g. "gittide.git.debug=true").
    const QString logDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("logs"));
    gittide::ui::installLogging(logDir);
    gittide::logf(gittide::LogLevel::Info, gittide::logcat::APP, "GitTide starting; logs at {}", logDir.toStdString());

    const gittide::LibGit2Context git_ctx;

    ThemeManager theme;
    {
        QSettings s;
        const int storedMode = s.value(QStringLiteral("themeMode"), 0).toInt();
        theme.setMode(static_cast<ThemeManager::Mode>(storedMode));
    }

    // Load the project registry into a local store (single-window shell — multi
    // window/session restore is a later plan).
    gittide::ProjectStore store;
    const QString configDir    = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString projectsFile = QDir(configDir).filePath(QStringLiteral("projects.json"));
    if (auto loaded = gittide::ProjectStore::load(std::filesystem::path(projectsFile.toStdString())))
    {
        store = std::move(*loaded);
    }

    ProjectController controller(&store, std::filesystem::path(projectsFile.toStdString()));
    const QString active = QString::fromStdString(store.activeProject());
    if (!active.isEmpty())
        controller.activate(active);
    else if (!store.projects().empty())
        controller.activate(QString::fromStdString(store.projects().front().id));

    QmlTheme qmlTheme(&theme);

    RepoViewModel repoVm;
    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &qmlTheme, controller.repos(), &controller, &repoVm,
                      nullptr,
                      QString::fromStdString(std::string(gittide::kVersion)));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
