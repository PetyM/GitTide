# Rebase — interactive engine (Tier 2)

| | |
|--|--|
| **Designed** | 2026-06-24 |
| **Status** | `shipped` |
| **Wishlist** | [rebase.md](../../wishlist/rebase.md) · [history-editing.md](../../wishlist/history-editing.md) |
| **Builds on** | [rebase.md (Tier 1, plain driver)](rebase.md) · [history-editing.md (reword-tip, multi-select)](history-editing.md) |
| **Touches** | core: manual cherry-pick engine + extended `RebaseState` + interactive verbs on `GitRepo`; ui: todo-list editor dialog, message-pause surface, banner extension, ViewModel/Controller wiring, commit-menu entry; design: todo-editor layout |

## Overview

The **interactive rebase engine** both wishes deferred from Tier 1: a todo-list
editor over a contiguous span of commits, supporting **reorder · drop · squash ·
fixup · reword** in one iteration. It subsumes the deferred trio from
[history-editing](history-editing.md) (reword-older, squash, reorder) — GitTide
ships **one** todo-list editor, owned here, never duplicated.

Tier 1 ([rebase.md](rebase.md)) shipped the plain driver: replay `upstream..HEAD`
in original order via `git_rebase_*`, pausing on conflict, with a banner and
continue/skip/abort. Tier 2 reuses that **surface** (banner, conflict resolution,
auto-stash, mutual-exclusion-with-merge, the disk-truth state rule) but replaces
the **engine**: libgit2 cannot drive a user-edited todo, so the core runs a manual
cherry-pick loop over an explicit operation plan.

## Scope

**In:**

- Entry from the **commit context menu**: *Edit history from here…* — the clicked
  commit is the **oldest editable entry**; the engine detaches at its **first
  parent** and the todo spans the clicked commit `..HEAD` (oldest first, git order).
- A **todo-list editor** dialog: per-commit action (pick / reword / squash / fixup
  / drop) + reorder.
- Manual cherry-pick engine driving the plan, with **two pause reasons**:
  - **conflict** — reuses Tier 1's conflict UI (`acceptConflict`, banner verbs);
  - **message** — reword/squash pause for a new/combined message (git-CLI style).
- **step k/n progress** in the banner for every pause; live updates per step.
- **Abort always reachable** at every pause — restores the exact pre-rebase branch
  tip and worktree, pops the auto-stash.

**Out (deferred):**

- `exec` todo entries (run a shell command between steps).
- `break`/`edit` (pause to amend a commit's *content*) — only message edits and
  structural ops here; content-amend mid-rebase is a later wish.
- Autosquash (`fixup!`/`squash!` commit-message magic).
- Rebase **onto a different branch** while interactively editing (`--onto`); Tier 2
  keeps the same branch, same base-parent — pure history tidy-up.
- Already-pushed-commit warning — still deferred with
  [network-sync](../../wishlist/network-sync.md).

---

## 1. Why a manual engine (new decision — D34)

libgit2's `git_rebase_init` **generates** the operation list itself from
branch/upstream/onto and only ever produces `PICK` operations in original order.
There is no public API to inject a reordered / squashed / dropped todo, nor to
emit `REWORD`/`SQUASH` operations. The Tier 1 `git_rebase_next`/`_commit`/`_finish`
loop therefore cannot express an interactive plan.

The engine is built **manually** instead: detach at the base, `git_cherrypick`
each kept commit in the chosen order, combine for squash/fixup, amend the message
for reword, and skip for drop. This is logged as **D34** (manual cherry-pick
engine + own on-disk state dir; mid-rebase message pause is the chosen UX over
up-front message collection). *Rejected:* (a) faking a libgit2 todo by re-init'ing
per step — no API; (b) shelling out to `git rebase -i` — violates the
"never build git command strings, use libgit2" invariant and loses structured
conflict state.

---

## 2. Core (`core/`, no Qt)

### 2.1 The operation plan

A plain `std` value the UI builds and core consumes:

```cpp
enum class RebaseAction { Pick, Reword, Squash, Fixup, Drop };

struct RebaseTodoEntry
{
    RebaseAction action = RebaseAction::Pick;
    std::string  oid;        ///< 40-char hex of the original commit
};

/// An interactive plan: replay these entries (in list order) on top of `base`.
struct RebaseTodo
{
    std::string                  base;     ///< oid to detach onto (parent-of-oldest)
    std::vector<RebaseTodoEntry> entries;  ///< oldest first (git todo order)
};
```

`base` is the **clicked commit's first parent** (the span is `base..HEAD`
inclusive of the clicked commit). The UI fills `entries` from the history between
`base` and `HEAD`, then lets the user reorder / relabel before starting.

