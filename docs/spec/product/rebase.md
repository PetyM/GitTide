# Rebase — plain driver (Tier 1)

| | |
|--|--|
| **Designed** | 2026-06-24 |
| **Status** | `spec` |
| **Wishlist** | [rebase.md](../../wishlist/rebase.md) · [history-editing.md](../../wishlist/history-editing.md) |
| **Touches** | core: `RebaseState` + driver verbs on `GitRepo`; ui: branch/app-menu entry points, `RebaseBanner`, `BranchPickerDialog`, ViewModel/Controller wiring; design: rebase-in-progress banner |

## Overview

Replay the current branch's commits onto a chosen target — the **plain,
non-interactive** rebase the [rebase wish](../../wishlist/rebase.md) insists on
building first. Drive it **step by step**: a conflicting step pauses; the user
resolves it with the **existing merge/conflict UI**, then **continues**; or
**skips** the step; or **aborts** to the exact pre-rebase state.

This is the **driver** the interactive engine hangs its todo-list editor off.
**Tier 2 (interactive) has graduated** — see
[rebase-interactive.md](rebase-interactive.md).

The design mirrors the shipped **merge engine** throughout: a disk-derived state
struct (D30), `Expected<T>` core verbs, an auto-stash dirty-tree guard (D31), a
banner, and the same per-region conflict resolution (`acceptConflict`). The
incremental cost over merge is the **stepwise driver** (continue / skip / abort)
and its k/n in-progress state.

## Scope

**In:**

- Rebase the current branch onto a **target branch** (its tip).
- Step-by-step drive: continue (after resolving a conflict), skip, abort.
- A rebase-in-progress banner showing **step k of n** and the current commit.
- Two entry points: branch context menu (direct) and app-menu popup (picker).

**Out (deferred to the interactive-rebase engine):**

- Any todo-list editor — reorder, drop, fixup, squash, reword-older.
- Rebase onto an **arbitrary commit** (commit context menu). Target is a branch
  tip only in this cut.
- `--onto` three-arg gymnastics, autosquash, rebasing remote branches.
- An already-pushed-commit warning — deferred with reword to
  [network-sync](../../wishlist/network-sync.md).

---

## 1. Core (`core/`, no Qt)

### 1.1 `RebaseState` (`rebase.hpp`)

Disk-truth, derived **every call** from the repo's `rebase-merge/` state (never
cached — D30), exactly as `MergeState` is:

```cpp
struct RebaseState
{
    bool        inProgress = false;
    std::string ontoRef;        ///< target branch shorthand, e.g. "main"
    int         current = 0;    ///< current step, 1-based (0 when none)
    int         total   = 0;    ///< total steps
    std::string stepSummary;    ///< summary of the commit being applied
    std::vector<std::filesystem::path> conflictedPaths;       ///< unresolved entries
    std::vector<std::filesystem::path> conflictedSubmodules;  ///< gitlink subset
};
```

`current`/`total` come from libgit2's `git_rebase_operation_current` /
`git_rebase_operation_entrycount`. `conflictedPaths`/`conflictedSubmodules` reuse
the same index-conflict iteration `MergeState` already performs, so the
conflict-resolution UI is shared verbatim.

### 1.2 Driver verbs on `GitRepo` (all `Expected<T>`)

```cpp
/// Start rebasing the current branch onto ontoRef's tip. Auto-stashes a dirty
/// worktree (D31). Runs git_rebase_next/_commit until a step conflicts or all
/// steps apply; git_rebase_finish on a clean run (pops the stash). Returns the
/// outcome (clean vs. paused-on-conflict).
Expected<RebaseOutcome> startRebase(std::string ontoRef);

/// Commit the resolved current step (git_rebase_commit; GIT_EAPPLIED => auto-skip),
/// then resume the next/commit loop until the next conflict or finish.
Expected<RebaseOutcome> continueRebase();

/// Advance past the current step without committing it (git_rebase_next only),
/// then resume the loop.
Expected<RebaseOutcome> skipRebase();

/// git_rebase_abort: restore the exact pre-rebase HEAD and worktree, pop the stash.
Expected<void> abortRebase();

/// Derive RebaseState from disk (mirrors mergeState()).
RebaseState rebaseState() const;
```

`RebaseOutcome` carries `bool conflicted` plus the conflicted-path lists (mirrors
`MergeOutcome`), so the controller can branch without a second disk read.

