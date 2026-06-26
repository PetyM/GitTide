# Design — Submodule init / update from the GUI

**Status:** shipped
**Date:** 2026-06-26
**Realises:** the deferred "full submodule support" wish noted in
[`context-menus.md` §6](context-menus.md) and the deferred actions in
[`product.md` § Submodules](product.md).

## Problem

When a freshly cloned repo has submodules, they appear in the sidebar tree as
**greyed-out (uninitialised)** rows. There is no way to initialise or update them
from the GUI — the user must drop to a terminal (`git submodule update --init`).
Two gaps:

1. **No GUI action** to initialise or update a submodule.
2. **No external-change refresh** — updating submodules in a terminal does not
   refresh the sidebar tree; the rows stay stale until a full reload.

## What already exists (reused, not rebuilt)

- **Core** `GitRepo::reinitSubmodule(path)` = `git_submodule_update(init=1)` with
  `GIT_CHECKOUT_FORCE`. This single call covers **both** cases for one submodule:
  uninitialised → clone + checkout the pinned commit; initialised-but-off-pin →
  checkout the pinned commit. (`core/src/gitrepo.cpp`.)
- **Core** `GitRepo::deinitSubmodule(path)` — empties the working dir, keeps the
  gitlink.
- **Core** `GitRepo::submoduleTree()` — recursive enumeration with per-node
  status (Clean / Dirty / Uninitialized) and pinned short OID.
- **Async** `AsyncRepo::reinitSubmodule` / `deinitSubmodule` already wrap both
  (`ui/src/asyncrepo.cpp`).
- **Refresh infra (D35):** `RepoWatcher` watches the active repo's git-dir /
  worktree (debounced 300 ms) and drives refreshes; `ProjectController::pollRepos`
  re-reads non-active repos every 5 s while the window is active.
- **Model:** `RepoListModel` builds the submodule subtree in `setRepos()` via
  `submoduleTree()` (today only as a full `beginResetModel` rebuild).

So the work is mostly **UI wiring + targeted refresh**, plus one small core
method for the bulk action.

## Decisions

- **Scope = any repo in the sidebar.** The greyed rows can belong to a non-active
  repo, so operations run via a *transient* repo handle in `ProjectController`
  (the same one-owner pattern `pollRepos` already uses), not only via the active
  repo's `AsyncRepo`. (Preserves the one-owner-per-`GitRepo` invariant.)
- **"Update all" is one level, drill deeper by navigating.** `Update all
  submodules` updates only the *direct* submodules of the clicked node. Recursion
  is **not** forced (it is not always wanted). To go deeper: once a submodule is
  initialised it becomes a tree node with its own greyed children — right-click
  *it* and `Update all submodules` again. The bulk action therefore appears on
  **top-level repo rows and on initialised submodule rows**.
- **Deinitialise is exposed** in the submodule right-click menu (core
  `deinitSubmodule` already exists; symmetric with init).
- **Refresh reuses D35**, no new `QFileSystemWatcher`. (A fresh watcher would be
  non-recursive and awkward across N submodule dirs; the existing git-dir watch +
  5 s poll already see the relevant writes.)

## Architecture

Layers touched, downward-only (`app → ui → core`):

### Core (`core/gitrepo`)

Add one method (the only new core surface):

```cpp
/// Initialise/update every DIRECT submodule of this repo to its pinned commit.
/// One level only — does not recurse into nested submodules. Mirrors
/// `git submodule update --init` (no --recursive). Stops at the first failure.
Expected<void> updateSubmodules();
```

Implementation: enumerate direct submodules (`git_submodule_foreach`, no descent)
and call the existing `reinitSubmodule` logic per entry. Recursion is achieved by
the caller invoking `updateSubmodules()` on the child repo (a submodule *is* a
repo), which the UI offers per node.

`reinitSubmodule` / `deinitSubmodule` / `submoduleTree` are unchanged.

### Async bridge (`ui/asyncrepo`)

Add `QCoro::Task<Expected<void>> updateSubmodules()` — same `QtConcurrent::run` +
`std::scoped_lock(impl->mutex)` pattern as the existing wrappers.

### ProjectController (`ui/`)

Owns the sidebar `RepoListModel`. Add `Q_INVOKABLE` coroutine slots, each opening
a **transient** `AsyncRepo` for `repoPath` (as `pollRepos` does), running the op,
then refreshing that repo's subtree on success:

- `initSubmodule(QString repoPath, QString submodulePath)` → `reinitSubmodule`.
- `updateAllSubmodules(QString repoPath)` → `updateSubmodules` (one level).
- `deinitSubmodule(QString repoPath, QString submodulePath)` → `deinitSubmodule`.