### 2.2 Extended `RebaseState` (`rebase.hpp`)

Tier 1's struct gains interactive fields; the plain-driver fields are unchanged so
the existing banner keeps working:

```cpp
enum class RebasePause { None, Conflict, Message };

struct RebaseState
{
    bool        inProgress = false;
    bool        interactive = false;   ///< true => driven by the manual engine
    RebasePause pause = RebasePause::None;
    std::string ontoRef;               ///< Tier 1: target branch shorthand
    int         current = 0;           ///< current step, 1-based (non-drop count)
    int         total   = 0;           ///< total non-drop steps
    std::string stepSummary;           ///< summary of the commit being applied
    std::string messagePrefill;        ///< pause==Message: prefilled editor text
    std::vector<std::filesystem::path> conflictedPaths;
    std::vector<std::filesystem::path> conflictedSubmodules;
};
```

`rebaseState()` (D30 — disk-truth, derived every call) now checks **both**:
libgit2's `git_repository_state()` (a Tier 1 / CLI plain rebase) **and** GitTide's
own state dir (an interactive rebase). For an interactive rebase, `current`/`total`
and `stepSummary` come from the on-disk todo + cursor, not from `git_rebase_*`.

### 2.3 On-disk state — `.git/gittide-rebase/`

The manual engine is **not** a libgit2 rebase, so it owns a small state directory
under the repo's git dir (created at start, removed at finish/abort):

- `base` — the oid to detach onto (parent-of-oldest).
- `todo` — the full plan (action + oid per line, oldest first).
- `done` — the cursor: how many entries are applied (advances per committed step).
- `applied` — a marker file: present when the current entry has been cherry-picked
  but not yet committed (distinguishes a conflict/message pause from "not started").
- `orig-head` — the pre-rebase branch tip oid (for abort restore).
- `branch` — the branch name being rewritten (to move at finish).

There is **no** persisted message buffer: a squash's combined-message prefill is
computed on the fly from the current `HEAD`'s message plus the entry's original
message, so a squash chain accumulates naturally through `HEAD` (§2.5).

