#pragma once

#include <QString>

class QQmlContext;

namespace gittide::ui {

class QmlTheme;
class RepoListModel;
class ProjectController;
class RepoViewModel;
class QmlLog;
class CredentialManager;
class AvatarService;

// Single source of the QML context wiring used by both the app entry point and
// the shell test. Sets the context properties Main.qml binds to: `theme`,
// `repoModel`, `projectController`, `repoVm`, `log`, `appVersion`, and
// `credentialManager` / `identityModel`.
// projectController/repoVm/credentials may be null in tests that don't exercise
// them. When `log` is null a default QmlLog parented to `ctx` is created, so the
// `log` property is always available to QML. `appVersion` defaults to an empty
// string so existing callers (tests) compile unchanged.
void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController,
                       RepoViewModel* repoVm, QmlLog* log = nullptr, const QString& appVersion = {},
                       CredentialManager* credentials = nullptr, AvatarService* avatars = nullptr);

/// Register C++ types exposed to QML (currently GraphColumn in module
/// "GitTide" 1.0). Idempotent — safe to call once per process or per engine.
void registerQmlTypes();

} // namespace gittide::ui
