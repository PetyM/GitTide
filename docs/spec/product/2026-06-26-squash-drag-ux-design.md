# Squash / drag UX polish — design

| | |
|---|---|
| **Status** | `shipped` |
| **Date** | 2026-06-26 |
| **Related** | [2026-06-26-history-graph-tab-design.md](2026-06-26-history-graph-tab-design.md), [rebase-interactive.md](rebase-interactive.md), [2026-06-25-history-drag-squash-design.md](2026-06-25-history-drag-squash-design.md) |

## Problem

After shipping the history Graph tab + drag fix (Plan 25), a drag-to-squash of
two adjacent commits exposes three UX rough edges:

1. **The dragged element is barely visible.** `rowDrag` (`DragHandler`) has
   `target: null`, so nothing follows the cursor — the source row only dims to
   `opacity 0.7`. There is no clear "this is what you're dragging" affordance.
2. **The squash drops you into the interactive-rebase banner** and you must click
   **Continue** before anything else happens.
3. **You then want the commit-message editor directly** — for a squash the whole
   point is to write the combined message, so the extra Continue click is friction.

Issues 2 and 3 share one root: the engine pauses with `RebasePause::Message`
(combined message already prefilled), but the UI requires a manual Continue click
in `RebaseBanner` before `requestMessageEdit()` fires and the dialog opens.

## Goals

- A **floating chip** under the cursor during an armed drag, showing the dragged
  commit and whether the current drop is a move or a squash.
- On **any** interactive-rebase message pause (squash *or* reword step),
  **auto-open** the message-edit dialog — no Continue click first.

## Non-goals

- No `core/` / rebase-engine change. The engine already pauses at the right point
  and prefills the combined/reword message; this is purely a UI surfacing change.
- No change to conflict-pause handling (that still uses the banner + Continue).
- No new drag mechanics (hold-to-arm, three-band drop zones, `performDrop` routing
  all stay as-is).

## Design

### 1. Auto-open the message dialog on a message pause

Rebase state reaches the ViewModel through the lambda at
`ui/src/repoviewmodel.cpp:72`:
```cpp
connect(m_controller, &RepoController::rebaseStateChanged, this,
        [this](const gittide::RebaseState& s) { m_rebase = s; emit rebaseStateChanged(); });
```

- Add a one-shot signal `void rebaseMessagePauseEntered()` to `RepoViewModel`.
- In that lambda, detect the **rising edge** into a message pause: emit
  `rebaseMessagePauseEntered()` when the new state has `pause == Message` AND
  either the previous state's pause was not `Message` OR the step index
  (`s.current`) advanced since the last message pause. Track the previous
  `pause`/`current` in members (`m_lastRebasePause`, `m_lastRebaseStep`).
  `rebaseStateChanged()` is still emitted as before.
- The edge logic fires once per message step: an N-step interactive rebase with
  several reword/squash entries opens the dialog once per entry (after each
  `continueRebase`, the next message step is a fresh rising edge).

In `ui/qml/WorkingPane.qml`:
- Refactor the existing `RebaseBanner.onRequestMessageEdit` body (split prefill →
  summary/body, then `rebaseMessageDialog.open()`) into a single function
  `openRebaseMessageDialog()`.
- `RebaseBanner.onRequestMessageEdit` calls `openRebaseMessageDialog()` (manual
  fallback path, unchanged behaviour).
- Add `Connections { target: repoVm; function onRebaseMessagePauseEntered() {
  openRebaseMessageDialog() } }` so a message pause opens the dialog automatically.
- Dismissing/cancelling the dialog does **not** auto-reopen (the signal already
  fired for this step); the banner's Continue remains available to reopen it.

### 2. Floating drag chip

In `ui/qml/HistoryPane.qml`:
- `dropLogic` gains: `property string draggedSummary`, `property string
  draggedShortOid`, and `property point dragPos`.
- When the hold timer arms the drag (`holdTimer.onTriggered`, where `dragArmed`
  becomes true), set `dropLogic.draggedSummary = model.summary` and
  `draggedShortOid = model.shortOid` from the delegate's model.
- In `rowDrag.onCentroidChanged`, in addition to `updateDropTarget`, set
  `dropLogic.dragPos` to the centroid mapped into the list column's coordinate
  space (the `Item` that wraps `historyList`).
- Add an overlay `Item` (the **chip**) as a child of that wrapping `Item`, high
  `z`, `visible: <any row> dragArmed`. It renders a small card: the dragged
  commit summary (elided) + short oid, plus a hint `Label` that shows
  **"Move"** when `dropLogic.dropTargetZone` is `above`/`below` and
  **"◆ Squash"** when it is `squash` (and a muted state when there is no valid
  target). Position `x/y` from `dropLogic.dragPos` with a small offset so the chip
  sits next to the cursor, clamped to the column bounds.
- The source row keeps `opacity: dragArmed ? 0.7 : 1.0`. The existing
  insertion-line and squash-highlight target indicators are unchanged.
- Colours come from theme tokens (`surfaceRaised`, `border`, `accent`,
  `textPrimary`/`textMuted`).

`dragArmed` is per-delegate; expose a single "is a drag active" signal to the chip
by storing the armed state on `dropLogic` (e.g. set `dropLogic.dragActive = true`
when a row arms and `false` on release) so the chip's `visible` binds to one
property rather than reaching into delegates.

### 3. Data flow

```
Squash drag → performDrop("squash") → squashCommitInto → startInteractiveRebase
  → engine pauses RebasePause::Message (combined message prefilled)
  → RepoController::rebaseStateChanged → RepoViewModel lambda detects rising edge
  → rebaseMessagePauseEntered() → WorkingPane opens rebaseMessageDialog (prefilled)
  → user edits → continueRebase(message) → rebase finishes
```

### 4. Error handling

No new failure modes. If the dialog is cancelled, the rebase stays paused and the
banner's Continue still works. The rising-edge guard prevents repeated auto-opens
for the same step.

## Testing

- **ViewModel unit** (`tests/ui/`, QtTest, mirror `test_repoviewmodel_rebase.cpp`):
  drive `RebaseState` transitions through the controller signal and assert
  `rebaseMessagePauseEntered` fires exactly once on `no-pause → Message`, once
  again when the step advances to a new `Message` entry, and **not** on a
  `rebaseStateChanged` that is not a message-pause rising edge (e.g. a conflict
  pause or a progress update).
- **Chip structural** (`tests/ui/test_qml_history.cpp`): assert the chip `Item`
  exists, its `visible` is driven by `dropLogic.dragActive`, and `draggedSummary`
  is wired. The live drag cannot be synthesised headlessly (same constraint as the
  existing drag test) — note it as a manual smoke item.
- **Manual smoke:** drag-squash two adjacent commits → a chip follows the cursor
  showing the dragged commit + "◆ Squash" over the target → on drop the message
  dialog opens immediately (no Continue click) → save completes the squash.

## Risks / open points

- The rising-edge detection must not double-fire: a single message pause can emit
  `rebaseStateChanged` more than once (e.g. a refresh). Keying on
  `(pause==Message, current)` vs the last-seen values guards this.
- Chip positioning uses centroid coordinates mapped into the column; verify the
  mapping origin so the chip tracks the cursor rather than drifting.
