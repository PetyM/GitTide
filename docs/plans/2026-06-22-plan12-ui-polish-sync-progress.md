# Plan 12 — UI polish + sync transfer progress

> Authored retrospectively from an interactive design-review session: the user
> drove the app live and gave incremental UI feedback; each item was implemented,
> rebuilt, and re-checked in turn. Recorded here so the durable result has a
> home alongside the living spec.

| | |
|--|--|
| **Date** | 2026-06-22 |
| **Status** | `done` |
| **Spec** | [`spec/design/design.md` §Components (Branch bar, Repo tree rows, Menus, Progress over spinners) + §Principles](../spec/design/design.md), [`spec/engineering/engineering.md` §Network operations → Transfer progress](../spec/engineering/engineering.md) |
| **Depends on** | [Plan 11 — Fleet fetch-all](2026-06-22-plan11-fleet-fetch-all.md), per-repo sync (`AsyncRepo` fetch/pull/push), the GitHub-Desktop UI shell |

**Goal:** A round of branch-bar / sidebar polish, and — the one structural
change — replace the in-flight **busy spinner** for fetch/pull/push with a real
**determinate progress bar** fed by libgit2's transfer counts.

**Architecture:** The progress signal already existed at the libgit2 boundary
(`transfer_progress` for fetch; `push_transfer_progress` was unwired). It now
flows on a parallel channel beside the awaited result:
`GitRepo` ProgressCallback → `AsyncRepo` (worker thread) →
`RepoController::progressSink()` (marshals onto the controller thread, queued +
`QPointer`-guarded) → `syncProgressChanged` → `RepoViewModel` properties →
QML `ProgressBar`. Everything else in the round is QML-only.

**Tech stack:** libgit2 push/fetch callbacks; Qt `QMetaObject::invokeMethod`
(queued cross-thread); QML `ProgressBar`, `TreeViewDelegate`.

## Global constraints

- No Qt in `core/`; `ProgressCallback` stays pure `std::function<bool(unsigned,
  unsigned)>` (invariant: core speaks `std`).
- Object names that tests find stay stable (`fileList`, `addRepoMenu`,
  `changesTabBar`, `branchChip`, …); `aheadCount` / `behindCount` / `syncing`
  contracts unchanged. Full suite (102) stays green.

---

## Task 1: Wire push transfer progress (core)

**Files:** Modify `core/src/gitrepo.cpp`.

- [x] Add `pushTransferProgressTrampoline(current, total, bytes, payload)` mapping
      to the shared `ProgressCallback` (same received/total shape as fetch).
- [x] Set `opts.callbacks.push_transfer_progress` in `GitRepo::push` (was unset —
      push reported no progress at all).

## Task 2: Thread the callback through the async layer

**Files:** Modify `ui/include/gittide/ui/asyncrepo.hpp`, `ui/src/asyncrepo.cpp`,
`ui/src/projectcontroller.cpp`.

- [x] `AsyncRepo::fetch/pull/push` gain a `ProgressCallback onProgress` param,
      forwarded into the worker lambda (captured by value).
- [x] Fleet `ProjectController::fetchOne` passes a no-op callback — it surfaces
      per-row tree state, not byte progress.

## Task 3: Marshal counts to the GUI thread (controller → VM)

**Files:** Modify `ui/include/gittide/ui/repocontroller.hpp`,
`ui/src/repocontroller.cpp`, `ui/include/gittide/ui/repoviewmodel.hpp`,
`ui/src/repoviewmodel.cpp`.

- [x] `RepoController::progressSink()` returns a callback that posts counts to the
      controller thread (`invokeMethod`, `Qt::QueuedConnection`, `QPointer`
      guard) and emits `syncProgressChanged(received, total)`; passed at all three
      call sites.
- [x] `RepoViewModel` exposes `syncProgress` (fraction, `-1` ⇒ indeterminate),
      `syncReceived`, `syncTotal`; counts reset at each transfer's start and end.

## Task 4: Progress bar in the branch bar (QML)

**Files:** Modify `ui/qml/BranchBar.qml`.

- [x] Replace the busy-spinner + "Working…" row with a `ProgressBar` (determinate
      from `syncProgress`, animated indeterminate sweep until the first count) and
      a `received / total` caption, beside the sync buttons.

## Task 5: Branch-bar + sidebar visual polish (QML)

**Files:** Modify `ui/qml/BranchBar.qml`, `ui/qml/Sidebar.qml` (and earlier in the
session: `WorkingPane.qml`, `ChangesPane.qml`, new `AppCheckBox.qml` /
`AppMenu.qml` / `AppMenuItem.qml` — committed in `cddc1c2`).

- [x] Fixed-width branch chip; eliding name; equal-width Fetch/Pull/Push beside it;
      ahead/behind as inline `accent` pills (not corner badges).
- [x] Repo tree: collapsed by default, expand on repo click; themed chevron +
      `MouseArea` toggle (Qt's built-in indicator tap was unreliable); drop the
      repo/submodule type glyphs; theme the fetch-all icon.

## Task 6: Fix stale Push badge after commit

**Files:** Modify `ui/src/repocontroller.cpp`.

- [x] `commit` / `commitSelection` now `co_await refreshSyncStatus()` so the ahead
      count (and the Push pill) updates after committing — they refreshed status +
      history but not sync.

---

## Outcome

Done 2026-06-22. Full suite green (102/102).

- **Shipped:** determinate transfer-progress bar for fetch/pull/push (push
  reported nothing before); fixed-width branch chip; equal-width sync buttons with
  inline ahead/behind pills; repo tree collapsed-by-default with a themed chevron
  and no type glyphs; themed checkboxes/menus (`AppCheckBox` / `AppMenu` /
  `AppMenuItem`); compact Changes/History tabs; Push ahead-count refresh after
  commit.
- **Code:** `pushTransferProgressTrampoline` (core); `ProgressCallback` param on
  `AsyncRepo::fetch/pull/push`; `RepoController::progressSink` +
  `syncProgressChanged`; `RepoViewModel::syncProgress` / `syncReceived` /
  `syncTotal`; `BranchBar.qml` progress bar + `SyncButton`; `Sidebar.qml` chevron
  toggle.
- **Spec:** `spec/design/design.md` — Branch bar, Repo tree rows, new Menus and
  **Progress over spinners** component bullets, and the Principles directive
  *prefer determinate progress over a busy spinner*; `spec/engineering/engineering.md`
  §Network operations → Transfer progress.
- **Commits:** `cddc1c2` (earlier polish round: themed checkboxes/menus, compact
  tabs, branch-bar layout) · `c599933` (this plan: sync progress, tree collapse,
  commit-sync fix, spec).
- **Wishlist:** `network-sync.md` updated — transfer progress now shipped; fleet
  pull-all and OS-keychain credentials still deferred.
