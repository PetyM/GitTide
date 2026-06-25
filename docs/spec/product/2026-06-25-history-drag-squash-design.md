# History drag gestures — whole-row long-press + drag-to-squash

| | |
|--|--|
| **Designed** | 2026-06-25 |
| **Status** | `shipped` |
| **Extends** | [rebase-interactive.md](rebase-interactive.md) §3.2 ("Reorder directly in the history view"), **D36** |
| **Touches** | ui: `HistoryPane.qml` delegate gesture rework, drop-zone hit-test + indicators; new `RepoViewModel::squashCommitInto`; reuse `RewordDialog` message pause. core: none (engine already squashes + pauses). |

## Problem

The history-view drag-to-reorder (rebase-interactive §3.2, D36) is hard to grab:
the only drag handle is the 16 px `⠿` grip at the row's right edge. Users miss it
and conclude drag "doesn't work". Separately, **squash** is reachable only via
multi-select + context menu ("Squash N commits…"); there is no direct-manipulation
path for it the way there is for reorder.

This round makes the **whole commit row** a drag source behind a short
press-and-hold, and adds **drag-to-squash** by reusing the same gesture with a
drop-zone that distinguishes the two operations.

## Scope

**In:**

- Whole-row drag in the history view, armed by a **250 ms press-and-hold**; a
  quick click still selects (no accidental reorder).
- A three-band drop zone on the target row: top third → reorder above, bottom
  third → reorder below, **middle third → squash into target**.
- Drag-to-squash: fold the dragged commit into the drop target via the interactive
  engine, pausing on the existing combined-message editor (`RewordDialog`).
- Distinct live drop indicators (insertion line vs. squash highlight) so the user
  sees which operation will fire before releasing.

**Out (unchanged / deferred):**

- The todo-editor (`RebaseTodoDialog`) drag — keeps its grip-only drag; this round
  touches only the **history view**.
- Fixup (message-discarding) via drag — drag-squash always opens the message editor
  (combined message). Fixup stays available through the todo editor.
- Cross-merge / root drag — still blocked; both source and target must lie in the
  **reorderable run** (`reorderableRunLength`).
- Already-pushed warning — still deferred to network-sync (per D36).

## 1. Gesture model (`HistoryPane.qml` delegate)

The delegate currently selects on click (`TapHandler`) and drags only from the
`reorderGrip` `DragHandler`. Rework:

- **Quick click** → select (existing single/Shift/Ctrl selection, unchanged).
- **Press-and-hold ≥ 250 ms** on any part of a row in the reorderable run → the row
  **arms** for drag: it "lifts" (opacity ≈ 0.7 + a subtle accent border / shadow
  via theme tokens), then follows the cursor vertically (`xAxis.enabled: false`,
  matching the existing reorder constraint).
- Release → resolve against the drop zone under the cursor (§2), then snap back.

Implementation note: a `DragHandler` carries no built-in hold delay. Arm it with a
companion `TapHandler { longPressThreshold: 0.25 }` (or a `Timer` started on press)
that enables the drag only after the hold elapses; a release before the threshold
falls through to the normal select tap. Keep the gesture **gated** to
`index < reorderableRunLength` exactly as today — rows outside the run neither lift
nor drag.

