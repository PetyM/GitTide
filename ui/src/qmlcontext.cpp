#include "gittide/ui/qmlcontext.hpp"

#include <QQmlContext>

#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"

namespace gittide::ui {

void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController)
{
    ctx->setContextProperty(QStringLiteral("theme"), theme);
    ctx->setContextProperty(QStringLiteral("repoModel"), repoModel);
    ctx->setContextProperty(QStringLiteral("projectController"), projectController);
}

} // namespace gittide::ui
