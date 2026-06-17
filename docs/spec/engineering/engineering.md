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
│  WindowManager owns N MainWindows; shared services    │
│  (ProjectStore registry, libgit2 init) live once here │
├─ ui/   (Qt Widgets + ViewModels, per window) ─────────┤
│  MainWindow, ProjectSidebar, ChangesView, DiffView,   │
│  HistoryView … and controllers: ProjectController,    │
│  RepoController (Qt signals; never call libgit2)       │
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
- **`ui/`** — Qt Widgets, ViewModels (`ProjectController`, `RepoController`,
  the list/tree models), and the async bridge `AsyncRepo`. Namespace
  `gittide::ui`. Controllers expose Qt signals/slots and own the translation
  between core's `std` types and Qt's. Headless-testable with Qt Test.
- **`app/`** — process-wide composition: `WindowManager` owns multiple
  `MainWindow`s, the single `ProjectStore` registry, and one-time `libgit2`
  init ([`app/main.cpp`](../../../app/main.cpp)).

### Where to find what

| Concern | Code |
|---------|------|
| Git operations (status/diff/stage/commit/log/submodules) | `core/src/gitrepo.cpp`, `core/include/gittide/gitrepo.hpp` |
| Diff parsing + partial-staging patch synthesis | `core/src/diffengine.cpp` |
| Commit graph lane layout | `core/src/graphbuilder.cpp` |
| Project registry persistence (JSON) | `core/src/projectstore.cpp` |
| Async / off-thread git | `ui/src/asyncrepo.cpp` |
| Controllers (ViewModels) | `ui/src/projectcontroller.cpp`, `ui/src/repocontroller.cpp` |
| Multi-window + session restore | `ui/src/windowmanager.cpp` |
| Theming (tokens → QSS) | `ui/src/theme*.cpp` — see [`../design/design.md`](../design/design.md) |

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
- **Dashboard fan-out.** Aggregated project status runs one `QtConcurrent::run`
  per repo, each opening its **own** `GitRepo` — no shared mutable state, full
  parallelism across independent repos.
- **Why QtConcurrent + QCoro.** QtConcurrent ships with Qt6 (no new dep); QCoro
  adds `co_await` over `QFuture` via FetchContent. Rejected: `std::execution
  par_unseq` (drags in TBB on libstdc++) and a hand-rolled pool (reinvents
  QtConcurrent).
- **Rendering** of graph/log/diff is lazy and virtualized — only visible rows
  render — so a very large history never stalls the UI.

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
  off — clone/init use local and `file://` paths in this milestone.
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
