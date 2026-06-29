# Plan 28 — Multi-select squash skips the todo editor

> **For agentic workers:** implement this plan task-by-task, test-first.

| | |
|--|--|
| **Date** | 2026-06-29 |
| **Status** | `done` |
| **Spec** | [`spec/product/rebase-interactive.md`](../spec/product/rebase-interactive.md) §3.2 · [`spec/product/context-menus.md`](../spec/product/context-menus.md) · [`spec/product/history-editing.md`](../spec/product/history-editing.md) §8 |
| **Depends on** | Plan 22 (history-editing UX), Plan 26 (squash/drag UX) |

**Goal:** Squashing several commits at once from the history context menu no
longer opens the interactive-rebase todo editor — it starts the squash directly
and jumps straight to the combined-message edit. A plain multi-commit squash has
no plan to edit, so the editor was an empty extra step.

**Architecture:** Pure `ui/` change. `RepoController::buildSquashTodo` already
validated the contiguous range and built the oldest-`pick`/rest-`squash` plan;
it now calls `startInteractiveRebase(base, actions, oids)` directly instead of
emitting `rebaseTodoReady` (which auto-opened `RebaseTodoDialog`). This mirrors
the existing drag-to-squash path (`RepoViewModel::squashCommitInto`). The todo
editor stays reserved for the explicit **“Edit history from here…”** reorder path
(`buildRebaseTodo` → `rebaseTodoReady`).

**Tech stack:** Qt Quick / QCoro, the manual interactive-rebase engine in `core/`.

## Global constraints

- No new behaviour in `core/`; the engine's `RebasePause::Message` pause already
  drives the combined-message edit.
- `rebaseTodoReady` / `RebaseTodoDialog` must keep working for the reorder path.

---

## Task 1: Start the squash directly

**Files:** Modify `ui/src/repocontroller.cpp` (`buildSquashTodo`);
Doxygen on `ui/include/gittide/ui/repocontroller.hpp`,
`ui/include/gittide/ui/repoviewmodel.hpp`. Test
`tests/ui/test_repocontroller_squash.cpp`.

- [x] **Step 1: Failing test** — `contiguous_selection_starts_rebase_paused_on_message`
  drives `buildSquashTodo` to completion and asserts (a) `rebaseTodoReady` is
  never emitted, (b) a `rebaseStateChanged` arrives with
  `interactive && pause == Message` and a non-empty `messagePrefill`. Failed
  while the controller still emitted `rebaseTodoReady`.
- [x] **Step 2: Make it pass** — replace the `entries` build + `emit
  rebaseTodoReady` tail with a `planOids`/`actions` build and
  `co_await startInteractiveRebase(base, actions, planOids)`.
- [x] **Step 3: Verify** — full suite green (161/161). Updated the stale
  "as buildSquashTodo emits" comment in `test_qml_rebase_todo.cpp`.

---

## Outcome

- Shipped: multi-select **“Squash N commits…”** runs the squash without the todo
  editor and lands on the combined-message `RewordDialog`. The editor is now only
  for explicit **“Edit history from here…”** reorders.
- Spec updated: `rebase-interactive.md` §3.2 (Squash from history),
  `context-menus.md` (Squash row), `history-editing.md` §8.
- Code: `RepoController::buildSquashTodo` (`ui/src/repocontroller.cpp`) now calls
  `startInteractiveRebase` directly; Doxygen on `buildSquashTodo` /
  `requestSquashTodo` updated. Regression test
  `tests/ui/test_repocontroller_squash.cpp::contiguous_selection_starts_rebase_paused_on_message`.
