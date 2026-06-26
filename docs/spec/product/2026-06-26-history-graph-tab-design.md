# History redesign + full git-graph tab — design

| | |
|---|---|
| **Status** | `approved` |
| **Date** | 2026-06-26 |
| **Supersedes** | The in-history graph column from earlier history specs |
| **Related** | [rebase-interactive.md](rebase-interactive.md), [2026-06-25-history-drag-squash-design.md](2026-06-25-history-drag-squash-design.md) |

## Problem

The commit-history view crams three concerns into one column strip: a branch
graph, a draggable reorder/squash affordance, and the commit list. Three issues:

1. The in-list graph only walks **HEAD** (`git_revwalk_push_head`), so it is a
   near-linear strip, not a real multi-branch graph — it earns its column width
   poorly.
2. Per-row drag grips (`⠿`) clutter the right edge of every reorderable row.
3. **Drag-to-reorder/squash does not work at all** — a row cannot be dragged.

## Root cause of the broken drag

`HistoryPane.qml`'s delegate uses an `anchors.fill` **`MouseArea`** for
selection/right-click alongside a sibling **`DragHandler`** for the reorder
gesture. `MouseArea` takes an *exclusive* pointer grab on press; the
`DragHandler` never wins the grab, so it never activates and the row never
arms for drag. Plan 23's own tech-stack line named `TapHandler` — the
implementation regressed to `MouseArea`. `TapHandler` (a pointer handler)
cooperates with `DragHandler` via shared/passive grabs; `MouseArea` does not.

## Goals

- Move the branch graph out of the history list into its **own "Graph" tab** that
  renders a **full git graph of all refs** (local + remote branches + tags).
- Make the Graph tab **fully interactive**: select a commit → commit detail panel
  + right-click context menu, mirroring History.
- **Strip** the graph column and the `⠿` drag grips from the History list.
- **Fix** history drag-to-reorder/squash so a row can actually be dragged.

## Non-goals

- No change to `GraphBuilder` — it already computes lanes, pass-throughs, and
  multi-parent edges correctly; it only needs to be fed all-refs commits.
- Reorder/squash stays **History-only**. The Graph tab does not offer reorder or
  squash (cross-branch reorder is meaningless here).
- No new graph styling system; reuse the existing `GraphColumn` painter and lane
  colours.

## Design

### 1. Core — all-refs log

`GitRepo::log(limit)` currently does `git_revwalk_push_head`. Add a sibling:

```cpp
/// logAllRefs walks every branch and tag (refs/heads/*, refs/remotes/*,
/// refs/tags/*) topologically + by time, for the full-graph view. Same
/// CommitNode shape as log(), so GraphBuilder::build() consumes it unchanged.
Expected<std::vector<CommitNode>> logAllRefs(std::size_t limit = 0);
```

Implementation: `git_revwalk_new` → `GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME` →
`git_revwalk_push_glob(walk, "refs/heads/*")`, `"refs/remotes/*"`,
`"refs/tags/*"` → drain into `CommitNode`s exactly as `log()` does. Tag refs that
point at non-commit objects are skipped by the walker; that is acceptable.

Ref-tip labels: extend each tip `CommitNode` with the short ref names that point
at it (e.g. `main`, `origin/main`, `v1.0`). Provide via a parallel map
`oid → [labels]` computed by enumerating refs, OR a `std::vector<std::string>
refNames` field on `CommitNode` populated only for tips. Tip labels render as
chips in the Graph tab; non-tip commits carry none.

### 2. ViewModel — second history model

`RepoViewModel` already owns a `HistoryListModel` exposed as `history`. The Graph
tab reuses the **same model class** (it already carries `graphRow`, `laneCount`,
and all roles):

- Add a second instance exposed as `Q_PROPERTY … graph`.
- Add `Q_INVOKABLE void refreshGraph()` — calls `logAllRefs` →
  `GraphBuilder::build` → `graphModel.setLayout(...)`, the async-bridge twin of the
  existing history refresh.
- Hook `refreshGraph()` into the same live-refresh trigger as `refreshHistory()`
  so the graph stays current after commits/fetches.
- Add a `refLabels` role to `HistoryListModel` (list of strings; empty for
  non-tips) so the Graph delegate can draw branch/tag chips. History delegate
  ignores it.

