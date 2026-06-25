# Plan 21 — Live refresh (file-system watching + fleet poll)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [x]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-06-25 |
| **Status** | `done` |
| **Spec** | `spec/engineering/engineering.md §Live refresh`; `decisions.md` D35 |
| **Depends on** | Plan 3b (async + refresh cascade), Plan 11 (fleet fetch-all) |

**Goal:** The GUI tracks reality on its own. The active repository refreshes
automatically when files change or git state moves (in-app or from a terminal /
editor); other repositories in the project are kept roughly current by a poll.
No manual refresh needed.

**Architecture:** All new code is in `ui/` — `QFileSystemWatcher` and `QTimer`
are Qt, banned from `core/`. The single new piece of `core/` is a pure read
helper, `GitRepo::watchTargets()`, that computes *which directories to watch*
under libgit2's ignore rules (keeping libgit2 in `core/`). A new ui type
**`RepoWatcher`** wraps the watcher + a debounce timer, classifies a batch into
*worktree* vs *git-dir* scope, and emits a signal per scope. `RepoController`
owns one `RepoWatcher` for the active repo and connects its signals to the
existing refresh methods (a worktree change → `refreshStatus`; a git-dir change →
a new `refreshAll`), re-arming the watch set after each batch and muting it
around its own mutations. `ProjectController` gains a window-active-gated poll
that refreshes the non-active repo rows. `Main.qml` re-syncs on window focus.

**Tech stack:** Qt `QFileSystemWatcher`, `QTimer`, QCoro tasks, libgit2
`git_ignore_path_is_ignored` / `git_repository_path` / `git_repository_workdir`.

## Global constraints

