# Merge — with conflict resolution

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `idea` |
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

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (merge action + conflict flow), spec/engineering (core merge + index conflict model, merge-in-progress state) · plan: plans/<file>
- Conflict-resolution UI fidelity (per-file ours/theirs vs 3-pane editor) rejects an alternative → log in decisions.md
-->
