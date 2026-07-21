# Design — Project Options dialog (per-project & per-repo identity)

**Status:** planned
**Date:** 2026-07-21
**Realises:** finishing identity management — a dedicated **Project Options**
surface, separate from the global **Options** dialog, where the user picks a
project-wide identity and overrides identity per repository.

## Problem

The identity data model already supports the full chain **global → project
default → repo override** (`CredentialsStore`: `m_globalIdentity`,
`m_projectDefaults`, `m_repoOverrides`; resolution in `resolveLocalIdentity` /
`resolveIdentity`). `CredentialManager` already exposes the setters/getters
(`setProjectDefault`, `setRepoOverride`, `projectDefaultId`, `repoOverrideId`)
and materializes the resolved identity into git config.

But the **only UI** for project/repo assignment lives in **Options → Identity**,
as per-identity-row *Project* / *Repo* buttons that target solely the **active
project** and the **currently open repo**. There is no way to:

- see all repositories of a project and set each one's identity, or
- assign identity to a repo that is not currently open.

Goal: a **Project Options** dialog, scoped to the active project, that owns
project + per-repo identity; and move the assignment concern out of the global
Options dialog (which then carries only global-scoped settings).

## What already exists (reused, not rebuilt)

- **Core resolution + persistence.** `CredentialsStore` per-project/per-repo
  assignments and `resolveLocalIdentity` (repo override → first project default
  → nothing; global deliberately not pushed into local config).
