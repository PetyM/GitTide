# GitTide — Design Spec

**Date:** 2026-06-16
**Status:** Approved design, pre-implementation

## 1. Purpose

A cross-platform (Windows / macOS / Linux) desktop GUI git client, similar in spirit to GitHub Desktop, but built around a first-class **Project** concept layered over multiple repositories. Goals that differentiate it:

- A "Project" groups several repositories, with fast switching between projects.
- Easy addition of existing local repositories.
- A commit graph visualization.

The application must be **fast**: no UI blocking, parallel work across repositories, incremental/virtualized rendering.

## 2. Scope

### MVP (v1)
- Projects: create/rename/delete; group repositories; switch active project; persist state.
- Add repository to a project: add existing local repo, clone from URL, init new.
- Per-repo working state: status, stage/unstage (file + hunk + line), commit.
- Diff viewer (file, hunk, intra-line).
- Per-repo commit graph (full branching: branches, merges, tags).
- Project dashboard: **read-only** aggregated status across the project's repos.
- **Multi-window (hybrid model):** each window keeps the in-window project switcher
  (Layout A), AND a window can spawn a new top-level window via an "open project in
  new window" action. Multiple windows may be open at once. Optional de-duplication
  ("focus existing window for this project" instead of opening a second) is a
  setting, off-or-on TBD; not required for first cut. Window/session state persists
  for restore.

### Post-MVP (explicitly out of v1)
- Network ops: push / pull / fetch, plus bulk "fetch/pull all" across a project.
- Branch management: create/switch/delete, merge, conflict resolution UI.
- Aggregated project timeline graph (commits of all repos on one axis).
- **Submodule nested view:** show a repo's submodules as expandable children in the
  RepoList; each submodule is itself a navigable `GitRepo`. Parent-repo status shows a
  submodule as a single changed entry (pointer move), not its recursed internals
  (`GIT_STATUS_OPT_EXCLUDE_SUBMODULES`). Enumeration via libgit2 `git_submodule_foreach`
  (works for local repos regardless of the USE_SSH/USE_HTTPS=OFF build). Targeted for an
  early post-MVP iteration since it touches both Core (submodule listing) and UI (tree).

> Note: the Project concept includes "bulk actions", but the only bulk action in MVP is the read-only aggregated status dashboard. Bulk **network** actions land with the network feature set post-MVP.

## 3. Stack & Build

- **Language:** C++23.
- **GUI:** Qt 6 Widgets (not QML). Graph rendered via `QGraphicsView`.
- **Git:** libgit2 (read and write). Auth/credentials via libgit2 credential callbacks.
- **Async bridge:** QCoro (Qt signals → `co_await` awaitables).
- **Tests:** Catch2 for Core; optional Qt Test for UI smoke tests.
- **Build:** CMake ≥ 3.28.
  - **Qt 6**: from system or `aqtinstall` via `find_package`. (NOT FetchContent — building Qt from source is impractical.)
  - **libgit2, QCoro, Catch2**: FetchContent.
  - **vcpkg**: deliberately avoided.
- **C++ modules:** used in the **Core** layer (pure C++, no Qt). UI layer stays on classic `#include` because Qt `moc` does not cooperate well with module units / `import std`. C++23 language features used throughout.
- **Packaging:** Windows (`windeployqt` + NSIS), macOS (`macdeployqt` + `.dmg`), Linux (AppImage).
- **CI:** GitHub Actions matrix — Linux / macOS / Windows.

## 4. Architecture

Clean layering; each layer independently understandable and testable. Dependencies point downward only.

```
┌─ App (Qt, process-wide) ────────────────┐
│  WindowManager (owns N MainWindows),     │
│  shared services: ProjectStore,          │
│  GitRepo cache, thread pool              │
├─ UI (Qt Widgets, per window) ───────────┤
│  MainWindow, ProjectSidebar, RepoList,   │
│  ChangesView, DiffView, GraphView        │
├─ ViewModels / Controllers ──────────────┤
│  ProjectController, RepoController        │  ← Qt signals; no direct libgit2
├─ Core (pure C++23, no Qt) ──────────────┤
│  GitRepo, DiffEngine, GraphBuilder,      │
│  ProjectStore                            │
└─ libgit2 ────────────────────────────────┘
```

### Multi-window (hybrid)
- A process-wide **WindowManager** owns multiple `MainWindow` instances. Core services
  (`ProjectStore`, the repo/object caches, the thread pool) live once at app scope and
  are shared across windows — windows are views over shared state.
- **"Active project" is per-window UI state**, not a single global. Each window tracks
  which project it currently shows; opening "in a new window" creates another `MainWindow`
  bound to a project.
- Optional de-dup: WindowManager can raise an existing window for a project instead of
  opening a second (a setting). De-dup logic is isolated in WindowManager so the rest of
  the UI is unaware.
- **Session persistence**: which projects have open windows (and geometry) is saved for
  restore on next launch. This session state is separate from the project *registry*
  (`ProjectStore`); see §5.

### Layer boundary rule
- **Core speaks std**: `std::string` (UTF-8), `std::vector`, `std::filesystem::path`, `std::expected`.
- Qt types (`QString`, Qt model classes) appear only at the ViewModel boundary. Conversion lives in one place: `QString::fromStdString(path.generic_u8string())` for display.

### Core modules
- **GitRepo** — RAII wrapper over libgit2. `status`, `stage/unstage` (file/hunk/line), `commit`, `log`, `diff`. No Qt → unit-testable. One `GitRepo` instance is **not** shared across threads; each repo has a single serialized operation queue.
- **GraphBuilder** — from a libgit2 commit walk, computes lane layout (which branch maps to which column) and edges. Pure function `build(commits) → GraphLayout`, testable with synthetic DAGs.
- **ProjectStore** — JSON persistence of projects. Add local / clone / init. Atomic writes.
- **DiffEngine** — parses libgit2 diff into a model for `DiffView` (hunks, lines, intra-line).