Disk is the single source of truth (D30): a paused interactive rebase survives an
app restart and `rebaseState()` reconstructs the banner from these files. The
format is GitTide-private (not git's `rebase-merge/` layout); `git rebase
--continue` from a terminal will **not** drive it — acceptable, because Abort is
always reachable and the engine is local-only.

### 2.4 Verbs on `GitRepo` (all `Expected<T>`)

```cpp
/// Begin an interactive rebase of the current branch per `todo` (base + ordered
/// entries). Auto-stash is the controller's job (D31). Detaches at base, writes
/// the state dir, then drives until the first pause (conflict or message) or a
/// clean finish. Returns the outcome.
Expected<RebaseOutcome> startInteractiveRebase(RebaseTodo todo);

/// Resume after a pause. If the current step is a Message pause, `message` MUST be
/// supplied (the new reword text / combined squash text) — it is committed, then
/// the loop advances. If a Conflict pause, the resolved index is committed (or
/// amended, for squash/fixup) and the loop advances. Drives to the next pause or
/// finish. Errors if no rebase in progress, or unresolved conflicts remain, or a
/// Message pause is continued without a message.
Expected<RebaseOutcome> continueRebase(std::optional<std::string> message = std::nullopt);

/// Skip the current step (drop the commit being applied) and advance.
Expected<RebaseOutcome> skipRebase();

/// Abort: restore orig-head + worktree exactly, remove the state dir.
Expected<void> abortRebase();
```

`continueRebase`, `skipRebase`, `abortRebase` from Tier 1 are **extended** to
detect which engine is live (libgit2 plain vs. our state dir) and dispatch
accordingly — one verb set, two engines. `RebaseOutcome` carries
`{ bool conflicted; RebasePause pause; }` so the controller branches without a
second disk read.

### 2.5 Engine semantics

The driver loop (shared by start / continue / skip) walks `todo.entries` from the
`done` cursor:

- **Pick** — `git_cherrypick` onto HEAD. On conflict → pause (Conflict). Else
  commit reusing the original author + message; advance the cursor.
- **Reword** — cherry-pick (may conflict → pause Conflict). Once applied cleanly,
  pause (Message) with `messagePrefill` = the commit's original message. On
  continue-with-message, commit using that message; advance.
- **Squash** — cherry-pick onto HEAD (the previous step's commit). On conflict →
  pause Conflict. Once applied, **append** this commit's message to the `message`
  buffer and pause (Message) with `messagePrefill` = the combined buffer. On
  continue-with-message: `git_commit_amend` the previous commit with the combined
  tree + supplied message (one commit replacing previous+this); advance.
- **Fixup** — like Squash but **no Message pause**: amend the previous commit with
  the combined tree, keep the previous commit's message; advance.
- **Drop** — advance the cursor, apply nothing.

A step can both conflict **and** need a message (a squash that conflicts): the
Conflict pause comes first (resolve + continue), then the Message pause, then the
commit. `continueRebase` is re-entrant and re-derives the current pause from disk.

- **First-entry guard.** `entries[0]` may not be `Squash`/`Fixup` (nothing above to
  combine into) — validated in core (`Expected` error) **and** in the editor UI.
- **All-drop guard.** A plan that drops everything errors (would orphan the branch).
- **Empty / already-applied** cherry-pick (`GIT_EAPPLIED` / empty diff) → implicit
  skip, no error (mirrors Tier 1).
- **Submodules.** Per-step cherry-pick must preserve gitlink pointers exactly
  unless the picked commit itself changed them (the history-editing wish's
  GitHub-Desktop failure mode). Gitlink conflicts surface in `conflictedSubmodules`
  and reuse the merge engine's handling.
- **Refresh cascade.** Every committed step rewrites HEAD/tree/index → the
  controller refreshes status + history + branches per pause, like Tier 1.

### 2.6 Guards (`Expected` left)

Unborn `HEAD`; detached `HEAD`; `base` not an ancestor of `HEAD`; the clicked
commit is a **root commit** (no first parent to detach onto — disallowed in the
first cut; revisit if rewriting the initial commit is wanted); a rebase already in
progress; a **merge** in progress (mutual exclusion, D33); the first *kept*
(non-drop) entry is squash/fixup (no prior in-range commit to fold into — leading
drops do not make squash legal); all-drop plan.

### 2.7 Tests (`TempRepo`, Catch2, test-first)

- **Reorder.** Two non-conflicting commits, swap their order → final history has
  them in the new order, both trees applied.
- **Drop.** Drop the middle of three commits → its change is absent; the other two
  replay; HEAD parent chain is linear onto base.
- **Squash.** `pick A` + `squash B` → one commit; tree = A∪B; message =
  the supplied combined text; descendant count drops by one.
- **Fixup.** `pick A` + `fixup B` → one commit; tree = A∪B; message = A's only;
  **no** Message pause (continue without a message succeeds).
- **Reword (older).** Reword a non-tip commit → its message changes, its tree and
  the descendants' trees are preserved (only ids change).
- **Conflict mid-plan.** A reorder/squash that conflicts pauses with
  `pause == Conflict` and the path listed; resolve + `continueRebase()` advances.
- **Squash that conflicts** pauses Conflict, then (after resolve+continue) Message.
- **Abort** restores the exact `orig-head` oid and a clean worktree; state dir gone.
- **Progress.** `current`/`total` reflect non-drop steps and advance per pause.
- **Submodule preserved.** A plan editing an unrelated file leaves a submodule
  pointer entry untouched.
- **Guards:** first-entry squash, all-drop, base-not-ancestor, merge-in-progress,
  detached, unborn each error.

---

## 3. UI — todo editor, panes, banner

### 3.1 Entry point

`CommitContextMenu.qml` — new item **"Edit history from here…"**. Enabled when the
commit is on the current branch, reachable from `HEAD` (it can be the tip — a
one-entry plan is valid: reword/drop the tip). The clicked commit is the oldest
editable entry; the engine detaches at its first parent (§2.1). Emits
`editHistory(oid)`; `HistoryPane.qml` opens the todo dialog seeded from that oid.

This sits alongside the existing **"Reword…"** item (history-editing round): reword
of the *tip* keeps its cheap `rewordHead` path; *Edit history from here…* is the
general engine for everything else.

### 3.2 `RebaseTodoDialog.qml` (new)

An `OverlayCard` following the existing dialog pattern. The ViewModel exposes the
seeded plan as a list model (`base..HEAD`, **oldest at top** per git convention);
each row shows:

- a **drag grip** (⠿) to reorder the row by dragging,
- an **action dropdown**: Pick · Reword · Squash · Fixup · Drop,
- **up / down reorder buttons** (move the row within the list).

Reorder offers both **drag-and-drop** (drag the grip; the list reorders live via the
same `moveRow` verb) **and** the ↑/↓ buttons. The buttons are retained for keyboard
reachability; drag is the discoverable default (D36). A dropped row renders
struck-through / dimmed (paired with the action label, never colour alone — D19).
Footer: **Start rebase** / **Cancel**. *Start* is disabled while the plan is invalid
(first kept entry squash/fixup, or all-drop) with an inline hint. *Start* →
`repoVm.startInteractiveRebase(base, actions, oids)`.

#### Squash from history (multi-select)

Selecting a contiguous range of commits in the history list and choosing **“Squash
N commits…”** seeds this same dialog with the oldest selected commit as `pick` and
the rest as `squash` (oldest-first), `base` = parent of the oldest selected. The
controller's `buildSquashTodo(oids)` validates the selection is a contiguous,
non-merge run (else `operationFailed`) and emits the usual `rebaseTodoReady`; the
seed honours a per-entry default `action`. The user then confirms / edits the
combined message through the normal message-pause flow.

#### Reorder directly in the history view

Each history row in the **reorderable run** — the contiguous single-parent
(non-merge) span from HEAD, exposed as `RepoViewModel::reorderableRunLength` — shows
a drag grip. Dragging a row onto another run row opens a **confirmation**
(`ReorderConfirmDialog`), then `reorderCommits(fromRow, toRow)` replays the run in
the new order through the interactive engine (all `pick`s on the run's fixed base).
Merges and the root are not draggable, and the already-pushed warning is deferred to
network-sync (D36).

### 3.3 ViewModel state

`RebaseState` mirrored onto `RepoViewModel` (one `rebaseStateChanged` NOTIFY,
extending Tier 1): the existing `rebaseInProgress`, `rebaseOnto`, `rebaseStep`,
`rebaseTotal`, `rebaseStepSummary`, `rebaseConflictedCount`,
`rebaseHasSubmoduleConflicts`, plus **`rebaseInteractive`**, **`rebasePauseReason`**
(`"none"|"conflict"|"message"`), **`rebaseMessagePrefill`**.

Seeding the editor: `Q_INVOKABLE void requestRebaseTodo(QString fromOid)` asks the
controller to build the `base..HEAD` entry list; result arrives via
`rebaseTodoReady(QString base, QVariantList entries)` to populate the dialog.

Verbs: `Q_INVOKABLE startInteractiveRebase(QString base, QStringList actions, QStringList oids)`,
`continueRebase(QString message)` (empty string ⇒ no message — valid only for a
Conflict pause / fixup), `skipRebase()`, `abortRebase()`. Conflict regions resolve
through the **existing** `acceptConflict(region, which)` — no new resolution code.

### 3.4 Banner — extend `RebaseBanner.qml`

The Tier 1 banner already reads VM properties and shows *step k/n + summary +
conflict count + Continue/Skip/Abort*. Tier 2 extends it for the **Message** pause:

- `pause == "conflict"` → unchanged: Continue enabled only when
  `rebaseConflictedCount === 0`; Skip; Abort.
- `pause == "message"` → headline *"step k/n — editing message (`<action>`)"*;
  the **Continue** action opens the message editor (§3.5) instead of committing
  directly; Skip and **Abort** remain. (A fixup never produces a Message pause, so
  the editor never opens for it.)

Step k/n progress and Abort are present in **both** pause states — the progress and
always-reachable-abort guarantee the user asked for.

### 3.5 Message pause surface — reuse `RewordDialog.qml`

A Message pause reuses the history-editing round's `RewordDialog.qml` (summary +
body, prefilled). It opens prefilled from `rebaseMessagePrefill`; *Save* →
`repoVm.continueRebase(message)`; *Cancel* leaves the rebase paused (the banner
still offers Continue/Skip/Abort). No new message-editor component.

### 3.6 Controller

Async tasks `startInteractiveRebase` / `continueRebase(message)` / `skipRebase` /
`abortRebase`, each tailed by the Tier 1 `refreshAfterRebase` cascade (status →
`rebaseState` → `rebaseStateChanged`, plus history + branches + sync).
`buildRebaseTodo(fromOid)` walks `base..HEAD` and emits `rebaseTodoReady`.
**Auto-stash (D31)** and **mutual-exclusion (D33)** are inherited unchanged — the
controller stashes before start, defers the pop across pauses, pops on clean finish
/ abort / start-error. On a clean finish, emit `rebaseFinished(headOid)`; on a
pause, leave the repo mid-rebase and let the banner drive. Errors surface via
`operationFailed`.

---

## 4. Conflict & message resolution — reuse

- **Conflict pause** is, to the index and worktree, the same shape merge and Tier 1
  produce: conflict stages + worktree markers. The existing diff-view conflict
  regions and `acceptConflict(region, which)` resolve them unchanged.
- **Message pause** reuses `RewordDialog.qml`. The only genuinely new core surface
  is the manual cherry-pick engine + the `RebaseTodo` plan; the UI is mostly the
  todo dialog plus banner/VM extensions.

---

## 5. Safety

- **Mutual exclusion (D33).** Interactive rebase guards on a merge or any rebase in
  progress; the UI shows at most one banner.
- **Abort always reachable.** Every pause (conflict or message) offers Abort,
  restoring the exact `orig-head` via the saved tip + worktree reset, removing the
  state dir, popping the auto-stash.
- **Dirty tree.** Auto-stashed (D31), never silently discarded; pop-conflict keeps
  the stash and errors.
- **Rewrites commit ids → local only.** Interactive rebase rewrites the whole
  `base..HEAD` span; like reword/Tier 1 it only ever touches the local branch. The
  loud already-pushed warning lands with
  [network-sync](../../wishlist/network-sync.md).
- **Submodules.** Per-step cherry-pick preserves gitlink pointers exactly unless the
  picked commit changed them; gitlink conflicts reuse the merge engine's handling
  (§2.5).
- **No-limbo.** State is disk-truth (D30): a paused interactive rebase survives a
  restart and reconstructs its banner; the user is never stuck in a state they
  can't describe or exit.

---

## 6. Files

**New `core/`:** `rebase.hpp` gains `RebaseAction`, `RebaseTodoEntry`, `RebaseTodo`,
`RebasePause`, and the extended `RebaseState`/`RebaseOutcome`. `GitRepo` gains
`startInteractiveRebase`, the extended `continueRebase`/`skipRebase`/`abortRebase`,
a `buildRebaseTodo`-style range walk (or expose via existing log), and private
cherry-pick/state-dir helpers. New tests in the core test list.

**New `ui/qml/`:** `RebaseTodoDialog.qml`.

**Modified `ui/qml/`:**
- `CommitContextMenu.qml` — add *Edit history from here…*.
- `RebaseBanner.qml` — Message-pause variant (open editor on Continue).
- `HistoryPane.qml` — open the todo dialog; route `rebaseTodoReady`; open
  `RewordDialog` on a Message pause.

**Modified `ui/`:**
- `asyncrepo.hpp/.cpp` — wrappers for `startInteractiveRebase`, the extended verbs,
  `buildRebaseTodo`.
- `repoviewmodel.hpp/.cpp` — new `RebaseState` properties (`rebaseInteractive`,
  `rebasePauseReason`, `rebaseMessagePrefill`), `requestRebaseTodo` +
  `rebaseTodoReady`, the interactive verbs.
- `repocontroller.hpp/.cpp` — async interactive tasks + `buildRebaseTodo` +
  `rebaseTodoReady`; reuse `refreshAfterRebase`, auto-stash, mutual-exclusion.
- `ui/CMakeLists.txt` — register `RebaseTodoDialog.qml`.

**Modified spec:**
- [rebase.md](rebase.md) — note Tier 2 graduated; link here.
- [context-menus.md](context-menus.md) — add *Edit history from here…* to the
  `CommitContextMenu` table.
- [history-editing.md](history-editing.md) — its deferred trio (reword-older,
  squash, reorder) now ships here; update §7.
- [decisions.md](../decisions.md) — **D34** (manual cherry-pick engine + own state
  dir + mid-rebase message pause).

**Graduated wishes:** [rebase.md](../../wishlist/rebase.md) — interactive editor
moves from `idea` to designed; [history-editing.md](../../wishlist/history-editing.md)
— reword-older / squash / reorder graduate.

---

## 7. Relationship to Tier 1 and history-editing

Tier 2 is **purely additive** to Tier 1's surface: same banner component (extended,
not replaced), same conflict UI, same auto-stash and mutual-exclusion, same VM
property bag (extended). The **engine** differs — manual cherry-pick over a
user-edited plan rather than libgit2's auto-generated operation list (D34) — and is
selected at runtime by which on-disk state is present, so one verb set serves both.
The todo-list editor is the single home for every history-rewrite verb both wishes
named; nothing duplicates it.
