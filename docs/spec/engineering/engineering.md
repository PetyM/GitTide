# Engineering

How GitTide is built: the layered architecture, the rules that hold across the
whole codebase, the async model, and the build/test setup.

**Scope of this section.** It describes *cross-cutting* design — things that span
many files and are not derivable from any single one. It is deliberately **not**
an API reference: the contract of a class or function lives in **Doxygen
comments next to that symbol**. When you want "what does `GitRepo::stage` do",
read [`core/include/gittide/gitrepo.hpp`](../../../core/include/gittide/gitrepo.hpp);
when you want "why is `core/` allowed to know nothing about Qt", read on.

## Architecture

Clean layering. Dependencies point downward only; each layer is understandable
and testable on its own.

```
┌─ app/  (Qt, process-wide) ───────────────────────────┐
│  QQmlApplicationEngine loads Main.qml; shared services │
│  (ProjectStore registry, libgit2 init) live once here  │
├─ ui/   (Qt Quick/QML + ViewModels) ───────────────────┤
│  Main.qml, Sidebar, WorkingPane, HistoryPane … bound   │
│  to context props: ThemeManager/QmlTheme,              │
│  ProjectController, RepoViewModel (Qt signals; never   │
│  call libgit2). C++ models feed QML.                   │
├─ ui/   (async bridge) ────────────────────────────────┤
│  AsyncRepo — QtConcurrent + QCoro over GitRepo         │
├─ core/ (pure C++23, no Qt) ───────────────────────────┤
│  GitRepo, DiffEngine, GraphBuilder, ProjectStore       │
└─ libgit2 ──────────────────────────────────────────────┘
```

- **`core/`** — pure C++23, no Qt. The git engine and persistence: `GitRepo`
  (RAII over one `git_repository`), `DiffEngine` (libgit2 diff → display model),
  `GraphBuilder` (commit walk → lane layout), `ProjectStore` (JSON project
  registry). Namespace `gittide`. Unit-tested with Catch2, no display needed.
- **`ui/`** — **Qt Quick/QML** views, the C++ ViewModels/models they bind to
  (`ProjectController`, `RepoViewModel`, `RepoListModel`, `ChangedFilesModel`,
  `DiffLinesModel`, `BranchListModel`, `HistoryListModel`, the `GraphColumn`
  `QQuickPaintedItem`, `ThemeManager`/`QmlTheme`), and the async bridge
  `AsyncRepo`. Namespace `gittide::ui`. The static lib links **no QWidgets** —
  only Qt Gui/Qml/Quick/QuickControls2. Controllers expose Qt signals/slots and
  translate between core's `std` types and Qt's. Headless-testable with Qt Test.
- **`app/`** — process-wide composition: a `QGuiApplication` +
  `QQmlApplicationEngine` that wires the context properties and loads
  `qrc:/qml/Main.qml`, plus the single `ProjectStore` registry and one-time
  `libgit2` init ([`app/qml_main.cpp`](../../../app/qml_main.cpp)). A single
  window today; multi-window/session restore is deferred.

### Where to find what

| Concern | Code |
|---------|------|
| Git operations (status/diff/stage/commit/log/submodules/branches) | `core/src/gitrepo.cpp`, `core/include/gittide/gitrepo.hpp` |
| Diff parsing + partial-staging patch synthesis | `core/src/diffengine.cpp` |
| Commit graph lane layout | `core/src/graphbuilder.cpp` |
| Project registry persistence (JSON) | `core/src/projectstore.cpp` |
| Async / off-thread git | `ui/src/asyncrepo.cpp` |
| Controllers (ViewModels) | `ui/src/projectcontroller.cpp`, `ui/src/repocontroller.cpp`, `ui/src/repoviewmodel.cpp` |
| QML views | `ui/qml/*.qml` (loaded from `ui/qml/qml.qrc`) |
| QML context wiring (context props + type registration) | `ui/src/qmlcontext.cpp` |
| Theming (tokens → QML bindings) | `ui/src/theme.cpp`, `ui/src/thememanager.cpp`, `ui/src/qmltheme.cpp` — see [`../design/design.md`](../design/design.md) |

## Cross-cutting invariants

These hold everywhere. Breaking one is a design regression, not a local choice.
The rationale and rejected alternatives behind them are logged in
[`../../decisions.md`](../../decisions.md).

