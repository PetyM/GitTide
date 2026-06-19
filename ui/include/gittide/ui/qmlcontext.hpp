#pragma once

class QQmlContext;

namespace gittide::ui {

class QmlTheme;
class RepoListModel;
class ProjectController;

// Single source of the QML context wiring used by both the app entry point and
// the shell test. Sets the context properties Main.qml binds to: `theme`,
// `repoModel`, `projectController`. projectController may be null in tests.
void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController);

} // namespace gittide::ui
