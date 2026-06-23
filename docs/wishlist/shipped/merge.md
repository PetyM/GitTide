# Merge — with conflict resolution

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `shipped` (2026-06-22) |
| **Touches** | product (merge action + conflict flow), engineering (core: merge + index conflict state on `GitRepo`), design (conflict resolution UI, merge-in-progress state) |

## What

Integrate one branch into another from inside GitTide. Per repo:

- **Merge** a selected local branch into the current branch (fast-forward when
  possible; otherwise a real merge commit).
- When the merge **conflicts**, show the conflicted files and let the user resolve
  them — at minimum: pick *ours* / *theirs* per file, edit the merged result, and
  mark resolved — then **commit** the merge. **Abort** a merge in progress to
  return to the pre-merge state.
- A clear **merge-in-progress** indicator (the repo is mid-merge; commit or
  abort).

## Why

Branching without merging is half a workflow. Once [branch
management](branch-management.md) lets the user create and switch branches, merge
is how that work comes back together — and conflict resolution is the part users
most want to *not* do in a terminal + `vimdiff`. It's the natural next step after
branches, still **entirely local** (no network), and it builds directly on the
diff/hunk machinery GitTide already has. The product spec lists merge +
conflict-resolution UI together as post-MVP; this is that.

## Notes

- **Builds on branch management.** Depends on [branch
  management](branch-management.md) existing (you need branches to merge). Could
  share its "pick a branch" UI.
- **Layering.** `core/` on `GitRepo`: libgit2 `git_merge`, `git_merge_analysis`
  (FF vs normal vs up-to-date), reading conflict entries from the index
  (`git_index_conflict_*`), writing resolved blobs, and creating the merge commit.
  Returns `Expected<T>`. The conflict *model* (which files, the three sides) is
  core data; the resolution *UI* is Qt.
- **Conflict UI — the real design work.** Three-way state per conflicted file
  (base / ours / theirs). First cut can be coarse: per-file ours/theirs + "edit in
  place, then mark resolved," reusing the diff viewer. A proper 3-pane merge
  editor with per-hunk pick is a richer later iteration — resist over-building it
  now (YAGNI).
- **Merge-in-progress is a real repo state.** `MERGE_HEAD` exists; status, the
  commit box, and available actions all change while mid-merge. The UI must
  represent "you are resolving a merge" and offer **abort** (`git_merge` cleanup /
  reset to `ORIG_HEAD`) without losing work unexpectedly.
- **Refresh cascade.** A merge rewrites the tree and index → full status + diff +
  history refresh, like checkout.
- **Never clobber silently.** Refuse to start a merge with a dirty tree (or guard
  it), consistent with checkout-safety in the branch wish.
- **Scope creep to resist.** No merge strategies/options beyond default, no octopus
  merges, no rebase (that's [rebase](rebase.md)), no remote-branch merge until
  [network sync](network-sync.md) lands. First cut: merge a local branch, resolve,
  commit, abort.

---

## Graduated → designed (2026-06-22)

Designed into the living spec:

- **Product:** [`spec/product/product.md` § Merge](../../spec/product/product.md#merge)
  — merge action (branch dropdown + history context menu), outcomes, the
  merge-in-progress state, inline conflict resolution, submodule deinit-and-retry.
- **Engineering:**
  [`spec/engineering/engineering.md` § Merge & conflict resolution](../../spec/engineering/engineering.md#merge--conflict-resolution)
  — core merge API, the merge-state-from-disk invariant, controller-side
  auto-stash + reactive submodule deinit.
- **Design:**
  [`spec/design/design.md`](../../spec/design/design.md) — `state.incoming` token,
  the merge banner, and the inline conflict view.

Decisions logged: **D29** (inline VS-Code-style conflict UI over coarse per-file
or full 3-pane), **D30** (merge state derived from disk — the no-limbo
guarantee), **D31** (controller-side auto-stash + reactive submodule
deinit-and-retry).

---

## Shipped (2026-06-22)

Built across two plans, both `done`:

- **[Plan 14a — Core merge engine](../../plans/2026-06-22-plan14a-core-merge-engine.md)**
  — `GitRepo::{mergeBranch, mergeState, commitMerge, abortMerge, stashSave,
  stashPop, deinitSubmodule, reinitSubmodule}`, `StatusFlag::Conflicted`,
  `merge.hpp` model.
- **[Plan 14b — UI merge + inline conflict](../../plans/2026-06-22-plan14b-ui-merge-conflict.md)**
  — AsyncRepo wrappers, RepoController orchestration (auto-stash + reactive
  submodule retry), RepoViewModel merge properties/actions, the `C` letter,
  conflict-region parse/accept, `MergeBanner.qml`, inline `DiffView` rendering,
  and the dropdown + history entry points.
