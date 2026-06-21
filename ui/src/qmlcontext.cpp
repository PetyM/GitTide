#include "gittide/ui/qmlcontext.hpp"

#include <QQmlContext>
#include <QtQml>

#include "gittide/ui/graphcolumn.hpp"
#include "gittide/ui/logging.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/projectlistmodel.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/repoviewmodel.hpp"

namespace gittide::ui {

void registerQmlTypes()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    qmlRegisterType<GraphColumn>("GitTide", 1, 0, "GraphColumn");
}

void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController,
                       RepoViewModel* repoVm, QmlLog* log)
{
    registerQmlTypes();
    ctx->setContextProperty(QStringLiteral("theme"), theme);
    ctx->setContextProperty(QStringLiteral("repoModel"), repoModel);
    ctx->setContextProperty(QStringLiteral("projectController"), projectController);
    ctx->setContextProperty(QStringLiteral("projectModel"), projectController ? projectController->projects() : nullptr);
    ctx->setContextProperty(QStringLiteral("repoVm"), repoVm);
    ctx->setContextProperty(QStringLiteral("log"), log ? log : new QmlLog(ctx));
}

} // namespace gittide::ui