1. **No Qt in `core/`.** Core compiles and tests without Qt on the include path.
   This is what keeps the git engine unit-testable and the layering honest.
2. **libgit2 and nlohmann/json are PRIVATE to `core/`.** No public core header
   includes them — `gitrepo.hpp` only forward-declares `struct git_repository`.
   They never leak onto downstream consumers (see the `PRIVATE` linkage in
   [`core/CMakeLists.txt`](../../../core/CMakeLists.txt)). Tests that need
   `<git2.h>` link libgit2 explicitly.
3. **Core speaks `std`; Qt stays at the boundary.** `std::string` (UTF-8),
   `std::vector`, `std::filesystem::path`, `std::expected`. Qt types appear only
   in `ui/`, and conversion happens in one place at the ViewModel edge
   (`QString::fromStdString(path.generic_u8string())`).
4. **Errors are values.** Core returns `Expected<T>` =
   `std::expected<T, GitError>`; no exceptions cross a layer boundary. A
   `GitError` carries the libgit2 code + message. Worker-thread failures surface
   as a signal, then a non-intrusive UI banner — never a crash.
5. **One owner per `GitRepo`.** `GitRepo` is move-only and not thread-safe.
   Parallelism comes from each worker opening *its own* repo instance, never
   from sharing one across threads.
6. **Paths via `generic_u8string()`, never `.string()`.** libgit2 wants UTF-8
   with forward slashes on every OS; `path.string()` yields ANSI on Windows and
   corrupts non-ASCII names. Keep paths as `std::filesystem::path` internally and
   convert only at the libgit2 edge. Never build git command strings — we use
   the libgit2 API, so there is no shell quoting.

## Async & threading model

The UI thread never blocks; git work runs off it.

- **`AsyncRepo`** ([`ui/include/gittide/ui/asyncrepo.hpp`](../../../ui/include/gittide/ui/asyncrepo.hpp))
  wraps each blocking `GitRepo` call in `QtConcurrent::run` (Qt's global thread
  pool) and exposes it as a `co_await`-able `QCoro::Task`. A **per-repo mutex**,
  held inside the worker lambda, serializes pool access so two awaited ops never
  touch the same `git_repository` at once — invariant #5, enforced. The repo +
  mutex live behind a `shared_ptr` so in-flight work stays valid even if the
  `AsyncRepo` is destroyed first.
- **Why QtConcurrent + QCoro.** QtConcurrent ships with Qt6 (no new dep); QCoro
  adds `co_await` over `QFuture` via FetchContent. Rejected: `std::execution
  par_unseq` (drags in TBB on libstdc++) and a hand-rolled pool (reinvents
  QtConcurrent).
- **Rendering** of graph/log/diff is lazy and virtualized — only visible rows
  render — so a very large history never stalls the UI.

### Network operations & credentials

Fetch, pull, and push run through the same `AsyncRepo` / `QtConcurrent::run`
worker model as all other git ops — they never block the UI thread. Before each
network call, the ViewModel supplies a `Credentials` POD (`sshUseAgent`,
`username`, `password`) containing the auth material for that session. A pure
`chooseCredential` helper (`core/src/credentialselect.cpp`, no Qt) inspects the
remote URL and the libgit2 `allowed_types` bitmask to decide between ssh-agent
and HTTPS userpass — it is unit-testable without a live remote. The libgit2
credential callback (`credentialTrampoline`) delegates to this helper; it never
blocks for a UI dialog. HTTPS tokens are stored in a session map on the
ViewModel and discarded on quit — secure keychain persistence is deferred.

### Fleet fetch-all

Fetching every repo in a project is orchestrated in `ProjectController` (it owns
the active project and the `RepoListModel`), not in `RepoController` (which holds
the single *active* repo). `fetchAll()` iterates the active project's `RepoRef`s
and, for each non-missing one, opens its **own** fresh `AsyncRepo` and awaits
`fetch()`. Each repo therefore gets a distinct `git_repository` handle — the
one-owner invariant (#5) holds without sharing the active repo's handle, and
because fetch only updates remote-tracking refs it is safe alongside an open repo.
Actual parallelism is bounded by Qt's global thread pool (the same pool every
`AsyncRepo` call dispatches to); all coroutines are launched, the pool throttles.

