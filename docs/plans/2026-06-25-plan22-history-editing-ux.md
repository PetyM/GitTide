# Plan 22 — History-editing UX: multi-select squash, drag reorder, undo last commit

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-06-25 |
| **Status** | `done` |
| **Spec** | [`spec/product/rebase-interactive.md`](../spec/product/rebase-interactive.md) §3.2, [`spec/product/history-editing.md`](../spec/product/history-editing.md) §8, [`spec/design/design.md`](../spec/design/design.md) (drag grip) |
| **Depends on** | [Plan 20 — interactive rebase engine](2026-06-24-plan20-interactive-rebase.md) |

**Goal:** Three UX affordances over the existing interactive-rebase engine —
multi-select **Squash N commits** from history, **drag-and-drop reorder** (in the
todo editor *and* directly in the history view), and **Undo last commit** (soft
reset, keep changes staged).

**Architecture:** No new rebase mechanics — every reorder/squash is expressed as a
`RebaseTodo` fed to the existing `startInteractiveRebase`. Squash and direct-history
reorder are new *front-ends* that build that plan; the only new git verb is
`GitRepo::undoLastCommit()` (soft reset to first parent). Decisions: **D36**
(two reorder gestures over one engine; direct-history drag gated to the linear
single-parent run + confirmation) and **D37** (undo = soft reset, mutual-exclusion
guarded).

**Tech stack:** C++23 + libgit2 (`git_reset` SOFT) in `core/`; Qt Quick/QML +
QCoro + `AsyncRepo` in `ui/`; Catch2 (core) / QtTest (ui), test-first.

## Global constraints

- No Qt in `core/`; errors as `Expected<T>`; paths via `generic_u8string()`
  ([engineering invariants](../spec/engineering/engineering.md)).
- Never signal state by colour alone (D19) — the drag grip is an icon affordance.
- New tests → the matching list in `tests/CMakeLists.txt` **and** the include +
  `RUN(...)` in `tests/ui/main.cpp` for UI tests.
- Keep the interactive-rebase suite and the QML dialog-load tests green.

---

## Task 1: Core `undoLastCommit()` (soft reset)

**Files:** `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`,
`tests/test_git_repo_undo.cpp` (+ `tests/CMakeLists.txt`).

**Interfaces:** `Expected<void> GitRepo::undoLastCommit();`

- [x] **Step 1: Failing tests** — HEAD moves to parent + change stays staged; errors
      on root / unborn / detached.
- [x] **Step 2: Implement** — guard `git_repository_state != NONE ||
      interactiveRebaseInProgress()`; resolve HEAD's first parent;
      `git_reset(parent, GIT_RESET_SOFT)`.
- [x] **Step 3: Verify** — `[undo]` tests + full core suite green.

## Task 2: UI plumbing for undo + entry points

**Files:** `asyncrepo.{hpp,cpp}`, `repocontroller.{hpp,cpp}`,
`repoviewmodel.{hpp,cpp}`, `CommitContextMenu.qml`, `HistoryPane.qml`,
`TitleBar.qml`, `Main.qml`, `tests/ui/test_repocontroller_undo.cpp`.

**Interfaces:** `AsyncRepo::undoLastCommit()`, `RepoController::undoLastCommit()`
(refresh status/history/branches), `RepoViewModel::undoLastCommit()` (Q_INVOKABLE).

- [x] **Step 1: Failing test** — controller undo moves HEAD to parent, b.txt staged.
- [x] **Step 2: Implement** the three-layer wiring; HEAD-only context-menu item +
      app-menu item (disabled mid-merge/-rebase).
- [x] **Step 3: Verify** — UI suite green.

## Task 3: Multi-select squash from history

**Files:** `repocontroller.{hpp,cpp}` (`buildSquashTodo`), `repoviewmodel.{hpp,cpp}`
(`requestSquashTodo`), `CommitContextMenu.qml`, `HistoryPane.qml`,
`RebaseTodoDialog.qml` (seed honours per-entry `action`),
`tests/ui/test_repocontroller_squash.cpp`, `tests/ui/test_qml_rebase_todo.cpp`.

