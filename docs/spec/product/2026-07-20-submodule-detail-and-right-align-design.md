# Design — Submodule detail rows + right-aligned sync

**Status:** shipped
**Date:** 2026-07-20
**Shipped:** 2026-07-20 (Plan 34)
**Builds on:** [`2026-07-20-repo-tree-entry-redesign-design.md`](2026-07-20-repo-tree-entry-redesign-design.md)
(Plan 33), which gave top-level repo rows a two-line branch/sync/dirty entry.

## Problem

Plan 33 upgraded **top-level repo** rows to a two-line entry (name + dirty badge;
branch + ahead/behind). Two gaps remain:

1. **Submodule rows stayed minimal** — an initialized submodule still shows only
   its pinned short OID + a single clean/dirty dot. It gives no sense of what the
   submodule is actually on (branch / detached), how dirty it is, or whether its
   checkout has drifted from the commit the superproject pins.
2. **Ahead/behind sit inline, left-aligned** on line 2, so the sync indicators
   don't form a clean scannable column down the sidebar.

Goal: give initialized submodules the same two-line detail as repos, and
right-align the ahead/behind indicators (repos and submodules) so line-1 dirty
badge and line-2 sync form one right-hand column.

## What already exists (reused, not rebuilt)

- **`submoduleTree()` already opens each initialized submodule** as its own
  `GitRepo` during recursion (`core/src/gitrepo.cpp`, `GitRepo::open(node.path)`),
  so per-submodule `head()` / `status()` reuse a handle the enumeration already
  creates — no extra opens.
- **libgit2 exposes both OIDs**: `git_submodule_head_id` (the **pinned** commit,
  already used for `SubmoduleNode::shortOid`) and `git_submodule_wd_id` (the
  submodule's **current** checkout) — the latter is not read today.
- **`git_graph_ahead_behind`** is already wrapped once, in `GitRepo::syncStatus()`
  — the exact pattern to reuse for current-vs-pinned.
- **UI `Node` fields + QML roles already exist** from Plan 33: `branch`,
  `detached`, `dirtyCount`, `ahead`, `behind`, `shortOid`. Submodule nodes simply
  don't populate them yet (`appendSubmodules` leaves them at defaults).

So the work is: new `SubmoduleNode` fields + fill them in `submoduleTree()` + one
small core ahead/behind helper; copy them in `appendSubmodules` and compare them
in the poll's `reconcileChildren`; and a QML delegate change (two-line submodule +
right-aligned sync for both row kinds).

## Layout

Line-1 dirty badge and line-2 sync form a **right-hand column**; branch text stays
left on line 2.

```
Repo — dirty, tracking, diverged:
gittide                          ● 3       line1: name (left) ........ dirty badge (right)
⎇ feat/theme            ↑2 ↓1              line2: branch (left) ...... ↑ahead ↓behind (right)

Submodule — detached, 2 past the pin, dirty:
  libs/core                      ● 3
  ⎇ detached  a1b9f3c      ↑2               ↑/↓ = current HEAD vs the pinned commit

Submodule — on a branch, clean, on pin:
  libs/ui
  ⎇ main

Submodule — uninitialised (unchanged):
  vendor/foo            [ Init ]           greyed, single line, no branch/sync
```

**Right alignment (both row kinds).** On line 2, a flexible spacer sits between
the branch text and the ahead/behind labels, pushing `↑N/↓N` (and the repo
no-upstream `—`) to the right edge, aligned under the line-1 dirty badge. The
dash `—` for repos with no upstream keeps its line-2-right position.

**Submodule line 1.** Name + the same dirty badge repos use (amber dot +
changed-file count, or a subtle `✓` when clean) — this **replaces** the old
single clean/dirty dot. The dirty badge is gated to initialized submodules; an
uninitialized submodule shows neither badge nor line 2.

**Submodule line 2** (initialized only): `⎇ <branch>` when on a branch, else
`detached  <headShortOid>`; then right-aligned `↑N/↓N` = the submodule's current
HEAD ahead/behind of its **pinned** commit. When current == pinned (ahead 0 /
behind 0) no arrows show; a clean submodule on a branch shows just `⎇ <branch>`.

## Decisions

