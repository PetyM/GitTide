#include "gittide/ui/qmlcontext.hpp"

#include <QQmlContext>
#include <QQmlEngine>
#include <QtQml>

#include "gittide/ui/avatarimageprovider.hpp"
#include "gittide/ui/avatarservice.hpp"
#include "gittide/ui/credentialmanager.hpp"
#include "gittide/ui/graphcolumn.hpp"
#include "gittide/ui/hostlistmodel.hpp"
#include "gittide/ui/sshkeylistmodel.hpp"
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
                       RepoViewModel* repoVm, QmlLog* log, const QString& appVersion, CredentialManager* credentials,
                       AvatarService* avatars)
{
    registerQmlTypes();

    // The avatar service is always available to QML (a default instance when the
    // caller passes none, mirroring `log`), so Avatar.qml's `avatarService.*`
    // bindings resolve in every test that loads Main.qml. Its image provider is
    // registered on the owning engine so `image://avatar/<hash>` resolves.
    AvatarService* avatarSvc = avatars ? avatars : new AvatarService(ctx);
    ctx->setContextProperty(QStringLiteral("avatarService"), avatarSvc);
    if (QQmlEngine* engine = ctx->engine(); engine && !engine->imageProvider(QStringLiteral("avatar")))
        engine->addImageProvider(QStringLiteral("avatar"), new AvatarImageProvider(avatarSvc));

    ctx->setContextProperty(QStringLiteral("theme"), theme);
    ctx->setContextProperty(QStringLiteral("repoModel"), repoModel);
    ctx->setContextProperty(QStringLiteral("projectController"), projectController);
    ctx->setContextProperty(QStringLiteral("projectModel"), projectController ? projectController->projects() : nullptr);
    ctx->setContextProperty(QStringLiteral("repoVm"), repoVm);
    ctx->setContextProperty(QStringLiteral("log"), log ? log : new QmlLog(ctx));
    ctx->setContextProperty(QStringLiteral("appVersion"), appVersion);
    ctx->setContextProperty(QStringLiteral("credentialManager"), credentials);
    ctx->setContextProperty(QStringLiteral("identityModel"), credentials ? credentials->identities() : nullptr);
    ctx->setContextProperty(QStringLiteral("hostModel"), credentials ? credentials->hosts() : nullptr);
    ctx->setContextProperty(QStringLiteral("sshKeyModel"), credentials ? credentials->sshKeys() : nullptr);
}

} // namespace gittide::ui
