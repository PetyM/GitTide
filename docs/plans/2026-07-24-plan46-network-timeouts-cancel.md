# Plan 46 — Network op timeouts & cancellation (+ unbounded-wait audit)

> **For agentic workers:** implement this plan task-by-task, test-first.

| | |
|--|--|
| **Date** | 2026-07-24 |
| **Status** | `done` |
| **Spec** | `spec/engineering/engineering.md §Network operations & credentials` (Timeouts & cancellation), `spec/design/design.md §sync cluster` |
| **Depends on** | Plan 11 (fleet fetch-all), Plan 12 (sync transfer progress), Plan 37 (keychain) |

**Goal:** A fetch/pull/push can no longer hang the UI forever (motivating case:
fetching an internal remote while off-VPN). Every blocking network/IO path gains
a timeout or a bounded escape, and the user can always cancel an in-flight sync.

**Architecture:** Three defence layers for git network ops — process-global
libgit2 server timeouts (HTTPS backstop), a per-op cancel flag reusing the
existing `ProgressCallback` (SSH-capable, transfer-phase abort), and a UI
`QTimer` watchdog + generation counter on `RepoController` that guarantees the UI
regains control at 30 s even while the worker thread stays blocked. The cancel
flag needs **no core signature change** — `progressSink()` returning `false`
already aborts via the existing trampoline. Secondary audit fixes bound
`submoduleTree()` recursion and the Qt HTTP requests.

**Tech stack:** libgit2 `git_libgit2_opts` (`GIT_OPT_SET_SERVER_*_TIMEOUT`, ≥1.7),
QCoro tasks, `QTimer`, `std::shared_ptr<std::atomic<bool>>`,
`QNetworkRequest::setTransferTimeout`.

## Global constraints

- No Qt in `core/`; cancellation stays value-based (the `bool` ProgressCallback).
- One owner per `GitRepo`; the watchdog must tolerate a still-running worker
  holding the per-repo mutex (dangling suspended coroutine, guarded by
  `QPointer self` + the worker's captured `impl` `shared_ptr`).
- All 212 existing tests keep passing.

---

## Task 1: libgit2 server timeouts — [x]

**Files:** Modify `core/src/libgit2context.cpp`.
- [x] Set `GIT_OPT_SET_SERVER_CONNECT_TIMEOUT` (10 s) + `GIT_OPT_SET_SERVER_TIMEOUT`
  (30 s) in the ctor, `LIBGIT2_VER`-guarded.

## Task 2: submoduleTree depth cap — [x]

**Files:** `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`,
`tests/test_git_repo_submodules.cpp`.
- [x] Test: `submoduleTree(kMaxSubmoduleDepth)` returns empty; depth 0 still descends.
- [x] Add `static constexpr int kMaxSubmoduleDepth = 20`; add `depth = 0` param;
  short-circuit at the cap; recurse with `depth + 1`.

## Task 3: core cancel-abort tests — [x]

**Files:** `tests/test_git_repo_sync.cpp`.
- [x] Assert a `ProgressCallback` returning `false` makes `fetch`/`push` fail.

## Task 4: RepoController cancel flag + watchdog — [x]

**Files:** `ui/include/gittide/ui/repocontroller.hpp`, `ui/src/repocontroller.cpp`.
- [x] `m_syncCancel` (fresh per op), `m_syncWatchdog`, `m_syncGen`, `m_syncActive`,
  `kSyncTimeout`; `beginSync()`/`endSync()`/`cancelSync()`.
- [x] `progressSink()` observes the flag; `fetch`/`pull`/`push` wrapped with
  `beginSync()` + post-await generation guard + `endSync()`.

## Task 5: cancelSync invokable + Cancel button — [x]

**Files:** `ui/include/gittide/ui/repoviewmodel.hpp`, `ui/src/repoviewmodel.cpp`,
`ui/qml/BranchBar.qml`.
- [x] `RepoViewModel::cancelSync()` forwards to the controller; **Cancel** button
  in the sync-progress cluster.

## Task 6: fleet fetch-all cancel — [x]

**Files:** `ui/include/gittide/ui/projectcontroller.hpp`, `ui/src/projectcontroller.cpp`.
- [x] `m_fleetCancel` (fresh per `fetchAll`), relayed by each `fetchOne` callback;
  `cancelFetchAll()`.

## Task 7: Qt HTTP transfer timeouts — [x]

**Files:** `ui/src/forgeclient.cpp`, `ui/src/avatarservice.cpp`.
- [x] Per-request `setTransferTimeout(30 s)` (survives an injected NAM).

## Task 8: UI cancel state-machine test — [x]

**Files:** `tests/ui/test_repo_controller.cpp`.
- [x] `fetch_toggles_sync_busy`, `cancel_sync_when_idle_is_a_noop`,
  `cancel_sync_aborts_in_flight_fetch` (deterministic — `beginSync()` runs before
  the first `co_await`, so a synchronous `cancelSync()` supersedes the op).

---

## Outcome

- **Shipped:** fetch/pull/push/clone and fleet fetch-all can no longer hang the
  UI; a 30 s watchdog + **Cancel** button always return control, with libgit2
  server timeouts as an HTTPS backstop and the cancel flag aborting the transfer
  phase (incl. SSH). `submoduleTree()` recursion and the forge/avatar HTTP
  requests are bounded. Keychain awaits deliberately left unbounded (documented).
- **Spec updated:** `spec/engineering/engineering.md §Network operations &
  credentials` gained the **Timeouts & cancellation** subsection; `spec/design/
  design.md` notes the Cancel affordance in the sync cluster.
- **Code:** `LibGit2Context` (server timeouts); `GitRepo::submoduleTree(int depth)`
  + `kMaxSubmoduleDepth`; `RepoController` `beginSync/endSync/cancelSync` +
  watchdog/generation; `RepoViewModel::cancelSync`; `BranchBar.qml` Cancel button;
  `ProjectController::cancelFetchAll` + `m_fleetCancel`; per-request transfer
  timeouts in `ForgeClient`/`AvatarService`. Tests in `test_git_repo_sync.cpp`,
  `test_git_repo_submodules.cpp`, `tests/ui/test_repo_controller.cpp`.
