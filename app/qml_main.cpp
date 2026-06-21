#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QStandardPaths>

#include "gittide/libgit2context.hpp"
#include "gittide/log.hpp"
#include "gittide/projectstore.hpp"
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

    // Wire diagnostics first: bridge core's Qt-free log facade onto Qt's category
    // machinery and start logging to the console + a rotating file. Verbosity is
    // controlled via QT_LOGGING_RULES (e.g. "gittide.git.debug=true").
    const QString logDir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("logs"));
    gittide::ui::installLogging(logDir);
    gittide::logf(gittide::LogLevel::Info, gittide::logcat::APP, "GitTide starting; logs at {}", logDir.toStdString());

    const gittide::LibGit2Context git_ctx;

    ThemeManager theme;
    theme.setMode(ThemeManager::Mode::System);

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
    installQmlContext(engine.rootContext(), &qmlTheme, controller.repos(), &controller, &repoVm);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
