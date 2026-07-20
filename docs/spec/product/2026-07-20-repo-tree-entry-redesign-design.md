# Design — Repo tree entry redesign (branch + sync + dirty)

**Status:** planned
**Date:** 2026-07-20
**Realises:** an in-session wish to make each sidebar repo row show more useful
state at a glance.

## Problem

Each top-level repo row in the sidebar ([`Sidebar.qml`](../../../ui/qml/Sidebar.qml)
`TreeViewDelegate`) is a single thin line showing only the repo **name**. The
commit **short OID** is shown for submodule rows but not for repos, and there is
no at-a-glance view of:

- which **branch** the repo is on (currently only visible for the *active* repo,
  elsewhere in the window),
- whether the repo is **ahead/behind** its upstream (this *is* computed per row
  but is only surfaced as a tiny fetch-result glyph `↓N`),
- whether the working tree is **dirty or clean**.

Goal: a taller, two-line entry that shows name + dirty state on line 1 and
branch + ahead/behind on line 2. Prefer showing the current branch over a raw
hash.

## What already exists (reused, not rebuilt)

- **Core** has all the data. `GitRepo::head()` → `HeadState{branch, oid,
  detached}` ([`branchinfo.hpp`](../../../core/include/gittide/branchinfo.hpp));
  `GitRepo::syncStatus()` → `SyncStatus{ahead, behind, hasUpstream, …}`
  ([`sync.hpp`](../../../core/include/gittide/sync.hpp)); `GitRepo::status()` →
  `std::vector<FileStatus>` (non-empty ⇒ dirty).
- **Ahead/behind are already plumbed per row.** `ProjectController` calls
  `co_await repo.syncStatus()` and pushes counts into the model via
  `RepoListModel::setSyncCounts` ([`projectcontroller.cpp`](../../../ui/src/projectcontroller.cpp)).
  The `ahead`/`behind` roles already exist on the model.
- **Per-row refresh infra exists.** `RepoWatcher` refreshes the active repo
  (debounced); `ProjectController::pollRepos` re-reads non-active repos every 5 s
  while the window is active. Branch/dirty producers piggyback this same path —
  no new watcher.
- **Theme tokens** in [`theme.cpp`](../../../ui/src/theme.cpp) /
  [`qmltheme.hpp`](../../../ui/include/gittide/ui/qmltheme.hpp): `textPrimary`,
  `textSecondary`, `textMuted`, `stateModified` (amber), `stateAdded` (green),
  `stateIncoming`, `accent`.

So the work is: two new model fields/roles (branch, dirty count) + a producer
that reads `head()`/`status()` alongside the existing `syncStatus()` call + a
redesigned QML delegate. The underlying core data all exists.

## Layout

Two-line entry (A+B hybrid — stacked, with a right-aligned badge on line 1):

```
Dirty repo, tracking, ahead+behind:
▾  gittide                        ● 3      line1: name (primary, larger) + dirty badge
   ⎇ feat/theme-grey-blue    ↑2 ↓1         line2: branch (secondary, smaller) + arrows

Clean, synced (upstream, 0/0):
   my-lib                          ✓
   ⎇ main

Clean, no upstream:
   scratch                         ✓
   ⎇ main                    —

Detached HEAD:
   old-checkout                    ● 1
   detached  a1b9f3c                        no arrows
```

**Line 1 — name + dirty badge**
- Repo **name**: `theme.textPrimary`, a step **larger** than line 2, `DemiBold`
  when this is the active repo. Visually dominant.
- **Dirty badge** (right-aligned): dirty ⇒ `theme.stateModified` dot + changed-file
  **count** (`● 3`). Clean ⇒ a subtle `theme.stateAdded` check `✓`.

**Line 2 — branch + sync** (muted row, smaller than line 1)
- `⎇` glyph + **branch name** in `theme.textSecondary` (distinct colour *and*
  smaller size from the name, per user request).
- **Ahead** `↑N` in `theme.stateAdded`, **behind** `↓N` in `theme.stateIncoming`.
- **No upstream** ⇒ `—` (em dash, `textMuted`) in place of the arrows.
- **Upstream, 0/0** ⇒ arrows omitted (blank).
- **Detached HEAD** ⇒ line 2 reads `detached  <shortOid>`, no `⎇`, no arrows.

## Decisions

- **Branch over hash for repos.** Top-level rows show the current branch, not a
  commit hash. The short OID appears only in the detached-HEAD fallback.
- **Submodule rows are unchanged.** They keep their pinned short OID + status dot
  (the pinned commit is the meaningful identity for a submodule). This redesign
  targets **top-level repo rows only**.
- **Dirty = count, not just a dot** (user choice B). Clean shows a subtle check
  rather than nothing, so a scanned column reads consistently.
- **Reuse the sync-count refresh path.** Branch and dirty count are produced by
  the same `ProjectController` code that already computes ahead/behind, so all
  three stay consistent and refresh together.

## Plumbing

1. **`RepoListModel`** ([`repolistmodel.hpp`](../../../ui/include/gittide/ui/repolistmodel.hpp)
   / `.cpp`): add `branch` (QString), `detached` (bool), `dirtyCount` (int) to
   the repo `Node`; add matching `Roles` + `roleNames`. (`ahead`/`behind` already
   exist.) Add a setter (e.g. `setRepoState`) mirroring `setSyncCounts`.
2. **`ProjectController`** ([`projectcontroller.cpp`](../../../ui/src/projectcontroller.cpp)):
   where it `co_await repo.syncStatus()`, also read `repo.head()` and
   `repo.status()` (dirty count) for the same row and push all via the new setter.
3. **`Sidebar.qml`** delegate: replace the single-line `contentItem` with a
   two-line layout (name row + status row) per the mockup, all colours via
   `theme.*`. Increase row height accordingly.

## Testing

- **Model** (headless, `TempRepo`): new roles report branch / detached / dirty
  count correctly; a repo with N modified files reports `dirtyCount == N`; a repo
  with no upstream reports the no-upstream state; a detached checkout reports
  `detached == true` with a short OID.
- **UI** (headless runner): the delegate renders branch text on line 2 and the
  dirty badge on line 1; detached HEAD hides the arrows and shows the short OID.

## Out of scope

- Changing submodule row rendering.
- Any per-branch or multi-branch display (only the current HEAD branch).
- Making ahead/behind clickable / actionable (still display-only).