- [x] **Step 1: Failing tests** — contiguous selection → base + pick/squash entries;
      non-contiguous rejected; dialog seeded-with-actions is valid and round-trips.
- [x] **Step 2: Implement** — contiguity check via history walk; reuse
      `rebaseTodoReady`; right-click keeps an active multi-selection.
- [x] **Step 3: Verify**.

## Task 4: Drag reorder in `RebaseTodoDialog`

**Files:** `RebaseTodoDialog.qml`.

- [x] **Step 1** — add a `⠿` grip + `DragHandler`/`DropArea` lift that reorders live
      via the existing `moveRow`; keep the ↑/↓ buttons.
- [x] **Step 2: Verify** — the dialog-load QML tests stay green (catch QML errors).

## Task 5: Drag reorder directly in the history view

**Files:** `repoviewmodel.{hpp,cpp}` (`reorderableRunLength` property +
`reorderCommits(fromRow,toRow)`), `HistoryPane.qml` (per-row grip + drag),
`ReorderConfirmDialog.qml` (new, registered in `ui/qml/qml.qrc`),
`tests/ui/test_repoviewmodel_rebase.cpp`.

**Interfaces:** `reorderableRunLength` = contiguous single-parent run from HEAD;
`reorderCommits` replays the run reordered (all `pick`, base = parent of the run's
oldest) via `startInteractiveRebase`.

- [x] **Step 1: Failing tests** — run length counts the linear run; `reorderCommits`
      rewrites history order.
- [x] **Step 2: Implement** — grip on run rows; drop computes target via
      `ListView.indexAt`; confirmation dialog → `reorderCommits`.
- [x] **Step 3: Verify**.

## Task 6: Docs

- [x] Spec: `rebase-interactive.md` §3.2 (drag both places, squash-from-history,
      direct-history reorder), `history-editing.md` §8, `design.md` (drag grip).
- [x] Decisions: **D36** (reorder gestures), **D37** (undo soft reset).
- [x] Wishlist: note Plan 22 in `history-editing.md` and `rebase.md`; this plan in
      the index.

---

## Outcome

- **Shipped:** Undo last commit (`git reset --soft HEAD~1`, changes kept staged);
  multi-select **Squash N commits…** seeding the todo editor; drag-to-reorder in the
  todo editor (grip alongside ↑/↓) and directly in the history view (gated to the
  linear single-parent run from HEAD, behind a confirmation). All reorder/squash
  routes through the existing interactive engine; the only new git verb is the soft
  reset.
- **Spec updated:** [`rebase-interactive.md`](../spec/product/rebase-interactive.md)
  §3.2 (drag + squash-from-history + direct-history reorder),
  [`history-editing.md`](../spec/product/history-editing.md) §8,
  [`design.md`](../spec/design/design.md) (drag-grip affordance), and
  [`decisions.md`](../decisions.md) **D36 / D37**.
- **Code:** core `GitRepo::undoLastCommit()` (`core/src/gitrepo.cpp`); UI
  `AsyncRepo`/`RepoController`/`RepoViewModel` `undoLastCommit`,
  `RepoController::buildSquashTodo`, `RepoViewModel::requestSquashTodo` +
  `reorderableRunLength`/`reorderCommits`; QML `ReorderConfirmDialog.qml`, drag
  grips in `RebaseTodoDialog.qml` and `HistoryPane.qml`, undo entries in
  `CommitContextMenu.qml`/`TitleBar.qml`. Tests: `test_git_repo_undo.cpp`,
  `test_repocontroller_undo.cpp`, `test_repocontroller_squash.cpp`,
  reorder cases in `test_repoviewmodel_rebase.cpp`, seed-with-actions in
  `test_qml_rebase_todo.cpp`.
