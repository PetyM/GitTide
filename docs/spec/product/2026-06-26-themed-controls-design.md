# Themed controls — AppButton + AppComboBox — design

| | |
|---|---|
| **Status** | `approved` |
| **Date** | 2026-06-26 |
| **Related** | [spec/design/design.md](../design/design.md) (visual system + tokens) |

## Problem

The app themes most controls, but two control types have **no shared themed
component**, so they fall back to the Qt Quick Controls **Basic** default style —
light, un-themed elements on the dark UI:

- **Buttons.** Every dialog footer uses a bare `Button { text; onClicked }`
  (Close / Cancel / Save / Delete across ~15 dialogs) → default Basic buttons.
  A handful of in-pane buttons are themed *inline* (custom `contentItem` +
  `background`), duplicated per call site. There is no `AppButton`.
- **ComboBoxes.** Three raw `ComboBox` (DeleteBranchDialog, NewBranchDialog,
  RebaseTodoDialog) render the default light dropdown. DeleteBranchDialog reads
  especially "off" because of this.
- **The submodule Init affordance** (`Sidebar.qml`) is a bare `ToolButton {
  text:"Init" }` — default style, and its padding overflows the sidebar row.

`TextField`/`TextArea` are already themed inline everywhere and look correct;
they are **out of scope** (deduping them into an `AppTextField` is separate,
optional polish, not part of this complaint).

## Goals

- One themed `AppButton` (variants: `primary` / `secondary` / `danger`, plus a
  `compact` size) replacing every plain action button.
- One themed `AppComboBox` replacing the three raw combos.
- The Sidebar Init affordance becomes a compact themed pill that fits the row.
- Pure QML restyle: **no C++/behaviour change; every `objectName` preserved** so
  the existing test suite keeps passing.

## Non-goals

- No `AppTextField` (TextFields already themed).
- No change to specialized controls: `MainTab` (TabButton), `WindowButton`
  (title-bar chrome), `AppRadioButton`/`AppCheckBox`, `EmptyState.Cta` (a
  deliberately distinct large empty-state CTA). These are not plain action
  controls and keep their bespoke styling.
- No new theme tokens — reuse the existing palette.

## Design

### 1. `AppButton.qml`

A `Button` (QtQuick.Controls.Basic) subclass:

- `property string variant: "primary"` — `primary` | `secondary` | `danger`.
- `property bool compact: false` — smaller height/font/padding for inline use.
- `contentItem`: a `Label` (centered, elide none) coloured per variant/state.
- `background`: a `Rectangle { radius: 6 }` coloured per variant/state:
  - **primary** — fill `theme.accent`; hover `theme.accentHover`; disabled
    `theme.surfaceOverlay`. Text `theme.surfaceBase` (disabled `theme.textMuted`).
  - **secondary** — transparent fill + `theme.border`; hover fill
    `theme.surfaceOverlay`. Text `theme.textPrimary` (disabled `theme.textMuted`).
  - **danger** — fill `theme.stateDeleted`; hover a darker red (e.g.
    `Qt.darker(theme.stateDeleted, 1.2)`); disabled `theme.surfaceOverlay`. Text
    `theme.surfaceBase`.
- Sizing: default `implicitHeight ≈ 30`, font 12, horizontal padding 14.
  `compact` → `implicitHeight ≈ 22`, font 11, padding 8.
- `objectName`, `text`, `enabled`, `onClicked`, `Layout.*` all pass through (it
  *is* a Button), so existing test hooks and layouts survive unchanged.
- Registered in `ui/qml/qml.qrc`.

### 2. `AppComboBox.qml`

A `ComboBox` (QtQuick.Controls.Basic) subclass, themed end-to-end:

- `contentItem`: a `Label` showing `displayText`, `theme.textPrimary`, left-padded.
- `background`: `Rectangle { radius: 6; color: theme.surfaceBase;
  border.color: activeFocus/pressed ? theme.accent : theme.border }`.
