#include "gittide/ui/qmlcontext.hpp"

#include <QQmlContext>

#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/repoviewmodel.hpp"

namespace gittide::ui {

void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController, RepoViewModel* repoVm)
{
    ctx->setContextProperty(QStringLiteral("theme"), theme);
    ctx->setContextProperty(QStringLiteral("repoModel"), repoModel);
    ctx->setContextProperty(QStringLiteral("projectController"), projectController);
    ctx->setContextProperty(QStringLiteral("repoVm"), repoVm);
}

} // namespace gittide::ui