### 1.3 Semantics & guards

- **onto resolution.** `ontoRef` is a local branch shorthand; resolve to its tip
  oid, `git_rebase_init` with HEAD's branch as the rebased ref.
- **Auto-stash (D31).** Same machinery as checkout/merge: a dirty worktree is
  stashed before `init` and popped on `finish`/`abort`. A pop conflict preserves
  the stash and errors.
- **Per-step loop.** `startRebase` and `continueRebase`/`skipRebase` each run the
  `git_rebase_next` → (`git_rebase_commit` | conflict) loop until either a step
  leaves conflict entries (pause, return `conflicted=true`) or operations are
  exhausted (`git_rebase_finish`, return `conflicted=false`).
- **Empty / already-applied step.** `git_rebase_commit` returning `GIT_EAPPLIED`
  (the change is already upstream) is treated as an implicit skip — advance, do
  not error.
- **Refresh cascade.** Every step rewrites tree/index/HEAD → the UI refreshes
  status + history + branches per pause, like merge.
- **Guards (`Expected` left):** unborn `HEAD`; **detached** `HEAD` (no branch to
  move — revisit with the engine); `ontoRef` not found; a rebase already in
  progress; a **merge** in progress (mutually exclusive — see §4).

### 1.4 Tests (`TempRepo`, Catch2, test-first)

- Clean rebase: branch with N commits onto a target replays all N; HEAD's new
  parent chain roots at the target tip; `rebaseState().inProgress == false`.
- Conflicting step: rebase across a conflicting change pauses with
  `inProgress == true` and the conflicted path listed.
- `continueRebase` after writing a resolved file advances to the next step /
  finishes; final history is linear onto the target.
- `skipRebase` drops exactly the current commit from the result.
- `abortRebase` restores the **exact** pre-rebase HEAD oid and a clean worktree.
- Already-applied step (a commit whose change is already on the target)
  auto-skips without error.
- Guards: unborn, detached, missing onto, rebase-in-progress, merge-in-progress
  each error.
- Dirty worktree is stashed and restored across a clean rebase.

---

## 2. UI — entry points, state, banner

### 2.1 Entry points

Two routes, one VM verb (`startRebase(ref)`):

- **`BranchContextMenu.qml`** — item **"Rebase current onto `<name>`"**, **hidden
  when `isHead`** (the current branch can't rebase onto itself), mirroring the
  *Merge into current* gating. `onRebase → repoVm.startRebase(name)`.
- **App-menu popup** — item **"Rebase current branch…"** (now in the title-bar
  Repository menu, see [app-menu §7](app-menu.md)) opens `BranchPickerDialog.qml`.
  The app menu is global, so it needs an explicit target picker rather than a
  baked-in branch.

### 2.2 `BranchPickerDialog.qml`

A reusable local-branch picker (originally `RebaseTargetDialog.qml`, generalised
in Plan 29 to also serve *Merge into current*): `OverlayCard` with a list of local
branches (excluding the current branch), single-select, and a caller-supplied
action/prompt (**Rebase** / **Cancel** here). *Rebase* → `repoVm.startRebase(selectedRef)`.

### 2.3 ViewModel state

`RebaseState` mirrored onto `RepoViewModel` as Q_PROPERTYs under one
`rebaseStateChanged` NOTIFY (mirrors `mergeStateChanged`):
`rebaseInProgress`, `rebaseOnto`, `rebaseStep`, `rebaseTotal`,
`rebaseStepSummary`, `rebaseConflictedCount`, `rebaseHasSubmoduleConflicts`.

Verbs: `Q_INVOKABLE startRebase(QString ref)`, `continueRebase()`,
`skipRebase()`, `abortRebase()` — each delegates to the controller. Conflict
regions are resolved through the **existing** `acceptConflict(region, which)` —
no new resolution code.

### 2.4 `RebaseBanner.qml` (new)

A clone of `MergeBanner.qml` (same `surfaceRaised` strip, `stateConflict` accent,
collapses to height 0 when idle), reading only VM properties:

> ⚠ Rebasing onto `<onto>` — step `k`/`n` — `<stepSummary>` — `c` conflicted file(s)

Buttons:
- **Continue** — `enabled` only when `rebaseConflictedCount === 0`; calls
  `repoVm.continueRebase()`.
- **Skip** — `repoVm.skipRebase()`.
- **Abort** — `repoVm.abortRebase()`.
- **Deinit submodules & retry** — reuse the merge banner's pattern **only if a
  submodule conflict can arise**; otherwise omit (kept out of the first cut unless
  a test surfaces the need).

The banner shares the changes-pane header slot with `MergeBanner`; only one is
ever visible because rebase and merge are mutually exclusive (§4).

### 2.5 Controller

Async tasks `startRebase` / `continueRebase` / `skipRebase` / `abortRebase`, each
tailed by `refreshAfterRebase` (status — which refreshes `RebaseState` →
`rebaseStateChanged` — plus history + branches + sync), the same cascade shape as
`refreshAfterMerge`. On a clean finish, emit `rebaseFinished(headOid)`; on a
conflicted pause, leave the repo mid-rebase and let the banner drive. Errors
surface via the existing `operationFailed` channel.

---

## 3. Conflict resolution — reuse

A paused rebase step is, to the index and worktree, the same conflict shape a
merge produces: conflict stages in the index, markers in the worktree. The
existing diff-view conflict regions and `acceptConflict(region, which)` resolve
them unchanged. The **only** new surface is the banner's verbs (continue / skip /
abort) replacing the merge banner's commit / abort — there is no per-step commit
message UI, because plain rebase reuses each original commit's message
(`git_rebase_commit` with null author/message).

