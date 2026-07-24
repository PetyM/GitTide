# Plan 47 — Rebase: fewer forced prompts (squash without a message pause · reorder without a confirm · Undo toast)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [x]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-24 |
| **Status** | `done` |
| **Spec** | `spec/product/rebase-interactive.md` (squash message pause, reorder confirm, message-pause surface); `spec/product/history-editing.md` |
| **Depends on** | Plan 20 (interactive engine), Plan 22/23 (history-editing UX, drag), Plan 28 (multi-select squash) |

**Goal:** Stop making the user confirm or edit when there is realistically only
one thing to do. A **squash** applies with the concatenated default message
instead of forcing the combined-message editor; a **drag-to-reorder** applies
immediately instead of popping a confirmation modal. Both are backed by a
**non-blocking Undo toast** (act-then-offer-undo, extending D40's philosophy)
rather than an up-front gate.

**Architecture:**

- **Core:** `driveInteractive` stops pausing on `Squash` — it commits with the
  accumulated combined message (HEAD's message + the squashed commit's message),
  exactly the prefill it used to hand the editor. `RebasePause::Message` now
  belongs to **`Reword` only**. A new soft-reset verb
  `undoHistoryEdit(preTipOid)` moves the current branch ref back to the
  pre-operation tip (index/worktree untouched — safe because a drop-free replay
  yields a content-identical tree).
- **UI:** `startInteractiveRebase` captures the pre-op tip and, on a clean
  finish of a **drop-free** plan carrying an undo label, emits
  `historyEditUndoable(preTip, label)`. `Main.qml` shows an Undo toast wired to
  `undoHistoryEdit`. The reorder path drops `ReorderConfirmDialog` and calls
  `reorderCommits` directly.

**Tech stack:** libgit2 (`git_reset` SOFT, `git_commit_amend`), QCoro tasks, Qt
Quick/QML overlay toast, Catch2 + headless QML runner.

## Global constraints

- No Qt in `core/`; errors are `Expected<T>`; mutual exclusion (D33) preserved on
  the new undo verb. See [engineering invariants](../spec/engineering/engineering.md).
- Undo is **only** offered when the replay dropped no commit (a drop changes the
  final tree, so a soft reset would misrepresent it). Drop stays behind the
  explicit todo editor with no undo toast.
- Keep passing: interactive-rebase core tests (the squash-pauses test is
  **rewritten**, not deleted), `reorderCommits`/`squashCommitInto` object-name
  contracts, `rebaseMessagePauseEntered` (reword still auto-opens `RewordDialog`).
- New tests → matching lists in `tests/CMakeLists.txt`.

---

## Task 1: Squash commits without a message pause (core)

**Files:** Modify `core/src/gitrepo.cpp` (`driveInteractive`, `rebaseState`);
Test `tests/test_git_repo_interactive_rebase.cpp`.

**Interfaces:** unchanged signatures. Behaviour change: `Squash` no longer yields
`RebaseOutcome{pause: Message}`; it commits with
`headMessage + "\n\n" + squashedOriginalMessage`. `continueRebase(message)` with
an explicit message for a squash step is still honoured (forward-compatible) but
the auto path supplies the concatenated default.

- [x] **Step 1: Rewrite the failing test** — replace *"interactive squash folds a
  commit into the previous, pausing for message"* with *"interactive squash folds
  without a message pause, keeping the combined message"*: `pick A, squash B` run
  with **no** message finishes `pause == None`, one commit, tree = A∪B, message
  contains both A's and B's text. Add a chain case `pick A, squash B, squash C` →
  message contains A, B and C. (Fixup/reword tests unchanged.)
- [x] **Step 2: Make it pass** — in `driveInteractive`, drop `Squash` from the
  "needs a message before commit" guard (leave `Reword`). In the `Squash` commit
  branch, when `message` has no value compute
  `git_commit_message(head) + "\n\n" + git_commit_message(orig)`; otherwise use the
  supplied message. In `rebaseState`, set `pause = Message` only for `Reword`
  (keep the reword prefill; delete the squash prefill branch).
- [x] **Step 3: Verify** — run `[rebase-i]`; confirm reword still pauses and fixup
  still keeps only the first message.

## Task 2: `undoHistoryEdit` soft-reset verb (core)

**Files:** Modify `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`;
Test `tests/test_git_repo_interactive_rebase.cpp`.

**Interface:**
```cpp
/// Move the current branch ref back to @p preTipOid (soft: index + worktree
/// untouched), undoing a just-finished drop-free history edit whose replay left a
/// content-identical tree. Guards: no other op in progress (D33), a real branch
/// (not detached/unborn), @p preTipOid resolvable.
Expected<void> undoHistoryEdit(std::string preTipOid);
```

- [x] **Step 1: Failing test** — after a clean `reorder`/`squash` finish, capture
  the new tip, call `undoHistoryEdit(preTip)`, assert HEAD == preTip and the
  worktree is clean; assert guards (detached, mid-rebase, bad oid) each error.
- [x] **Step 2: Implement** — mirror `undoLastCommit` guards; `git_reset` with
  `GIT_RESET_SOFT` onto the looked-up `preTipOid` commit.
- [x] **Step 3: Verify** — run `[rebase-i]`.

## Task 3: Async + ViewModel plumbing (ui)

**Files:** Modify `ui/include/gittide/ui/asyncrepo.hpp` + `ui/src/asyncrepo.cpp`;
`ui/include/gittide/ui/repocontroller.hpp` + `ui/src/repocontroller.cpp`;
`ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp`;
Test `tests/ui/test_repocontroller_interactive_rebase.cpp`,
`tests/ui/test_repocontroller_squash.cpp`, `tests/ui/test_repoviewmodel_rebase.cpp`.

**Interfaces:**
- `AsyncRepo::undoHistoryEdit(QString oid) -> Task<Expected<void>>`.
- `RepoController::startInteractiveRebase(base, actions, oids, QString undoLabel = {})`
  — captures `preTip = head()` before start; on a clean finish, if
  `!undoLabel.isEmpty()` **and** the plan has no `drop`, emit
  `historyEditUndoable(preTip, undoLabel)`.
- `RepoController::undoHistoryEdit(QString oid) -> Task<void>` → core verb then
  `refreshAfterRebase`.
- `RepoViewModel`: `Q_INVOKABLE void undoHistoryEdit(QString oid)`; signal
  `historyEditUndoable(QString preTipOid, QString label)` forwarded from the
  controller; `reorderCommits`/`squashCommitInto` pass a label
  ("Commits reordered" / "Commit squashed"); `buildSquashTodo` passes
  "Commits squashed"; the todo-dialog start passes "History updated".

- [x] **Step 1: Failing tests** — controller emits `historyEditUndoable` on a
  clean drop-free reorder/squash but **not** when the plan contains a drop;
  `undoHistoryEdit` restores the pre-op tip; VM re-emits the signal and its
  `undoHistoryEdit` reaches the controller.
- [x] **Step 2: Implement** the plumbing above.
- [x] **Step 3: Verify** — run the affected `ui` test targets.

## Task 4: Drop the reorder confirm; add the Undo toast (qml)

**Files:** Modify `ui/qml/HistoryPane.qml`, `ui/qml/Main.qml`; remove
`ui/qml/ReorderConfirmDialog.qml` + its `ui/CMakeLists.txt` registration;
Test `tests/ui/test_qml_history.cpp`.

- [x] **Step 1: Failing test** — a top/bottom-band drop calls `reorderCommits`
  directly (no `reorderConfirmDialog` in the tree); after a clean history edit an
  `undoToast` (objectName) is visible with an `undoToastButton` that invokes
  `undoHistoryEdit`.
- [x] **Step 2: Implement** — `dropLogic.performDrop`'s reorder branch calls
  `repoVm.reorderCommits(fromIndex, toIndex, zone)`; delete the
  `ReorderConfirmDialog` instance/import. Add an `undoToast` in `Main.qml`
  (modelled on `errorBanner`: overlay strip, ~6 s timer, Label + `AppButton`
  "Undo") opened by `Connections { onHistoryEditUndoable }`, storing the preTip;
  Undo → `repoVm.undoHistoryEdit(storedOid)`.
- [x] **Step 3: Verify** — run the QML history/rebase test targets.

## Task 5: Docs — spec + decision

**Files:** `docs/spec/product/rebase-interactive.md`,
`docs/spec/product/history-editing.md`, `docs/decisions.md`, wish/index updates.

- [x] Update rebase-interactive §2.5/§3.2/§3.4/§3.5: squash commits with the
  concatenated default (no Message pause); reorder applies without a modal; the
  Undo toast is the safety net; message pause now = reword only.
- [x] New **D41** — *history edits act-then-offer-undo, not confirm-first*:
  squash uses the concatenated default message; reorder drops the confirm modal;
  a drop-free clean replay is reversible via a soft-reset Undo toast; drop keeps
  its editor-only path with no toast (its replay is not content-identical).
- [x] Tick this plan's boxes, fill **Outcome**; if this closes the Rebase wish,
  flip its status and move it to Shipped in `wishlist/index.md`.

---

## Outcome

- **Shipped:** Two forced prompts in the interactive-rebase UX are gone. A **squash**
  (drag-to-squash, multi-select "Squash N commits", and the todo editor) now commits
  with the concatenated default message and finishes in one run — no message pause;
  reword keeps its auto-opened editor. **Drag-to-reorder** applies immediately with no
  confirm modal. Both are backed by a non-blocking **Undo toast** whose Undo soft-resets
  the branch to the pre-edit tip; the toast is offered only for **drop-free** edits
  (a drop's replay is not content-identical). Full suite green (215/215).
- **Spec updated:** [rebase-interactive.md](../spec/product/rebase-interactive.md)
  (§2.5 squash semantics, pause = reword only; §3.2 reorder/squash gestures; §3.3
  VM signals; §3.4/§3.5 banner + message surface; new §3.6 Undo toast; §6.1 Plan 47
  delta); [history-editing.md](../spec/product/history-editing.md) §8; new **D41** in
  [decisions.md](../decisions.md) (supersedes the reorder-confirm part of D36 and the
  squash-message part of D38/D40).
- **Code:** core `GitRepo::driveInteractive`/`rebaseState` (squash no longer pauses) +
  new `GitRepo::undoHistoryEdit` (soft reset); `AsyncRepo`/`RepoController`/
  `RepoViewModel` gain `undoHistoryEdit` and the `historyEditUndoable(preTip, label)`
  signal (`startInteractiveRebase` grew an `undoLabel` + pre-tip capture);
  `HistoryPane.qml` reorder → `reorderCommits` directly; `Main.qml` `undoToast`;
  `ui/qml/ReorderConfirmDialog.qml` deleted (dropped from `qml.qrc`).