### 3. UI — new `GraphPane.qml` + third tab

New `ui/qml/GraphPane.qml`, structurally a trimmed `HistoryPane`:

- `ListView { model: repoVm.graph }`, delegate = `GraphColumn` + ref-tip chips +
  avatar + summary/author/date.
- Reuse `CommitDetail` for the selected-commit panel (right side).
- Reuse `CommitContextMenu` for right-click, but wire **only** the cross-branch-safe
  actions: copy SHA, new branch from here, checkout commit, merge. **No** reword /
  squash-selected / edit-history / undo (those are History/tip operations).
- Selection: `TapHandler` (left = select, right = context menu). No drag, no grips.

`WorkingPane.qml`:

- Add a third `MainTab { text: "Graph" }` after History.
- Add a third `StackLayout` child: `GraphPane { objectName: "graphTabBody" }`.
- Extend `takeFocus`/`takeFocusLast` and the focus chain for index 2.
- Add `Ctrl+3` shortcut → `tabs.currentIndex = 2; graphTabBody.takeFocus()`.
- On first open / repo change, also kick `repoVm.refreshGraph()` (lazily on first
  switch to the Graph tab is acceptable to avoid cost when unused).

### 4. History — strip graph, fix drag

`HistoryPane.qml` delegate:

- **Remove** the `GraphColumn` block (current lines 223–231).
- **Remove** the `reorderGrip` `Label` block (current lines 274–287). The accent
  left-border selection marker stays.
- **Replace** the whole-row `MouseArea` with pointer handlers that cooperate with
  the existing `DragHandler`:
  - `TapHandler { acceptedButtons: Qt.LeftButton }` — selection. Read modifiers via
    `point.modifiers` to keep Shift-range / Ctrl-toggle / plain-click selection
    behaviour identical to today. Call `historyList.forceActiveFocus()` on tap.
  - `TapHandler { acceptedButtons: Qt.RightButton }` — context menu, same payload
    population + `commitMenu.popup()` as today, including the keep-multi-selection
    rule.
- Keep `DragHandler rowDrag` (250 ms hold-arm), `dropZoneAt`, `updateDropTarget`,
  `performDrop`, and the reorder/squash indicators **unchanged** — they now arm
  because no `MouseArea` steals the grab.

### 5. Data flow

```
Graph tab:  logAllRefs → GraphBuilder::build → HistoryListModel(graph)
            → GraphPane ListView (GraphColumn + chips) → CommitDetail / CommitContextMenu
History tab: log (HEAD) → GraphBuilder::build → HistoryListModel(history)
            → HistoryPane list (no graph col) + DragHandler reorder/squash → CommitDetail
```

### 6. Error handling

`logAllRefs` returns `Expected<…>`; a failure surfaces through the same async-bridge
error path as `log`. An empty repo / detached HEAD yields an empty graph (no rows),
not an error. Tag refs to non-commits are silently skipped by the walker.

## Testing

- **core** (`TempRepo`): create commits on two branches + a tag; assert
  `logAllRefs` returns commits from **both** branch tips and the tag is reachable,
  whereas `log` (HEAD) returns only the current branch's line.
- **core**: ref-tip labels — assert the tip `CommitNode`s carry the expected branch
  / tag names and interior commits carry none.
- **qml** (`test_qml_history` / new `test_qml_graph`): Graph tab exists; selecting a
  row populates `CommitDetail`; right-click opens the context menu.
- **qml**: History drag now **arms and drops** — simulate hold + move + release,
  assert `performDrop` routes to reorder/squash. Keep the existing pure
  `dropZoneAt` band test.
- **qml**: History delegate no longer instantiates `GraphColumn` or `reorderGrip`
  (findChild returns null).

## Risks / open points

- `Ctrl+3` and the focus chain must stay consistent with the existing
  `Ctrl+1`/`Ctrl+2` wiring; off-by-one in `takeFocus` indexing is the likely bug.
- Lazy vs eager `refreshGraph`: eager is simpler but pays the all-refs walk even
  when the tab is never opened. Default: refresh on first switch to the Graph tab,
  then on the live-refresh trigger while it stays open.
- Ref-label plumbing touches `CommitNode` (core struct) — keep it `std` only
  (vector<string>), no Qt, per the core invariant.
