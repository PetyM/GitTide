# History editing — range diff & reword (round 1)

| | |
|--|--|
| **Designed** | 2026-06-24 |
| **Status** | `spec` |
| **Wishlist** | [history-editing.md](../../wishlist/history-editing.md) · [rebase.md](../../wishlist/rebase.md) |
| **Touches** | core: two read/rewrite ops on `GitRepo`; ui: history multi-select + new dialog + ViewModel/Controller wiring; design: multi-select highlight |

## Overview

Two **independent, cheap** slices graduate from the
[history-editing](../../wishlist/history-editing.md) and
[rebase](../../wishlist/rebase.md) wishes — the parts that need **no
interactive-rebase engine**:

1. **Combined range diff** — select a contiguous range of commits in the History
   graph and see their *net* changes as one diff.
2. **Reword the tip** — rewrite the message of the `HEAD` commit (and only the
   tip), via `git_commit_amend` with the tree unchanged.

The larger trio — **reword of older commits, squash, reorder** — is deliberately
**deferred**. Each rewrites every descendant commit, which is interactive rebase
under the hood (rebase driver + todo-list editor + per-step conflict UI). That is
the "big, separate iteration" both wishes warn against building prematurely
(YAGNI). This round ships the two slices that stand alone, and leaves the menu and
selection model as the natural home for the rest when the rebase engine lands.

## Scope

**In:**

- Multi-select of commits in the History graph (Shift-click range, Ctrl-click
  toggle).
- A **combined diff** of a contiguous selection, shown in the existing commit
  detail pane.
- **Reword** the `HEAD` commit's message from the commit context menu.

**Out (deferred to the interactive-rebase engine):**

- Reword of any commit other than the tip.
- Squash / fixup of a range.
- Reorder of commits.
- Combined diff of a **non-contiguous** selection (no single tree-vs-tree diff
  represents only the selected commits — see [§3](#3-combined-diff-semantics)).

---

## 1. Core (`core/`, no Qt)

Three new methods on `GitRepo`, returning `Expected<T>`. The range diff mirrors
the existing **per-file** commit-detail model (`commitFiles` + `commitDiff`) so it
reuses the same UI plumbing — not a single monolithic diff:

```cpp
/// Files changed across the inclusive range oldOid..newOid:
/// tree(parent(oldOid)) vs tree(newOid). Flags use Index* (added/modified/deleted),
/// matching commitFiles().
Expected<std::vector<FileStatus>> rangeFiles(std::string oldOid, std::string newOid) const;

/// Diff one file across the inclusive range oldOid..newOid (same tree pair as
/// rangeFiles). Mirrors commitDiff()'s DiffResult.
Expected<DiffResult> rangeDiff(std::string oldOid, std::string newOid,
                               const std::filesystem::path& file) const;

/// Rewrite HEAD's commit message, keeping its tree and parents exactly.
Expected<std::string> rewordHead(std::string newMessage);   // returns new oid

/// Full commit message (summary + body) for the 40-char hex oid.
/// Used to pre-fill the reword dialog (the history model carries only the summary).
Expected<std::string> commitMessage(std::string oid) const;
```

### 1.1 `rangeFiles` / `rangeDiff`

- `oldOid` is the **older** endpoint, `newOid` the **newer**. The net change of
  the range `oldOid..newOid` *inclusive* is `tree(parent(oldOid))` vs
  `tree(newOid)`. Both reuse the existing private `commitTrees` helper:
  `parent(oldOid)`'s tree (the older endpoint's first-parent tree) vs `newOid`'s
  own tree.
- If `oldOid` is a **root commit** (no parent), diff against the **empty tree** —
  the range then includes the repository's first commit (`commitTrees` already
  yields a null parent tree for a root commit).
- `rangeFiles` returns the same `FileStatus` list the commit-detail file list
  renders; `rangeDiff` returns the same `DiffResult` per file — no new diff
  plumbing, the read-only `CommitDetail` view is reused verbatim.
- Contiguity is the **caller's** guarantee (the UI only calls these for a gap-free
  selection). Core simply diffs the two endpoints' trees; it does not walk or
  validate the range.
- Errors (`Expected` left): either oid not found / not a commit.

### 1.2 `rewordHead(newMessage)`