- Invariants in `spec/engineering/engineering.md` — especially **no Qt in
  `core/`** (#1), **libgit2 private to `core/`** (#2), **paths via
  `generic_u8string()`** (#6).
- Refreshes must stay **read-only** so the watcher never feeds back on itself.
- New `core/` sources → `core/CMakeLists.txt`; new `ui/` sources →
  `ui/CMakeLists.txt`; new tests → the matching list in `tests/CMakeLists.txt`
  (UI tests also need the `#include` + `QTest::qExec` edit in `tests/ui/main.cpp`).
- Debounce interval must be **injectable** so tests run fast and deterministic.

---

## Task 1: Core — `GitRepo::watchTargets()`

**Files:** Create `core/include/gittide/watch.hpp`; modify
`core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`; test
`tests/test_git_repo_watch.cpp` (+ `tests/CMakeLists.txt`).

**Interfaces:**
```cpp
// watch.hpp — std-only, no Qt, no libgit2
namespace gittide {
struct WatchTargets {
    std::filesystem::path              workdir; ///< working-tree root
    std::filesystem::path              gitDir;  ///< the .git directory
    std::vector<std::filesystem::path> dirs;    ///< all dirs to watch (worktree non-ignored + gitdir subtree)
};
}
// gitrepo.hpp
Expected<WatchTargets> watchTargets() const;
```

- [x] **Step 1: failing test** — in a `TempRepo` with `src/a.txt`, an ignored
      `build/` (via `.gitignore`), and a `.git` dir: assert `dirs` contains the
      root and `src`, contains the `.git` root, **excludes** `build`, and that
      `gitDir`/`workdir` are set. Fails to compile (method absent).
- [x] **Step 2: implement** — walk `workdir` with `std::filesystem`; for each
      directory call `git_ignore_path_is_ignored` and prune ignored subtrees and
      the `.git` dir; separately enumerate every directory under
      `git_repository_path`. Paths kept as `std::filesystem::path`; convert to the
      libgit2 edge with `generic_u8string()`.
- [x] **Step 3: verify** — run `gittide_core_tests`; add edge cases (bare-ish /
      empty repo returns at least workdir + gitDir).

## Task 2: UI — `AsyncRepo::watchTargets()`

**Files:** `ui/include/gittide/ui/asyncrepo.hpp`, `ui/src/asyncrepo.cpp`; test in
`tests/ui/test_async_repo.cpp`.

**Interfaces:** `QCoro::Task<gittide::Expected<gittide::WatchTargets>> watchTargets();`

- [x] **Step 1: failing test** — await `watchTargets()` on an open `AsyncRepo`,
      assert it returns the expected dirs (mirrors Task 1, through the bridge).
- [x] **Step 2: implement** — same `QtConcurrent::run` + `scoped_lock` pattern as
      `status()`.
- [x] **Step 3: verify** — `gittide_ui_tests`.

## Task 3: UI — `RepoWatcher`

**Files:** Create `ui/include/gittide/ui/repowatcher.hpp`,
`ui/src/repowatcher.cpp` (+ `ui/CMakeLists.txt`); test
`tests/ui/test_repo_watcher.cpp` (+ CMake + `main.cpp` runner edits).

**Interfaces:**
```cpp
class RepoWatcher : public QObject {
    Q_OBJECT
public:
    explicit RepoWatcher(int debounceMs = 300, QObject* parent = nullptr);
    void watch(const gittide::WatchTargets& targets); ///< replace the watch set
    void clear();
    void mute();    ///< drop events (self-induced) until unmute
    void unmute();  ///< resume after the debounce tail
signals:
    void worktreeChanged(); ///< only worktree dirs changed → status scope
    void gitDirChanged();   ///< something under the git dir changed → full cascade
};
```

- [x] **Step 1: failing test** — construct with a small debounce (e.g. 20 ms) and
      `watch()` a `TempRepo`'s targets. Writing a worktree file emits
      `worktreeChanged` (and not `gitDirChanged`); touching a file under `.git`
      emits `gitDirChanged`. A burst of edits coalesces to a single emission.
      `mute()` suppresses; `unmute()` restores. Use `QSignalSpy::wait`.
- [x] **Step 2: implement** — own a `QFileSystemWatcher` + single-shot `QTimer`.
      `directoryChanged`/`fileChanged` record the scope (gitDir if the path is
      under `targets.gitDir`, else worktree) and (re)start the timer; on timeout
      emit the accumulated scope(s) and clear. `mute` sets a flag checked in the
      slots; `unmute` clears it after one debounce interval.
- [x] **Step 3: verify** — headless `gittide_ui_tests`; no Qt warnings.

## Task 4: UI — wire `RepoWatcher` into `RepoController`

**Files:** `ui/include/gittide/ui/repocontroller.hpp`,
`ui/src/repocontroller.cpp`; test `tests/ui/test_repo_controller.cpp`.

**Interfaces:** private `QCoro::Task<void> refreshAll();` (status + branches +
history + sync); private `QCoro::Task<void> rearmWatch();` (fetch `watchTargets`,
`m_watcher->watch`). A test seam to set a small debounce.

- [x] **Step 1: failing test** — open a `TempRepo` through the controller, then
      **externally** write a new file; spin the event loop; assert `statusChanged`
      fires with the new file *without* any explicit `refresh*` call. A second
      test: an external commit (move HEAD) triggers `historyReady`.
- [x] **Step 2: implement** — on `open()` success, `co_await rearmWatch()`.
      Connect `worktreeChanged → refreshStatus (+ active diff) then rearmWatch`;
      `gitDirChanged → refreshAll then rearmWatch`. Extract `refreshAll()` and
      reuse it from `RepoViewModel::open()`'s cascade. Bracket each mutating slot
      with `m_watcher->mute()/unmute()` (coroutine-local) so self-writes don't
      double-refresh.
- [x] **Step 3: verify** — full `gittide_ui_tests` stays green (existing refresh
      behaviour unchanged).

## Task 5: UI — `ProjectController` fleet poll

**Files:** `ui/include/gittide/ui/projectcontroller.hpp`,
`ui/src/projectcontroller.cpp`; test `tests/ui/test_project_controller.cpp`.

**Interfaces:** `Q_INVOKABLE void setWindowActive(bool active);` (starts/stops the
poll). Private `QTimer m_pollTimer;` + `QCoro::Task<void> pollRepos();`.

- [x] **Step 1: failing test** — with an active project of two repos, commit in
      the non-active one externally, drive one poll tick, assert its row's
      `ShortOidRole` / ahead-behind / status updates; the active repo's row is
      left to the live watcher (skipped).
- [x] **Step 2: implement** — a `QTimer` (interval a named constant, e.g. 5 s),
      started only while `setWindowActive(true)`. Each tick opens its **own**
      `AsyncRepo` per non-active, non-missing top-level repo (one-owner invariant),
      reads HEAD + dirty + local `syncStatus`, and updates the row via the
      existing `RepoListModel` setters. Skip the active repo.
- [x] **Step 3: verify** — `gittide_ui_tests`; no per-tick churn when the project
      is empty.

## Task 6: QML — focus re-sync + poll gating

**Files:** `ui/qml/Main.qml`; `ui/include/gittide/ui/repoviewmodel.hpp`,
`ui/src/repoviewmodel.cpp` (add `Q_INVOKABLE void resync()`); test
`tests/ui/test_qml_shell.cpp` (binding present) and a `RepoViewModel::resync`
unit assertion.

- [x] **Step 1: failing test** — assert `RepoViewModel::resync()` exists and kicks
      the controller's `refreshAll` (observe a resulting signal); assert the
      `appWindow` wires `onActiveChanged`.
- [x] **Step 2: implement** — `RepoViewModel::resync()` → controller `refreshAll`.
      `Main.qml`: `onActiveChanged: { if (active) { repoVm.resync(); } projectController.setWindowActive(active); }`.
- [x] **Step 3: verify** — headless QML shell test loads clean; manual smoke
      (edit a file in a terminal → Changes updates).

---

## Outcome

- **Shipped:** the GUI now tracks reality on its own. The **active repo** is
  live-watched — editing files, or external `git`/editor activity (commit,
  checkout, rebase, stage), refreshes the changed-file list, the open diff,
  history, branches, and sync status within a debounced moment, with no manual
  action. **Other repos** in the project have their sidebar sync counts refreshed
  by a window-active-gated poll. A window focus-in re-syncs the active repo to
  catch in-place edits the directory watcher can miss.
- **Spec updated:** `spec/engineering/engineering.md` §"Live refresh — watching the
  working tree and `.git`"; `decisions.md` D35.
- **Code:**
  - `core/include/gittide/watch.hpp` (`WatchTargets`) + `GitRepo::watchTargets()`
    — the directory set to watch, computed under libgit2 ignore rules.
  - `AsyncRepo::watchTargets()` — off-thread bridge.
  - `ui/.../repowatcher.{hpp,cpp}` (`RepoWatcher`) — `QFileSystemWatcher` + debounce,
    classifies worktree vs git-dir scope, with mute/unmute.
  - `RepoController` — owns the watcher, arms it on `open()`, `refreshAll()` cascade,
    `rearmWatch()`, and muting around mutations **and** the watch-driven refresh
    handlers (closing a feedback loop: libgit2 reads touch on-disk `.git` caches).
  - `ProjectController::setWindowActive()` + `pollRepos()` (`QTimer`) — the fleet poll.
  - `RepoViewModel::resync()` + `Main.qml` `onActiveChanged` — focus re-sync + poll gating.
- **Tests:** `tests/test_git_repo_watch.cpp` (core), `tests/ui/test_repo_watcher.cpp`,
  plus new cases in `test_async_repo.cpp`, `test_repo_controller.cpp`,
  `test_project_controller.cpp`, `test_repo_view_model.cpp`. (`TempRepo::writeFile`
  now creates parent dirs.)
- **Known gap:** the in-place-edit case (a write that changes no directory entry,
  e.g. `echo >> f`) is covered by the focus re-sync, not live — a deliberate cut
  to avoid per-file watching (D35).