## 5. Data Model

### ProjectStore JSON (`~/.config/gittide/projects.json` or OS-appropriate config dir)
```json
{
  "version": 1,
  "activeProject": "uuid",
  "projects": [
    {
      "id": "uuid",
      "name": "Work",
      "repos": [
        {"path": "/home/u/api-server", "alias": "api-server"},
        {"path": "/home/u/web-client", "alias": "web-client"}
      ],
      "lastActiveRepo": "/home/u/api-server"
    }
  ]
}
```
- Paths absolute. A repo is only a reference to disk (no copying).
- `version` for schema migrations.
- **Atomic write**: write temp file + rename, to avoid data loss on crash.
- `activeProject` is retained as the **last-focused** project (a hint for which window
  to open at launch when there is no richer session). It is no longer "the one active
  project" — under multi-window there can be several open at once.

### Window session (multi-window restore) — separate file
Window/session state is persisted separately from the project registry, e.g.
`~/.config/gittide/session.json`, so opening/closing/moving windows never rewrites the
project registry:
```json
{
  "version": 1,
  "windows": [
    {"projectId": "uuid", "geometry": "<base64 QByteArray>", "lastActiveRepo": "/home/u/api-server"}
  ]
}
```
Implemented in Plan 2 (UI shell), owned by WindowManager. Listed here so the data model
is coherent; the Core `ProjectStore` (Plan 1) stays the registry only.

### Core types (no Qt)
```cpp
struct FileStatus { std::filesystem::path path; StatusFlag flags; };          // staged/modified/untracked/...
struct CommitNode { std::string oid; std::string summary, author; int64_t time;
                    std::vector<std::string> parents; int lane; };            // lane from GraphBuilder
struct DiffHunk  { int oldStart, newStart; std::vector<DiffLine> lines; };
```

## 6. Path Handling (explicit rule)

Paths with spaces / special characters / non-latin names must work everywhere.

- **Use `std::filesystem::path` internally everywhere** — never raw `std::string`. It handles separators and native encoding.
- **libgit2 expects UTF-8 with forward slashes** on all OSes. Convert with `path.generic_u8string()`, **never** `path.string()` (on Windows `string()` yields ANSI → breaks non-ASCII). Wrap in single-point helpers `to_git_path()` / `from_git_path()`.
- **Windows is UTF-16 natively.** `std::filesystem::path` holds this correctly; corruption only happens if flattened to `std::string` in the wrong encoding. Never stringify a path prematurely.
- **No building git command strings.** We use the libgit2 API, not a CLI shell, so shell quoting / space issues do not arise.

## 7. Data Flow

### Switch project
```
click project → ProjectController.activate(id)
  → ProjectStore loads repos
  → for each repo: GitRepo::open() (lazy; only active repo fully loaded)
  → signal projectChanged → UI redraws RepoList
  → dashboard: async status of all repos (parallel, off main thread)
```

### Stage → commit
```
RepoController holds GitRepo
  ChangesView stage file → GitRepo::stage(path) → emit statusChanged → refresh
  Commit → GitRepo::commit(msg, author from git config) → emit historyChanged → graph + log refresh
```

## 8. Performance & Concurrency

The app must be fast and never block the UI.

- **Coroutines (C++20)** at the UI↔Core boundary: git ops modeled as `co_await`-able tasks instead of callback chains, bridged via **QCoro**.
- **Parallel execution**: dashboard status of N repos computed in parallel (thread pool / `std::for_each(par_unseq)` where the work is pure); graph build and diff computed off the main thread. Independent repos = independent tasks.
- **UI thread never blocks.** Per-repo serialized queue (one repo = one owner); across repos, parallel.
- **Lazy + virtualized rendering**: graph / log / diff render only visible rows and load incrementally. A 100k-commit graph must not stall the UI.

## 9. Error Handling

- **GitRepo returns `std::expected<T, GitError>`** (C++23) — no exceptions across layers. `GitError` = libgit2 code + message.
- ProjectStore: corrupt/missing JSON → start empty + back up the bad file; never crash.
- Repo missing from disk (deleted directory) → mark "missing" in UI; do not crash the project.
- Worker-thread errors → `operationFailed(GitError)` signal → non-intrusive banner in UI.

## 10. UI Layout

Chosen layout: **A — two-level sidebar** (GitHub-Desktop-like).

- Left: project switcher at top; repo list of the active project below.
- Main area: tabs **Changes** / **History + Graph**.
- Changes tab: staged/unstaged file list beside the diff view; commit message box at the bottom.
- History tab: commit graph + log.
- **Multi-window (hybrid):** the in-window project switcher stays. An "open project in
  new window" action (sidebar context menu / shortcut) asks WindowManager for another
  window. Layout A is the per-window layout, replicated across windows.
- **Submodules (post-MVP):** RepoList rows are a tree — submodules appear as expandable
  children under their parent repo; selecting one navigates into it as its own repo view.

## 11. Testing

- **Core unit tests** (no Qt, no display):
  - `GitRepo` against throwaway repos created in-test (libgit2 init + commits under `std::filesystem::temp_directory`).
  - `GraphBuilder` against synthetic DAGs.
  - `ProjectStore`: JSON round-trip, migration, corrupt-input handling.
- Framework: Catch2 (FetchContent) for Core; Qt Test optionally for UI smoke.
- CI: GitHub Actions matrix Linux / macOS / Windows.
