#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QStandardPaths>

#include "gittide/libgit2context.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

int main(int argc, char** argv)
{
    Q_INIT_RESOURCE(icons);
    Q_INIT_RESOURCE(qml);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("gittide"));
    QGuiApplication::setOrganizationName(QStringLiteral("gittide"));

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

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &qmlTheme, controller.repos(), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
