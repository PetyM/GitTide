#pragma once

class QQmlContext;

namespace gittide::ui {

class QmlTheme;
class RepoListModel;
class ProjectController;
class RepoViewModel;

// Single source of the QML context wiring used by both the app entry point and
// the shell test. Sets the context properties Main.qml binds to: `theme`,
// `repoModel`, `projectController`, `repoVm`. projectController/repoVm may be null
// in tests that don't exercise them.
void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController, RepoViewModel* repoVm);

} // namespace gittide::ui