Each repo's `Expected` is captured independently — one failure never aborts the
fleet. Results drive a transient per-row state on `RepoListModel`
(`Idle → Running → UpToDate | Updated | Failed`, plus a refreshed ahead/behind
from a follow-up `syncStatus()`), surfaced through new roles and incremental
`dataChanged`. `ProjectController` exposes a `fetchingAll` flag and an aggregate
summary string for the project header. Session credentials are shared at the
controller level (not per-`RepoViewModel`) so a single on-demand
`CredentialDialog` serves the whole fleet; the prompt is serialized — the first
auth failure prompts once, caches, and the rest reuse it.

### Branch operations & the refresh cascade

Branch enumeration and mutation (list / create / checkout / delete / rename, plus
detached-commit checkout) are pure git operations → they live on `GitRepo` in
`core/` over the libgit2 `git_branch_*` / `git_checkout_tree` /
`git_repository_set_head[_detached]` / `git_stash_*` APIs, returning `Expected<T>`
like the rest of core. `AsyncRepo` wraps each as a `QCoro::Task`; `RepoController`
exposes them as slots and emits the result.

- **Safe-switch invariant — never clobber uncommitted work.** A checkout that
  would overwrite a dirty working tree must not silently discard it. Both branch
  checkout and detached-commit checkout route through one core helper that, on a
  dirty tree, stashes (`git_stash_save`, including untracked), checks out the
  target, then re-applies (`git_stash_pop`). A pop conflict is the single
  non-clean exit: it stops, **keeps** the stash, and returns a `GitError` —
  `HEAD` has moved but no work is lost. Checkout uses `GIT_CHECKOUT_SAFE`, not
  `FORCE`. (Rationale and rejected alternatives: D21.)
- **Cascade.** A successful switch / checkout / create-with-checkout invalidates
  status + history + branches and triggers the same refresh cascade as "switch
  project," scoped to the one repo. Delete / rename (HEAD unchanged) refreshes the
  branch list only.

### Inline selection, commit, and the history diff

There is **no staging area**: the UI owns the commit selection, and `core/` stays
the place that touches the index. This shapes two flows.

- **Working diff is vs `HEAD`.** The editable Changes diff shows *all* of a file's
  working changes against `HEAD` (the index is not the user's model), so `diff()`
  gains a `WorktreeVsHead` target alongside `WorktreeVsIndex` / `IndexVsHead`.
  Because `commitSelection` resets the index to `HEAD` before staging, the
  partial-staging patch (computed from `WorktreeVsIndex` at that moment) lines up
  with the line indices the user picked from the displayed `WorktreeVsHead` diff.
- **Commit from the checked set.** The Changes view holds the checked selection
  (whole files, or specific line indices within a file) as ViewModel state, not
  in the git index. On commit, `RepoController` rebuilds the index to match
  exactly the checked set and then commits it: reset the index to `HEAD`, stage
  each checked whole-file and each checked line-selection (reusing the existing
  file/hunk/line `stage` patch-synthesis, D11), then `commit`. The index is an
  invisible build buffer; the user never manages it. This needs one new core
  primitive — **reset the index to `HEAD`** (`git_reset_default` over all paths /
  unborn-safe) — alongside the existing `stage` / `commit`.
- **History shares the diff view.** Inspecting a commit reuses the same diff
  panel as working changes, so `core/` gains read-only commit-diff endpoints:
  **list a commit's changed files** and **diff one file in a commit** (its tree
  vs its first parent, with the root commit handled via an empty parent tree).
  These mirror the working `status` / `diff` shapes (`FileStatus` / `DiffResult`)
  so the UI renders both through one widget. Per-symbol contracts live in the
  `gitrepo.hpp` Doxygen.

## Logging & diagnostics

GitTide is observable across every layer through one categorised, level-controlled
logging facility. It is the *diagnostic* channel that runs alongside the
error-as-value channel (invariant #4): an `Error`-level log usually accompanies a
returned `GitError`, it does not replace it.

- **The core/no-Qt boundary is a tiny Qt-free facade.** `core/` logs through
  `gittide::logf(level, category, fmt, …)` (in
  [`core/include/gittide/log.hpp`](../../../core/include/gittide/log.hpp)) — a
  `std::format`-based call that depends on nothing but the standard library, so
  invariant #1 holds. It routes through a process-wide `LogBackend` (two
  `std::function`s in `std` types — a `write` sink and a cheap `enabled` gate)
  installed once at startup. With no backend, logging is a silent no-op. (D26.)