The `⠿` grip **stays** as a discoverability affordance (hover tooltip "Drag to
reorder or squash"), but is no longer the only target — the whole row is live.

## 2. Drop zones & disambiguation

On release, map the cursor Y into the target row's local height and split into
thirds:

| Band (target row) | Operation | Indicator |
|--|--|--|
| **Top third** | reorder: insert dragged commit **above** target | 2 px `accent` insertion line at the row's top edge |
| **Bottom third** | reorder: insert **below** target | 2 px `accent` insertion line at the row's bottom edge |
| **Middle third** | **squash** dragged commit **into** target | target row filled `surfaceOverlay` + a "◆ squash" badge |

Guards (a drop that fails any → no-op, snap back):

- Target index must be `< reorderableRunLength` (in the run) and `!= source`.
- Indicators update **live** during the drag (hit-test on every position change), so
  the user sees reorder-line vs. squash-fill before releasing — the operation is
  never ambiguous at the moment of drop.

Reorder keeps its existing confirmation (`ReorderConfirmDialog` →
`reorderCommits(fromRow, toRow)`), unchanged. Squash routes to a new VM method (§3);
its message editor is the confirmation gate, so no separate confirm dialog.

## 3. Drag-to-squash wiring (QML → ViewModel → engine)

New `Q_INVOKABLE void RepoViewModel::squashCommitInto(int fromRow, int toRow)`,
sibling to the existing `reorderCommits`. It builds an interactive-rebase plan that
folds the dragged commit (`fromRow`) into the target (`toRow`), then drives the
engine:

1. Validate: both rows in `[0, reorderableRunLength)`, `fromRow != toRow`,
   `reorderableRunLength >= 2`. Else return (no-op), matching `reorderCommits`.
2. Build the run **newest-first** from `m_lastLayout.rows` (same as
   `reorderCommits`); `base` = first parent of the run's deepest (oldest) commit.
3. **Reorder so the dragged commit sits immediately newer-adjacent to the target**,
   then mark it `squash`. Concretely, in the **oldest-first** plan handed to the
   engine: emit each run commit as `pick`, except place the dragged oid directly
   **after** the target oid and label it `squash`. The engine folds a `squash`
   entry into the preceding kept commit (rebase-interactive §2.5), so the dragged
   commit folds into the target; the combined commit takes the **target's**
   position. All other entries are `pick`.
4. `m_controller->startInteractiveRebase(base, actions, oids)`. The engine pauses at
   the squash for the **combined message**, surfacing the existing `RewordDialog`
   pre-filled with both messages (target's then dragged's), per
   rebase-interactive §2.5 / §3.5. User edits/confirms → engine completes and the
   refresh cascade runs.

This reuses the manual engine (D34) and the message-pause surface verbatim — no
core change. Direction is fixed: **dragged folds into target**, result keeps the
target's identity/slot.

## 4. Visual feedback (theme tokens, no hex)

- **Lifted source row:** opacity ≈ 0.7 + `accent` 1 px border (or low-elevation
  shadow). Tokenised; per the affordance rule (D19) it is not a colour-only state —
  the lift + motion carry it.
- **Reorder indicator:** 2 px `accent` insertion line, top or bottom edge of the
  hovered row.
- **Squash indicator:** `surfaceOverlay` row fill + "◆ squash" label, visibly
  different from the reorder line.

## 5. Tests (headless QML runner + VM unit)

- **Quick tap selects, does not drag** — a press/release under the hold threshold
  selects the row and fires **neither** `reorderCommits` nor `squashCommitInto`.
- **Hold arms drag** — press ≥ 250 ms sets the lifted state.
- **Drop middle third → squash** — releasing over a target's middle band calls
  `squashCommitInto(from, to)` with the right rows.
- **Drop top/bottom third → reorder** — releasing over the outer bands opens
  `ReorderConfirmDialog` / calls `reorderCommits` (existing path) with the correct
  insert position.
- **Drop outside run / onto self** → no-op.
- **VM unit `squashCommitInto`** builds the correct oldest-first plan: all `pick`
  except the dragged oid placed immediately after the target oid as `squash`; `base`
  = parent of the oldest run commit.

## 6. Files

**Modified `ui/qml/`:**
- `HistoryPane.qml` — delegate gesture rework (whole-row hold-to-drag, lifted
  state); three-band drop-zone hit-test + live reorder/squash indicators; route
  middle-band drops to `squashCommitInto`.

**Modified `ui/`:**
- `repoviewmodel.hpp/.cpp` — add `squashCommitInto(int fromRow, int toRow)`.

**Modified spec:**
- [rebase-interactive.md](rebase-interactive.md) §3.2 — replace the grip-only
  description of "Reorder directly in the history view" with the whole-row
  hold-to-drag + drop-zone (reorder vs. squash) model; reference the new
  `squashCommitInto`.
- [decisions.md](../../decisions.md) — **D38**: whole-row long-press drag + drop-zone
  disambiguation (reorder thirds vs. squash middle); why hold-to-drag over grip-only
  and over a modifier key; drag-squash opens the combined-message editor (no fixup
  via drag).

**No `core/` change** — the interactive engine already squashes and pauses for the
combined message.