- `indicator`: a small themed chevron (`theme.textMuted`).
- `popup`: `background` = `Rectangle { color: theme.surfaceRaised;
  border.color: theme.border; radius: 6 }` (or reuse `OverlayCard` if it fits);
  `contentItem` a themed `ListView` with `AppScrollBar`.
- `delegate`: `ItemDelegate` whose `contentItem` Label is `theme.textPrimary` and
  whose `background` highlights to `theme.surfaceOverlay` on hover/selection.
- `objectName`/`model`/`currentIndex`/`currentText`/signals pass through.
- Registered in `ui/qml/qml.qrc`.

### 3. Migration

- **Dialog footers** (Clone, Credential, DeleteBranch, Discard, Reword,
  RenameBranch, RebaseTarget, ReorderConfirm, NewProject, NewBranch, Options,
  About, Main delete-project, InitRepo, CloneProgress, RebaseTodo): Cancel/Close →
  `AppButton variant:"secondary"`; confirm (Save/Initialize/Clone/Create/OK) →
  `variant:"primary"`; destructive (Delete/Discard) → `variant:"danger"`. Keep
  every `objectName`/`enabled`/`onClicked`/`text` binding verbatim.
- **In-pane themed buttons** → `AppButton` too (single source of truth):
  `commitButton` (primary, `Layout.fillWidth`), `checkoutCommitButton`
  (secondary), `fetchAllButton`/`addRepoButton` (Sidebar), `DiffView` action
  buttons, RebaseTodoDialog buttons. Preserve each button's current weight
  (filled vs outline) by variant choice and its `objectName`.
- **ComboBoxes** → `AppComboBox`: DeleteBranchDialog, NewBranchDialog,
  RebaseTodoDialog. Preserve `objectName`/`model`/`currentIndex` and the
  `onCurrentTextChanged`/`onCurrentText…` handlers.
- **Submodule Init** (`Sidebar.qml` `ToolButton`) →
  `AppButton { variant:"primary"; compact:true; text:"Init" }` sized to the row;
  same `visible`/`onClicked`.
- **DeleteBranchDialog** ends fully themed: `AppComboBox` + Cancel
  (`secondary`) + the arm-on-first-click confirm (`danger`, keeping the
  "Delete" → "Click again to delete" / "Force delete" text states + behaviour).

### 4. Data flow / behaviour

None changes. `AppButton`/`AppComboBox` are presentation wrappers; all click
handlers, model bindings, enable conditions, and the delete-confirm arming logic
stay exactly as today.

## Testing

- **`AppButton` load test** (QtTest): instantiates for each variant without QML
  errors; `compact:true` yields a smaller `implicitHeight` than default;
  `objectName`/`enabled`/`text` pass through to the underlying Button.
- **`AppComboBox` load test** (QtTest): instantiates; `model`/`currentIndex`/
  `currentText` behave as a ComboBox; popup delegate renders.
- **Regression by existing suite:** migration preserves every `objectName`
  (`commitButton`, `rewordSave`, `checkoutCommitButton`, `fetchAllButton`,
  `addRepoButton`, `initRepoCreate`, `deleteBranchConfirm`, `deleteBranchTarget`,
  `newBranchName`, …). The full UI suite must stay green — it is the migration's
  safety net.
- **Manual smoke:** open each dialog → all buttons + combos themed (no light
  Basic controls); DeleteBranchDialog reads on-theme; sidebar uninitialised
  submodule → compact Init pill fits the row.

## Risks / open points

- **`objectName` drift** is the main risk: a migrated button/combo that loses or
  renames its `objectName` silently breaks a test and a context hook. The
  migration must copy each `objectName` verbatim — call it out per task.
- **Popup theming** for `AppComboBox` is the fiddliest part (Basic popup +
  delegate); if `OverlayCard` doesn't compose cleanly as the popup background,
  use a plain themed `Rectangle`.
- **`DialogButtonBox` vs custom footer:** all migrated footers already use a
  custom `RowLayout` footer, not `DialogButtonBox`, so swapping the `Button`
  type is local and low-risk.