Each sets a per-row **busy** flag for the duration, clears it on completion, and
emits `submoduleOpFailed(repoPath, submodulePath, message)` on error (routed to
the existing error-overlay).

**Path resolution (unambiguous):** a submodule *is* a repo, so the handle used for
an op is always the **immediate parent node's** working dir:

- `initSubmodule` / `deinitSubmodule` on a submodule node → `repoPath` = the
  node's **immediate parent** path, `submodulePath` = the node's path relative to
  that parent. (For a direct submodule of a top-level repo, the parent is that
  repo.)
- `updateAllSubmodules` on a node → `repoPath` = that **node's own** path, so
  libgit2 enumerates *its* direct submodules.

Nested levels need no special case: each node's parent is itself a real repo once
initialised.

### RepoListModel (`ui/`)

Add a **targeted** refresh (no full reset — preserves expansion & selection):

```cpp
/// Re-scan and rebuild the submodule subtree under the repo at `repoPath`.
void refreshSubmodules(const QString& repoPath);
```

It locates the root node by path, re-runs `submoduleTree()`, and reconciles the
children via `beginRemoveRows`/`beginInsertRows` (+ `dataChanged` for status/OID
changes on surviving rows). Add a `BusyRole` so a row can show a spinner while its
op is in flight, and a role exposing each submodule row's **owning repo path** (or
walk to the root node) so QML can call the controller.

### UI / QML

All three entry points the user requested:

1. **Inline button on a greyed (uninit) row** — `Sidebar.qml` renders a small
   "Init" button on rows where `row.uninit`; click → `initSubmodule(...)`.
2. **Right-click on a submodule row** — today `Sidebar.qml` bails on `row.isSub`.
   Instead show a **submodule context menu**:
   - *Initialize / Update submodule* (→ `initSubmodule`) — label adapts to status.
   - *Update all submodules* (→ `updateAllSubmodules`) — only when the row is an
     initialised submodule (has children to drill into).
   - *Deinitialize submodule* (→ `deinitSubmodule`) — only when initialised.
3. **"Update all submodules" on a top-level repo** — added to the existing
   `RepoContextMenu.qml` → `updateAllSubmodules(repoPath)`.

While a row is busy, its action is disabled (the `BusyRole` guard also blocks
double-trigger in the controller).

### External-change refresh (the "filesystem watcher" requirement)

Reuse D35 rather than adding a watcher:

- **Active repo:** a terminal `git submodule update` writes under
  `<repo>/.git/modules/…` and the submodule working dir, which the active
  `RepoWatcher` already reports as a git-dir / worktree change. Hook that signal
  (via a `RepoController → ProjectController` connection) to also call
  `RepoListModel::refreshSubmodules(activeRepoPath)`. Debounce is already applied.
- **Non-active repos:** extend `pollRepos()` (already iterating every repo every
  5 s) to also re-scan submodule status and call `refreshSubmodules` when it has
  changed.

Net effect: the active repo's submodules refresh on the next debounce tick;
others within the 5 s poll — covering both our own ops and external terminal use.

## Error handling

- Core returns `Expected<void>`; the controller surfaces failures as
  `submoduleOpFailed(...)` → existing error-overlay. Partial bulk failure stops at
  the first error and refreshes whatever succeeded.
- The `BusyRole` flag prevents re-entrancy on a row whose op is in flight.

## Testing (TDD — failing test first)

**Core** (`tests/test_git_repo_submodules.cpp`, Catch2, `TempRepo`):

- `reinitSubmodule` on an uninitialised submodule → status becomes `Clean` and the
  working dir is populated.
- `updateSubmodules` initialises **all direct** submodules but **not** nested ones
  (one-level guarantee).
- `deinitSubmodule` returns a node to `Uninitialized`.

**UI** (headless QtTest):

- `RepoListModel::refreshSubmodules` updates a node's status/children and emits the
  right model signals **without** a full reset (selection/expansion preserved).
- `ProjectController::initSubmodule` runs the op on a non-active repo and the
  subtree reflects the new state; failure emits `submoduleOpFailed`.

## Out of scope (deferred)

- `git submodule update --remote` (advance the pin to the submodule's upstream) —
  this design only updates **to the pinned commit**.
- Adding / removing submodules (`git submodule add`/`deinit -f` + `.gitmodules`
  editing).
- Forced full-depth recursive update in one click (intentionally — navigate
  per-node instead).
- Sync of submodule URL changes (`git submodule sync`).
