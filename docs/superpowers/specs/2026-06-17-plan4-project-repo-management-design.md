# Plan 4 — Project / Repo Management + Onboarding UX

Date: 2026-06-17
Status: Design approved, ready for implementation plan.

## Purpose

Give the user the UI to **create projects** and **add repositories**, and guide
a first-time / empty user toward those actions with centered empty-state
call-to-action buttons. Until now the UI shell (Plan 2) and the async Changes /
Dashboard layers (Plan 3a/3b) assumed projects and repos already existed in
`projects.json`; there was no way to create them from the GUI.

This plan does **not** cover submodules — see [Out of Scope](#out-of-scope) and
the Plan 5 sketch. It does, however, make two forward-compatible data/UI
decisions so Plan 5 does not force a rewrite.

## Scope

In scope:

- Create a project from the GUI.
- Add a repository to the active project in three modes (spec §20):
  - **Add existing** local repo (pick a directory containing `.git`).
  - **Initialize new** empty repo (`git init` in a chosen location).
  - **Clone from URL** (network, async, with progress + cancel).
- Onboarding empty states:
  - No project exists → centered **Create Project** call-to-action.
  - Project exists but has no repos → centered three add-repo buttons.
- Persistent entry points once the UI is non-empty:
  - **Create Project** = a "New project…" item inside the project switcher
    combo dropdown. (A future app menu entry is out of scope here.)
  - **Add Repository** = a three-button toolbar (icon + tooltip) at the bottom
    of the repo list — the same three actions shown in the empty state.

Out of scope (deferred): see below.

## Decisions

These were settled during brainstorming:

- **Add-repo modes:** all three (existing / init / clone) ship in Plan 4.
- **Add-repo presentation:** three separate buttons (not a dropdown / not a
  tabbed dialog), shown both centered in the empty state and as a bottom
  toolbar in the populated sidebar.
- **Create-project entry:** item in the project switcher combo dropdown (plus a
  later app-menu entry, out of scope now). No standalone `+` button.
- **Clone progress UX:** modal dialog with a determinate progress bar and a
  Cancel button. Progress driven by libgit2's `transfer_progress` callback;
  Cancel = returning non-zero from the callback.
- **Submodules:** separate **Plan 5**, but Plan 4 is built forward-compatible
  (tree-capable repo model, submodules never persisted). Details below.

## Architecture

Three layers, matching the existing codebase layout (`core/` pure git +
storage, `ui/` Qt ViewModels + widgets, async via the QCoro `AsyncRepo` layer
from Plan 3b).

```
core/  GitRepo::init / GitRepo::clone        (libgit2)
       ProjectStore mutations + persistence
         │
ui/    ProjectController mutation slots/signals
       RepoListModel  → tree model (QAbstractItemModel)
         │
ui/    MainWindow empty-state switching
       ProjectSidebar (combo "New project…", QTreeView, 3-button toolbar)
       Dialogs (existing / init / clone) + clone progress modal
```

### 1. Core (`core/`)

- `GitRepo::init(const std::filesystem::path& path) -> Expected<GitRepo>`
  - `git_repository_init`. Error if path is inside an existing repo or the
    directory is non-empty in a way that makes init unsafe.
- `GitRepo::clone(const std::string& url, const std::filesystem::path& dest,
  ProgressCallback cb) -> Expected<GitRepo>`
  - `git_clone` with `fetch.callbacks.transfer_progress` wired to `cb`.
  - `cb(received_objects, total_objects)` returns a bool/int; a non-zero /
    cancel return aborts the clone (libgit2 surfaces this as an error).
  - **Plain clone only** — no submodule recursion in Plan 4.
- `ProjectStore` mutations (the store currently exposes only a raw `projects()`
  vector + `save`/`load`):
  - `createProject(std::string name) -> Project&` — generates a unique `id`,
    appends, returns a reference.
  - `addRepo(const std::string& projectId, RepoRef repo) -> Expected<void>` —
    rejects a duplicate path within the same project.
  - Each mutation is followed by an atomic `save()` to `projects.json`
    (temp + rename, already implemented).
  - `RepoRef` stays the flat user-added-repo shape. Submodules are **not**
    stored here (see forward-compat).

### 2. ViewModel (`ui/`)

- `ProjectController` new slots:
  - `createProject(const QString& name)`
  - `addExistingRepo(const QString& path)`
  - `initRepo(const QString& parentDir, const QString& name)`
  - `cloneRepo(const QString& url, const QString& dest)`
- New signals:
  - `projectCreated(const QString& projectId)`
  - `repoAdded(const QString& path)`
  - `repoAddFailed(const QString& message)`
- After a successful mutation: refresh `ProjectListModel` / `RepoListModel`,
  and auto-activate the new project / select the new repo.
- Clone runs through the existing QCoro `AsyncRepo` async layer (off the main
  thread); the transfer-progress callback marshals progress back to the UI for
  the modal progress dialog.

### 3. UI (`ui/`)

- **RepoListModel → tree model.** Re-implement as a `QAbstractItemModel`
  (tree-capable) instead of the current list model. In Plan 4 it has a single
  level (top-level repos only, no children). This is a forward-compat decision
  for Plan 5 (submodule children) — see below.
- **ProjectSidebar:**
  - Repo list becomes a `QTreeView` (single level for now).
  - Project switcher combo gains a "New project…" item; selecting it opens the
    create-project name dialog.
  - Bottom toolbar with three icon buttons (Add existing / Init new / Clone),
    each with a tooltip — the persistent home of the three add-repo actions.
- **MainWindow empty-state switching** (central area):
  - 0 projects → centered **Create Project** CTA.
  - Active project with 0 repos → centered three add-repo buttons.
  - Otherwise → the existing Changes / History tab content.
- **Dialogs:**
  - Add existing → `QFileDialog` (pick directory).
  - Init new → parent directory + repository name.
  - Clone → URL + destination directory.
- **Clone progress:** modal `QProgressDialog` (determinate, Cancel button)
  bound to the clone progress callback.

## Error handling

- Add existing pointed at a non-repo directory → `repoAddFailed`, surfaced as a
  non-fatal message (status / message box), no crash.
- Init in a non-empty / already-a-repo directory → error, surfaced the same way.
- Clone failure (network, auth, or user cancel) → close the modal and show an
  error message. Cancel is a clean, expected outcome, not a crash.
- Duplicate repo path within a project → rejected by `ProjectStore::addRepo`.
- `ProjectStore` persistence already backs up corrupt JSON and never throws on
  bad data; mutations reuse the existing atomic save.

## Testing (TDD)

Core:

- `GitRepo::init` — creates a valid repo; rejects unsafe targets.
- `GitRepo::clone` — clone from a local `file://` temp repo (no network in
  tests); progress callback is invoked; a cancel return aborts cleanly.
- `ProjectStore` mutations — `createProject` id uniqueness, `addRepo` dedup,
  persistence round-trip (mutate → save → load → equal).

UI:

- `ProjectController` slots against a mock/in-memory store: create project,
  add existing, init, clone (with a fake async repo) emit the right signals and
  refresh models.
- Empty-state switching: 0 projects, empty project, populated — correct widget
  shown.
- `RepoListModel` tree model: single-level behavior matches the old list model
  (so Plan 3b consumers keep working).

## Decomposition

Like Plan 3, split into two sub-plans, each its own plan → implementation cycle:

- **Plan 4a — Core:** `GitRepo::init` / `GitRepo::clone`, `ProjectStore`
  mutations + dedup + persistence.
- **Plan 4b — UI:** `ProjectController` slots/signals, `RepoListModel` tree
  conversion, `ProjectSidebar` (combo item + tree + toolbar), `MainWindow`
  empty states, dialogs, clone progress modal.

## Out of Scope

Deferred to later plans:

- **Submodules** — entire subsystem, **Plan 5** (sketch below).
- **Commit graph visualization** — was the original Plan 4; a separate plan.
  Note the dependency: a submodule's "see its graph" view (Plan 5) needs the
  graph feature to exist first; its history / changes views work without it.
- **App-menu** entry for Create Project (only the combo-dropdown entry ships
  now).
- **Remove / rename** project or repo, and repo reordering.

## Plan 5 sketch — Submodules (not designed here)

Recorded so Plan 4's forward-compat decisions are justified. Designed properly
in its own brainstorming cycle later.

Key insight: a submodule **is** a git repo at a working-directory path. If
submodules are modeled as **dynamic children** (discovered live, never
persisted), then "open a submodule" = open that path as a `GitRepo` and reuse
the entire existing Changes / History / Graph machinery for free.

- **Core:** submodule enumeration + status (`git_submodule_foreach`,
  `git_submodule_status`), `submodule init + update`, recursive clone (run
  init + update after `git_clone`).
- **UI:** the tree `RepoListModel` lazily populates submodule children under
  their parent repo; dirty / uninitialized badges; clicking a submodule opens
  it as a first-class repo (reusing Plan 3b views, and the graph once it exists).
- **Data model:** submodules are **not** persisted in `projects.json` — they are
  computed from the parent repo's git state on demand. Git is the single source
  of truth; no stale submodule list when `.gitmodules` changes.

Forward-compat obligations on Plan 4 (satisfied above):

1. `RepoListModel` is a tree model (`QAbstractItemModel`) from the start.
2. The sidebar repo list is a `QTreeView`.
3. Submodules are never written to `projects.json`; `RepoRef` stays the
   flat user-added-repo shape.