- **Git-config write path.** `CredentialManager::applyIdentityToRepo` +
  `GitRepo::setLocalIdentity` / `clearLocalIdentity` (see
  [Git-config integration](#git-config-integration)).
- **Manager API.** `setProjectDefault`, `setRepoOverride`, `projectDefaultId`,
  `repoOverrideId`, `globalIdentityId`, `identities()` (→ `identityModel`).
- **Project context.** `ProjectController.activeProjectId` /
  `activeProjectName`; `ProjectListModel` already exposes the project name via
  `Qt::DisplayRole` (`model.display`). The Sidebar project-switcher menu
  (`projectMenu`) already hosts *New project…* / *Delete current project…*.
- **`AppDialog`** modal base, `AppComboBox`, `AppButton`, `AppScrollBar` —
  unchanged.

## Design

### 1. Entry point — Sidebar project-switcher menu

Add a **"Project options…"** `AppMenuItem` to `projectMenu` (Sidebar.qml),
between the project list and the New/Delete items, `enabled` only when
`projectController.activeProjectId.length > 0`. It emits a new
`projectOptionsRequested()` signal on `Sidebar`; `Main.qml` opens the dialog on
that signal (mirroring the existing `newProjectRequested` / `deleteProjectRequested`
wiring). No app-menu-bar entry — the action is contextual to the active project.

### 2. `ProjectOptionsDialog.qml` (new) — scoped to the active project

`AppDialog` with the header title bound to `projectController.activeProjectName`
(objectName `projectOptionsDialog`). Two sections in a scrollable column:

**a. Project identity** — a single `AppComboBox` (objectName
`projectIdentityCombo`). Model rows:

- row 0: **`(Inherit — <global name>)`** — the resolved global fallback;
- one row per catalogue identity (`identityModel`).

Current selection reflects `credentialManager.projectDefaultId(activeProjectId)`
(empty ⇒ row 0). Selecting a real identity → `setProjectDefault(pid, id)`; row 0
→ `setProjectDefault(pid, "")`.

**b. Repositories** — a `ListView` (objectName `projectRepoList`) over the
active project's **top-level** repositories. Each row: the repo name plus an
`AppComboBox`. Model rows:

- row 0: **`(Inherit — <resolved>)`** where *resolved* = the repo's inherited
  identity (project default else global);
- one row per catalogue identity.

Current selection reflects `credentialManager.repoOverrideId(path)` (empty ⇒ row
0). Selecting an identity → `setRepoOverride(path, id)`; row 0 →
`setRepoOverride(path, "")`.

Submodules are **out of scope** — top-level repos only (though the store keys
overrides by any path, so a later extension is additive).

The dialog is modal and rebuilds its repo list on open; repos do not change
while it is open, so a static snapshot (no live model reactivity) is enough.

### 3. Backend additions (additive)

Two helpers on `CredentialManager`, both driving the `(Inherit — …)` labels:

- `Q_INVOKABLE QString identityLabel(const QString& id) const` — the display
  name of the identity with this id; empty string for an empty/unknown id. Lets
  QML render an id (from `inheritedIdentityId`) as text without a manual
  model scan.
- `Q_INVOKABLE QString inheritedIdentityId(const QString& projectId) const` —
  the id a repo inherits when it has no override: the project default if
  `projectId` is non-empty and set, else the global identity id. Passing an
  empty `projectId` returns the global id (so the project picker's inherit row
  can reuse it).

Active-project repo enumeration for section (b): add
`Q_INVOKABLE QVariantList activeProjectRepos() const` on `ProjectController`,
returning one `{ path, name }` map per top-level repo of the active project
(`name` = `RepoRef.alias` if set, else the path's basename). Reuses
`activeRepos()`; no new model type.

### 4. Options → Identity tab — drop assignment

Remove from `OptionsIdentityTab.qml` the per-row **Project** and **Repo**
`AppButton`s and the machinery that fed them: the `repoOverrideId` /
`projectDefaultId` properties, `refreshAssignments()`, the
`hasRepo` / `activeProjectId` helpers, and the `credentialManager` / `repoVm`
`Connections`. The tab keeps: the identities list, the add row, the per-row
**Global** button (→ `setGlobalIdentity`), and delete. The row highlight/badges
that depended on project/repo assignment simplify to just the **Global** badge.

Global identity management is unchanged and remains **only** here (pick a
catalogue identity as Global → writes `~/.gitconfig`). The global Options dialog
carries global-scoped settings only; project/repo assignment lives in Project
Options.

## Git-config integration

Project Options changes **no** write-path code — it only exposes the existing
setters more fully. The write model (unchanged) is:

- On every `setProjectDefault` / `setRepoOverride` (and on active-repo change),
  `applyIdentityToRepo` resolves the repo's LOCAL identity (repo override →
  project default → none) and writes `user.name` / `user.email` into the repo's
  **local** git config, plus a marker key **`gittide.identity = <identity id>`**.
- **Marker guard:** a repo whose local identity was set by hand (via the CLI —
  present but unmarked) is never overwritten. GitTide only touches identity it
  owns.
- Removing an override/default that GitTide had materialized →
  `clearLocalIdentity` wipes `user.name` / `user.email` / marker, so the repo
  falls back to the **global** git identity.
- The **global** identity is written to `~/.gitconfig` by `setGlobalIdentity`;
  it is never pushed down into a repo's local config.

**Immediacy.** A per-repo override set from the dialog rewrites *that* repo's
config immediately, even if the repo is not currently open (`applyIdentityToRepo`
opens it via a transient `AsyncRepo`). Changing the **project default** re-applies
only the currently-open repo; the project's other repos pick up the new default
**lazily on next open** (resolution recomputed each open, marker-guarded). This
is a lazy-materialization delay, not a correctness gap — a GitTide-managed repo
self-corrects when reopened, and git falls back to global config meanwhile.

## Testing (TDD — failing test first)

- **Manager (`test_credential_manager.cpp`)** — `identityLabel`: known id → its
  name; empty/unknown → empty. `inheritedIdentityId`: repo has project default →
  project default; no project default → global; empty `projectId` → global.
- **Controller (`test_project_controller.cpp`)** — `activeProjectRepos` returns
  one `{path,name}` per top-level repo of the active project (alias honoured,
  basename fallback); empty when no active project.
- **UI shell (`test_qml_shell.cpp`)** — `projectOptionsRequested` opens
  `projectOptionsDialog`; `projectIdentityCombo` and `projectRepoList` present;
  selecting a repo-row identity calls `setRepoOverride` (assert via the store /
  `repoOverrideId`). Options → Identity tab no longer exposes the Project/Repo
  buttons (assert their objectNames are absent) while `identityList` /
  `identityAdd` / the Global button remain.

## Non-goals

- Submodule-level identity assignment.
- Editing more than one project from the dialog (scoped to the active project).
- Per-project settings beyond identity.
- Any change to the global identity mechanism or the git-config write path.
- Direct name/email editing of `~/.gitconfig` (global identity stays
  pick-from-catalogue in the Identity tab).
