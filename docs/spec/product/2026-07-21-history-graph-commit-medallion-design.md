# History & Graph refinements + commit medallion — design

**Status:** shipped · **Date:** 2026-07-21 · **Plan:** [Plan 43](../../plans/2026-07-21-plan43-history-graph-medallion.md)

Three related UI refinements to the History tab, the Graph tab, and the
commit-detail panel, plus the small backend needed to feed the new panel.

## 1. History list — drop the hash

The History-tab row sub-line currently shows author · **short hash** · date. The
hash carries little value in the list (it lives in the detail panel now — see §3)
and adds noise.

- Remove the short-oid `Label` from the History row delegate
  (`HistoryPane.qml`, the `model.shortOid` cell). Author and date remain.
- The **Graph tab keeps its hash** — this change is History-tab only.

## 2. Graph — fixed columns, stacked refs

**Problem.** In the Graph delegate the layout is
`GraphColumn → ref chips → avatar → summary`. The ref chips (`Repeater` of
branch/tag labels) have variable total width, so the summary text starts at a
different X on every row — the "text jumps around" the user reported.

**Fix — put refs in a fixed-width column and stack them vertically.**

- Wrap the ref chips in a **fixed-width** column (`Layout.preferredWidth =
  kRefColW`, a new design constant, ~120px). When a commit has multiple refs they
  **stack vertically** (one chip per line) instead of extending horizontally.
- The row height becomes **dynamic**: `implicitHeight = max(kRowH,
  refCount * kChipH)`. Rows with 0–1 refs stay at the current 48px; a commit with
  N refs grows to fit them.
- Avatar + summary are **top-aligned** so the first line stays put as the row
  grows.
- The summary column therefore begins at a **constant X on every row**:
  `graphWidth + kRefColW`.

**GraphColumn painting under variable row height.** `GraphColumn` is a
`QQuickPaintedItem` that paints lanes/dot within its own item rect. Two
adjustments so tall rows still look right:

- Draw the commit **dot at a fixed offset from the top** (aligned with the first
  line / avatar) rather than at vertical centre, so it doesn't drift down on tall
  rows.
- Lane vertical segments fill the **full item height**, so connectors between
  adjacent rows stay continuous regardless of row height.

The incoming/outgoing edge geometry keys off the dot's (now fixed) Y instead of
`height()/2`.

## 3. Commit medallion (right panel)

Replace the bare `Commit <7hash>` header in `CommitDetail.qml` with a flat
medallion header, shown when a single commit is selected (range/stash modes keep
their existing headers). It carries **no frame of its own** — the framed
changed-files panel below (see §3.1) provides the visual divide, so a second
bordered card here would just compete with it. The metadata **spreads across the
full width** rather than hugging the left edge. Top-to-bottom:

- **Summary** — first line, bold, primary colour, wrapping; the **Checkout**
  button sits on the right of this row.
- **Body** — remaining message lines, muted, wrapped; hidden when empty.
- **Author** — a small circular **avatar** (`Avatar.qml`, keyed on the author
  email) + `name <email>` on the left, with the **author date pinned to the
  right edge** of the row. (Author only; no committer line.)
- **Stats + hash** — one row: `N files changed` · green `+additions` · red
  `−deletions` on the left; the short oid (mono) and a compact **copy icon**
  (`⧉`, an `AbstractButton` with hover surface + tooltip) pinned to the right.
  The icon copies the full 40-char oid via the existing
  `repoVm.copyToClipboard(...)` path.

Colours come from theme tokens (`theme.stateAdded` / `theme.stateDeleted` for the
+/− stats, `theme.textMuted` etc.) — no hex literals.

### 3.1 Changed-files panel framing

The changed-files list (top pane of the detail `SplitView`) reads as its own
framed section: a titled **`Changed files · N`** strip (`theme.surfaceOverlay`
background with a `theme.border` divider under it) above the list, and a
**persistent 1px `theme.border` frame** around the pane. (The frame is static —
it no longer brightens to `theme.focusBorder` on keyboard focus; per
[keyboard-controls.md §1.3](keyboard-controls.md) the navigable lists dropped
their section-wide focus rings in favour of the current-row highlight.)

## 4. Backend to feed the medallion

The panel currently has no source for body, author, or line stats. Add **one
combined async fetch** per selection (not three round-trips):

- **core** — `Expected<CommitDetail> GitRepo::commitDetail(std::string oid)`.
  `CommitDetail { std::string summary, body, authorName, authorEmail;
  int64_t authorTime; int filesChanged, additions, deletions; }`.
  Implemented with a commit lookup + the commit-vs-first-parent diff, reading
  `git_diff_get_stats` for `filesChanged / additions / deletions` and the commit
  signature/message for the rest. No Qt in core; returns `Expected`.
  **TDD:** core test first (a `TempRepo` with a known add/delete commit asserts
  the counts, summary/body split, and author).
- **controller** — `QCoro::Task<void> refreshCommitDetail(QString oid)` → emits
  `commitDetailReady(...)`, mirroring `refreshCommitFiles`.
- **viewmodel** — kick off `refreshCommitDetail` inside `selectCommit()` next to
  `refreshCommitFiles`. Cache the result into Q_PROPERTYs
  (`detailSummary`, `detailBody`, `detailAuthor`, `detailAuthorEmail`,
  `detailDate`, `detailFilesChanged`, `detailAdditions`, `detailDeletions`) with
  change signals, cleared on deselect. QML binds to these.
  (`RepoViewModel::changed()` fires only on open/close, so these need their own
  change signals emitted from the `onCommitDetail` slot.)

The existing `commitMessage(oid)` / `requestCommitMessage` path is left intact for
its current callers.

## Out of scope

- No hash removal from the Graph tab.
- No committer identity in the medallion.
- No new stats in the History/Graph list rows.

## Affected files (indicative)

- `ui/qml/HistoryPane.qml` — remove hash cell.
- `ui/qml/GraphPane.qml` — fixed ref column, stacked chips, dynamic row height.
- `ui/src/graphcolumn.cpp` / `graphcolumn.hpp` — top-anchored dot, full-height lanes.
- `ui/qml/CommitDetail.qml` — medallion card.
- `core/include/gittide/gitrepo.hpp` + `core/src/gitrepo.cpp` — `commitDetail`.
- `ui/src/repocontroller.cpp` (+ hpp) — `refreshCommitDetail` / `commitDetailReady`.
- `ui/src/repoviewmodel.cpp` (+ hpp) — detail Q_PROPERTYs + slot.
- `tests/` — core `commitDetail` test; UI headless coverage for the medallion.