- `git_commit_amend` over `HEAD` with **the same tree** and **the same parents**,
  replacing only the message. The working tree and index are **untouched** (we
  pass `HEAD`'s existing tree, never the index), so a dirty worktree is irrelevant
  and is *not* folded in.
- Because the tree is reused verbatim, **submodule pointers are preserved
  exactly** — the GitHub-Desktop failure mode the wish calls out cannot occur
  here (no staged content is dragged in).
- Author identity is preserved; committer is refreshed from config (standard
  amend semantics).
- Guards (`Expected` left): **unborn `HEAD`** (no commit to reword);
  **detached `HEAD`** — disallowed in the first cut (rewording a detached commit
  rewrites nothing reachable by a branch; revisit with the rebase engine).
- Returns the new commit oid. `HEAD` advances to it; descendants don't exist
  (it's the tip), so nothing else is rewritten.

### 1.3 Tests (`TempRepo`, Catch2, test-first)

- `rangeFiles` over a 3-commit range lists the net file set of all three changes;
  `rangeDiff` for one of those files equals its net hunks across the range.
- `rangeFiles` whose `oldOid` is the root commit diffs against the empty tree
  (every file shows as added).
- `rangeFiles`/`rangeDiff` of a single commit (old==new) equal that commit's own
  `commitFiles`/`commitDiff`.
- `rewordHead` changes the message, **keeps the tree** (same tree oid) and
  **keeps the parent**.
- `rewordHead` in a repo **with a submodule**, rewording an unrelated change,
  leaves the submodule pointer entry **untouched**.
- `rewordHead` with a **dirty worktree** does not fold the dirty changes in.
- `rewordHead` on an **unborn** branch → error; on **detached** `HEAD` → error.
- `commitMessage` returns the full summary+body of a commit (multi-line message
  round-trips); error on a bad oid.

---

## 2. UI — selection model & history pane

### 2.1 Selection state

The History view tracks a **selected-row set** (list of row indices) in
`HistoryPane.qml` — it already owns `currentIndex`. On every change it hands the
whole set to a single `RepoViewModel::selectCommitRows(rows)` call; the ViewModel
resolves rows → oids against its history model, tests contiguity, and routes to
the right detail outcome (single / range / hint). Keeping resolution and the
contiguity test in the ViewModel makes them unit-testable and keeps the QML to set
bookkeeping + highlight. Row highlighting (the existing `accent` border + row
highlight) is painted for every member of the set.

The reword dialog needs the commit's **full** message (summary + body), which the
history model does not carry (rows hold only the first-line `SummaryRole`).
Rather than thread full bodies through the whole `log → GraphBuilder → GraphRow`
chain and bloat every row, the dialog **lazy-fetches** the message for the one
commit being reworded (always `HEAD`) via a dedicated core call.

### 2.2 Gestures (`HistoryPane.qml`)

| Gesture | Effect |
|---------|--------|
| **Click** | Clear the set, select one commit (existing single-commit detail). |
| **Shift+click** | Select the contiguous range anchor..clicked (fills the set with every row between). |
| **Ctrl+click** | Toggle the clicked oid in the set (arbitrary multi-select). |

Selected rows all carry the existing selection highlight (the 2px `accent` left
border + row-wide highlight), extended to paint every member of the set.

### 2.3 Detail routing

`RepoViewModel::selectCommitRows(rows)` inspects the set and drives the
`CommitDetail` pane, which reuses the existing `commitFiles`/`commitDiff` models
plus two new VM string properties (a header line and a hint line):

- **1 row** → the existing single-commit flow; header/hint cleared.
- **≥2 rows, contiguous** → populates the file list via `rangeFiles` and per-file
  diffs via `rangeDiff` (oldest = max row index since history is newest-first,
  newest = min), and sets the header *"Changes across N commits
  (`<shortOld>`…`<shortNew>`)"*.
- **≥2 rows, with a gap** → clears the panes and shows the hint *"Select a
  contiguous range to see combined changes."* — **no** core call.

Contiguity is tested on the row indices (sorted indices consecutive).

---

## 3. Combined-diff semantics

"Combined changes of these commits" means the **net** change — one tree-vs-tree
diff. That is well-defined **only for a contiguous range**: `tree(parent(oldest))`
vs `tree(newest)`. With a gap (Ctrl-click skipping middle commits), no single
tree-vs-tree diff represents *only* the selected commits, because the unselected
in-between commits also touched files. Rather than silently show a misleading
span, the UI **disables** the combined-diff view for non-contiguous selections and
prompts for a contiguous range ([§2.3](#23-detail-routing)).

**Contiguity test:** map each selected oid to its row index in the loaded history
model; the sorted indices must be consecutive. Caveat: this is only meaningful
within the **loaded/visible** log window — acceptable for the first cut.

---

## 4. Reword wiring (QML → ViewModel → Controller → core)

- **`CommitContextMenu.qml`** — new item **"Reword…"**, **visible only when
  `isHead`** (mirrors how *Merge* is gated to branch tips; per the
  disabled-vs-hidden rule in [context-menus](context-menus.md#13-disabled-vs-hidden-rule),
  rewording a non-tip can *never* apply in this round, so the item is hidden, not
  a permanently-disabled tease). Emits `reword()`.
- **`HistoryPane.qml`** — `onReword: rewordDialog.open()` then
  `repoVm.requestCommitMessage(commitMenu.oid)` to lazy-fetch the body; the dialog
  pre-fills when `RepoViewModel::commitMessageReady(oid, message)` arrives.
- **`RewordDialog.qml`** (new) — an `OverlayCard` following the existing dialog
  pattern: a summary field + a body text area, pre-filled from the fetched
  message. *Save* → `repoVm.rewordHead(newMessage)`; *Cancel* discards.
- **`RepoViewModel`** — `Q_INVOKABLE void rewordHead(QString message)` and
  `Q_INVOKABLE void requestCommitMessage(QString oid)`, plus signal
  `commitMessageReady(QString oid, QString message)`. Delegates to
  `RepoController`.
- **`RepoController`** — `rewordHead` async task calls `GitRepo::rewordHead` then
  runs the **refresh cascade** (status + history + branches — `HEAD`'s oid
  changes), the same cascade as checkout/merge; `requestCommitMessage` calls
  `GitRepo::commitMessage` and emits `commitMessageReady`. Errors surface via the
  existing `operationFailed` channel.

Reword is guarded twice: the menu only offers it on `HEAD`, and core re-checks
(`Expected` error if not the tip / unborn / detached).

## 5. Safety

- Reword **rewrites a commit id**. It is purely **local** and only ever touches
  the tip. Once [network sync](../../wishlist/network-sync.md) warnings for
  already-pushed commits exist, reword joins them; until then it is local-only by
  construction.
- No dirty-tree guard is needed — reword never reads the index/worktree and never
  mutates them.
- Submodule pointers are preserved exactly ([§1.2](#12-rewordheadnewmessage)).

---

## 6. Files

**New `core/`:** methods on `GitRepo` (`gitrepo.hpp` / `gitrepo.cpp`):
`rangeFiles`, `rangeDiff`, `rewordHead`, `commitMessage`. New tests in the core
test list.

**New `ui/qml/`:** `RewordDialog.qml`.

**Modified `ui/qml/`:**
- `CommitContextMenu.qml` — add *Reword…* item (gated to `isHead`).
- `HistoryPane.qml` — multi-select gestures; combined-diff routing; reword dialog.
- `CommitDetail.qml` — optional header (range label) + hint line.

**Modified `ui/`:**
- `asyncrepo.hpp/.cpp` — `rangeFiles`, `rangeDiff`, `rewordHead`, `commitMessage`
  pool wrappers.
- `repoviewmodel.hpp/.cpp` — `selectCommitRows` routing (resolves rows → oids,
  contiguity test), header/hint properties, `rewordHead`, `requestCommitMessage`.
- `repocontroller.hpp/.cpp` — async `rewordHead` + refresh cascade;
  `refreshRangeFiles`/`refreshRangeDiff` + `rangeFilesReady`/`rangeDiffReady`;
  `requestCommitMessage` + `commitMessageReady`.
- `ui/CMakeLists.txt` — register `RewordDialog.qml`.

**Modified spec:**
- [context-menus.md](context-menus.md) — add the *Reword…* row to the
  `CommitContextMenu` table and note multi-select range behaviour.
- [product.md](product.md) — History-tab note that a contiguous range shows a
  combined diff.

---

## 7. Shipped — the interactive-rebase engine

Reword-older, squash/fixup, and reorder shipped in
[rebase-interactive.md](rebase-interactive.md) (Plan 20). The multi-select model
and the commit context menu built here are the surfaces the engine hangs off.