- **Ahead/behind for submodules = current HEAD vs the pinned commit** (not vs a
  remote upstream). *Why:* the pin is the submodule's contract with the
  superproject; "have I drifted from the pin, and which way" is the question a
  multi-repo user asks. Computed on the **submodule's own repository** (both OIDs
  live there), never the superproject's.
- **Show the current short OID only when detached** (mirrors the repo rule from
  Plan 33). On a branch, the branch name carries identity; the pin divergence is
  conveyed by the arrows.
- **Pinned commit absent (shallow / unfetched) → no arrows.** `git_graph_ahead_behind`
  fails when an OID isn't reachable; treat that as "unknown", leave ahead/behind
  at 0 (no arrows), never error the whole tree.
- **Fill in `submoduleTree()`, one path.** Because both the load-time seed
  (`RepoListModel::setRepos`) and the 5-second poll (`ProjectController::pollRepos`
  → `AsyncRepo::submoduleTree`) already route through `submoduleTree()`, filling
  the fields there means both paths stay consistent with zero duplication — the
  same reason Plan 33 seeded repo state in one place.
- **Keep `SubmoduleNode::shortOid` = the pinned commit** (its current meaning);
  add a distinct `headShortOid` for the current checkout. Avoids a semantic
  change to an existing field.

## Plumbing

1. **`core` — `SubmoduleNode`** (`core/include/gittide/submodule.hpp`): add
   `branch` (std::string), `detached` (bool), `headShortOid` (std::string, current
   checkout — distinct from the pinned `shortOid`), `dirtyCount` (int), `ahead`
   (int), `behind` (int).
2. **`core` — ahead/behind helper** (`GitRepo`): a small method that, given a
   repo and two commit OIDs, returns `Expected<std::pair<int,int>>` (ahead,
   behind) via `git_graph_ahead_behind`, mirroring `syncStatus()`. Used on the
   submodule repo with current (`wd_id`) vs pinned (`head_id`).
3. **`core` — `submoduleTree()`** (`core/src/gitrepo.cpp`): capture
   `git_submodule_wd_id` alongside the existing `head_id` in the `foreach`; in the
   recursion step where the child repo is already opened, fill `branch`/`detached`/
   `headShortOid` from `child->head()`, `dirtyCount` from `child->status().size()`,
   and `ahead`/`behind` from the helper (current vs pinned). Uninitialized
   submodules keep empty/zero.
4. **`ui` — `appendSubmodules`** (`ui/src/repolistmodel.cpp`): copy the six new
   fields onto the `Node`. Map `SubmoduleNode::headShortOid` → the Node's
   `shortOid` role for the detached-oid display (the delegate shows the current
   checkout, not the pin).
5. **`ui` — poll reconcile** (`submodulesEqual` / `reconcileChildren`): extend the
   equality check and the in-place field update to include the new fields, so a
   poll refresh updates a submodule's branch/dirty/sync without collapsing an
   expanded subtree.
6. **`ui` — `Sidebar.qml`**: make the submodule branch of the delegate two-line
   (mirroring the repo branch, gated on initialized); add the flexible spacer that
   right-aligns ahead/behind on line 2 for both row kinds; the dirty badge already
   exists — extend its `visible` to initialized submodules and drop the old
   single status dot.

## Testing

- **Core** (`TempRepo`): a superproject with an initialized submodule reports the
  submodule's branch (or detached + current short OID), `dirtyCount`, and
  ahead/behind vs the pin. Cases: submodule checked out **past** the pin
  (ahead > 0); submodule **behind** the pin (pin advanced, submodule not updated,
  behind > 0); on-pin (ahead 0 / behind 0); dirty working tree (`dirtyCount` > 0);
  uninitialized (all fields empty/zero, unchanged classification).
- **UI model**: `appendSubmodules` exposes the new roles on a submodule row;
  `applySubmodules` updates them in place on a branch/pin change without a
  destructive rebuild (extends the existing identity-preservation test).
- **UI QML**: `Main.qml` still loads with the extended delegate (load-regression
  guard); a submodule row exposes branch/dirtyCount/ahead. Right-alignment and
  the two-line submodule layout are verified visually in the running app.

## Out of scope

- Ahead/behind of a submodule versus its **own remote** upstream (only vs pin).
- Recursively fetching submodules to resolve an absent pinned commit.
- Changing uninitialized-submodule rendering or the `Init`/update flows.