---

## 4. Safety

- **Mutual exclusion.** A rebase cannot start mid-merge, and a merge cannot start
  mid-rebase; each verb guards on the other's disk state. The UI shows at most one
  banner. Logged in [decisions.md](../decisions.md).
- **Abort always reachable.** Every paused step offers Abort, which restores the
  exact pre-rebase HEAD via `git_rebase_abort` + stash pop.
- **Dirty tree.** Auto-stashed (D31), never silently discarded; a pop conflict
  preserves the stash and errors.
- **Rewrites commit ids → local only.** Like reword, rebase only ever rewrites the
  local branch. The loud already-pushed warning lands with
  [network-sync](../../wishlist/network-sync.md); until then it is local by
  construction.
- **Submodules.** Per-step checkout must not clobber submodule pointers; the
  merge engine's submodule-conflict handling is reused if a gitlink conflicts.

---

## 5. Files

**New `core/`:** `rebase.hpp` (`RebaseState`, `RebaseOutcome`); `GitRepo`
methods `startRebase`, `continueRebase`, `skipRebase`, `abortRebase`,
`rebaseState`. New tests in the core test list.

**New `ui/qml/`:** `RebaseBanner.qml`, `BranchPickerDialog.qml` (was
`RebaseTargetDialog.qml`; renamed in Plan 29).

**Modified `ui/qml/`:**
- `BranchContextMenu.qml` — add *Rebase current onto `<name>`* (hidden when `isHead`).
- App-menu popup (`AppMenu` in `Main.qml`/`TitleBar.qml`) — add *Rebase current branch…*.
- The changes-pane layout — host `RebaseBanner` alongside `MergeBanner`.

**Modified `ui/`:**
- `asyncrepo.hpp/.cpp` — pool wrappers for the four driver verbs + `rebaseState`.
- `repoviewmodel.hpp/.cpp` — `RebaseState` properties, `rebaseStateChanged`, the
  four verbs.
- `repocontroller.hpp/.cpp` — async driver tasks + `refreshAfterRebase` +
  `rebaseFinished`.
- `ui/CMakeLists.txt` — register the two new QML files.

**Modified spec:**
- [context-menus.md](context-menus.md) — add the *Rebase* row to the
  `BranchContextMenu` table.
- [app-menu.md](app-menu.md) — add *Rebase current branch…* to the popup.
- [decisions.md](../decisions.md) — rebase⇄merge mutual exclusion; driver-first
  (no interactive editor) rationale.

**Graduated wish:** [rebase.md](../../wishlist/rebase.md) — driver part moves from
`idea`; the interactive editor stays `idea`.

---

## 6. Deferred — the interactive engine

Reorder / drop / fixup / squash / reword-older still need a **todo-list editor**
on top of this driver, jointly owned by [rebase.md](../../wishlist/rebase.md) and
[history-editing.md](../../wishlist/history-editing.md) (the two must not
duplicate the editor). The branch/app-menu entry points and the banner built here
are the surfaces that engine extends.