- **The app bridges that facade onto Qt.** At composition time `app` calls
  `gittide::ui::installLogging()`
  ([`ui/src/logging.cpp`](../../../ui/src/logging.cpp)), which wires core's
  `LogBackend` to Qt's `QLoggingCategory` machinery: core records route to the
  matching category and the `enabled` gate consults it, so one set of Qt rules
  governs **all** layers. `ui`/`app` C++ logs directly with `qCDebug`/`qCWarning`
  on those categories; QML logs through the `log` context property (a `QmlLog`)
  so GUI diagnostics use the same categories + levels, never a stray
  `console.log`.
- **Categories** are one coherent taxonomy, defined once as `gittide::logcat`
  constants and namespaced `gittide.*`: `git` (libgit2 ops), `repo` (repo/project
  persistence), `async` (worker/refresh cascade), `auth` (credentials), `ui`
  (view-models/QML), `app` (startup/composition).
- **Levels** are `Trace`/`Debug`/`Info`/`Warning`/`Error`, mapped onto Qt's
  `QtMsgType` at the bridge (Trace+Debug → Debug, Error → Critical).
- **Control is global + per-category, via Qt's rules** —
  `QT_LOGGING_RULES="gittide.git.debug=true"` (or a `qtlogging.ini`) raises one
  area while the rest stay quiet. A persisted setting + an in-app toggle is a
  later wish; this cut is the env-var path. (D27.)
- **Sinks: console + a rotating file.** A `qInstallMessageHandler` writes every
  record to stderr and to `gittide.log` (rolled to `gittide.log.1` past a size
  cap) under the app data dir, so a user can attach it to a bug report. (D27.)
- **Paths in logs** honour the path rule — log the `toGitPath()` UTF-8 form, never
  `std::u8string`/`.string()`.

## Code style

Naming, formatting, and the mandatory C++/Qt rules live in
[`code-style.md`](code-style.md) — the standard, enforced by
[`.clang-format`](../../../.clang-format). The codebase conforms to it; keep new
code conformant (see the [Conformance](code-style.md#conformance) note for the
test-layer exception). Principles first: KISS, DRY, SOLID, YAGNI.

## Build & test

- **Toolchain.** C++23, CMake ≥ 3.28. The `core/` and `ui/` layers currently use
  classic `#include` headers (Qt's `moc` does not cooperate with C++ modules).
- **Dependencies.** Qt 6 comes from the system or `aqtinstall` via
  `find_package` — **never** FetchContent (building Qt from source is
  impractical). libgit2, QCoro, and Catch2 are fetched via **FetchContent**
  (pinned tags in [`cmake/Dependencies.cmake`](../../../cmake/Dependencies.cmake)).
  vcpkg is deliberately avoided. Network transports (`USE_SSH`/`USE_HTTPS`) are
  **on** (D28): HTTPS uses the platform TLS backend (OpenSSL on Linux →
  `libssl-dev`, SChannel on Windows, SecureTransport on macOS); SSH links libssh2
  on Linux (`libssh2-1-dev`) and macOS (`brew install libssh2`) so the credential
  callback's ssh-agent / key auth works. **Windows SSH is off for now** (no system
  libssh2; vcpkg-vs-`exec` deferred). clone/fetch/push speak `https://`, `ssh://`,
  scp-like `user@host:path`, and local/`file://` paths.
- **Targets.** `gittide_core` (static lib), `gittide_ui` (static lib, AUTOMOC),
  `gittide_app` (executable), plus test targets below.
- **Tests.** Catch2 for `core/` (`gittide_core_tests`, one ctest entry per case
  via `catch_discover_tests`). Qt Test for `ui/` (`gittide_ui_tests`, a single
  aggregated binary run headless with `QT_QPA_PLATFORM=offscreen`). New UI
  sources go in `ui/CMakeLists.txt`'s `gittide_ui` list; new UI tests go in the
  `gittide_ui_tests` source list in `tests/CMakeLists.txt`. How tests are
  structured (the `TempRepo` helper, the `#include` UI runner) and how to add one:
  [`testing.md`](testing.md).
- **CI.** GitHub Actions matrix: Linux / macOS / Windows
  ([`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml)).
- **Development is test-first (TDD):** write the failing test, then the code.
  See the plans in [`../../plans/`](../../plans/index.md) for the task-by-task
  cadence.

Command reference (configure / build / test / single test) lives in the
repository [`CLAUDE.md`](../../../CLAUDE.md).
