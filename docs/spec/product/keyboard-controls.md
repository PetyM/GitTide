# Keyboard Controls

| | |
|--|--|
| **Designed** | 2026-06-23 |
| **Status** | `spec` |
| **Wishlist** | [docs/wishlist/keyboard-controls.md](../../wishlist/keyboard-controls.md) |
| **Touches** | ui: QML `Keys`/`FocusScope`/`Shortcut` wiring on existing models; design: `focusBorder` token; **not** core |

## Overview

Drive the review-and-commit loop entirely from the keyboard. Scope is navigation +
activation of flows that already exist — no new git capability. Pure `ui/` work.

---

## 1. Focus model

### 1.1 Changes tab

```
fileList  →(Tab)→  commitSummary  →(Tab)→  commitDescription  →(Tab)→  fileList
```

- `fileList.activeFocusOnTab: true` enters the Tab chain.
- Focus lands on `fileList` when a repo first opens (via `Connections` on
  `repoVm.repoOpen` → `fileList.forceActiveFocus()`, only when no text field is
  already active).
- The diff pane is excluded — read-only today, nothing to activate.

### 1.2 History tab

```
historyList  →(Tab)→  commitFilesList  →(Tab)→  historyList
```

- `historyList` receives focus when the History tab is activated.
- `commitFilesList` (in `CommitDetail`) is included; navigating it calls
  `selectCommitFile`. The read-only diff list below it is excluded.

### 1.3 Focus-ring affordance

New theme token **`focusBorder`** — resolved to `accent` in both dark and light
themes. `fileList`, `historyList`, and `commitFilesList` gain a 1 px outer border
whose colour is `theme.focusBorder` when `activeFocus` is true, transparent
otherwise. Border sits on a thin `Rectangle` wrapper around the `ListView` so the
list content is not inset.

---

## 2. Keyboard shortcuts

| Key | Context | Action |
|-----|---------|--------|
| ↑ / ↓ | `fileList` focused | Move selection; call `repoVm.selectFile(path)` |
| Space | `fileList` focused | Toggle stage/unstage: `repoVm.setFileChecked(index, …)` |
| ↑ / ↓ | `historyList` focused | Move selection; call `repoVm.selectCommit(oid)` |
| ↑ / ↓ | `commitFilesList` focused | Move selection; call `repoVm.selectCommitFile(path)` |
| Tab | Changes tab | Cycle: `fileList` → `commitSummary` → `commitDescription` → back |
| Tab | History tab | Cycle: `historyList` → `commitFilesList` → back |
| Ctrl/Cmd+Enter | `commitSummary` or `commitDescription` focused | Commit (when button enabled) |
| Ctrl+1 | window | Switch to Changes tab; focus `fileList` |
| Ctrl+2 | window | Switch to History tab; focus `historyList` |
| Ctrl+R | window | Refresh: `repoVm.refreshHistory()` (status refresh is already triggered by the controller) |
| ? | window (no text input focused) | Toggle shortcuts overlay |

### 2.1 Ctrl/Cmd+Enter commit wiring

Handled via `Keys.onReturnPressed` inside both `commitSummary` and
`commitDescription` — not via a `Shortcut` item. This avoids conflicts with
TextArea's own Enter handling:

```qml
// In commitSummary (TextField)
Keys.onReturnPressed: {
    if ((event.modifiers & Qt.ControlModifier) && commitButton.enabled) {
        repoVm.commit(commitSummary.text, commitDescription.text)
        event.accepted = true
    }
}
// In commitDescription (TextArea)
Keys.onReturnPressed: {
    if ((event.modifiers & Qt.ControlModifier) && commitButton.enabled) {
        repoVm.commit(commitSummary.text, commitDescription.text)
        event.accepted = true
    }
    // else: default TextArea behaviour inserts newline
}
```

On macOS Qt maps `Qt.ControlModifier` to the Command key via `StandardKey`
conventions; no platform guards needed here.

### 2.2 Global `Shortcut` items

Ctrl+1, Ctrl+2, Ctrl+R, and `?` are declared as `Shortcut` items at `WorkingPane`
level. Because `fileList` and `historyList` are private to their respective pane
components, each pane exposes a `function takeFocus()` that forwards focus to its
primary list. `WorkingPane` calls these:

```qml
// In ChangesPane.qml
function takeFocus() { fileList.forceActiveFocus() }

// In HistoryPane.qml
function takeFocus() { historyList.forceActiveFocus() }
```

```qml
// In WorkingPane.qml
Shortcut {
    sequence: "Ctrl+1"
    enabled: repoVm && repoVm.repoOpen
    onActivated: { tabs.currentIndex = 0; changesTabBody.takeFocus() }
}
Shortcut {
    sequence: "Ctrl+2"
    enabled: repoVm && repoVm.repoOpen
    onActivated: { tabs.currentIndex = 1; historyTabBody.takeFocus() }
}
Shortcut {
    sequence: "Ctrl+R"
    enabled: repoVm && repoVm.repoOpen
    onActivated: repoVm.refreshHistory()
}
Shortcut {
    sequence: "?"
    context: Qt.WindowShortcut
    enabled: repoVm && repoVm.repoOpen && !anyTextInputActive
    onActivated: shortcutsPopup.visible ? shortcutsPopup.close() : shortcutsPopup.open()
}
```

`anyTextInputActive` is a `readonly property bool` in `WorkingPane` that checks
`changesTabBody.commitSummaryActive || changesTabBody.commitDescriptionActive`.
`ChangesPane` exposes these as `readonly property bool` aliases on its text fields'
`activeFocus`.

Clicking a tab does not call `takeFocus()` — the Tab key and mouse are independent
paths. If focus-on-click-tab is desired, add an `onCurrentIndexChanged` handler in
`WorkingPane` calling the appropriate `takeFocus()`. This is deferred; the shortcut
path covers the keyboard-first use case.

### 2.3 List key handling pattern

Each list follows the same pattern (shown for `fileList`). Because key handlers
live on the `ListView` (not a delegate), they cannot access `model.get()` on a
C++ `QAbstractItemModel`. Two new `Q_INVOKABLE` methods are added to
`RepoViewModel` to avoid this:

| Method | Implementation |
|--------|---------------|
| `void selectFileAtRow(int row)` | Reads `filePath` from `changedFiles` at `row`; calls `selectFile` |
| `void selectCommitAtRow(int row)` | Reads OID via `OidRole` from `history` at `row`; calls `selectCommit` |
| `void selectCommitFileAtRow(int row)` | Reads `filePath` from `commitFiles` at `row`; calls `selectCommitFile` |

`selectCommitAtRow` reads the OID internally via `HistoryListModel::OidRole` so QML key handlers
don't need to access delegate properties from the list's `currentItem`.

```qml
ListView {
    id: fileList
    focus: true
    activeFocusOnTab: true
    KeyNavigation.tab: commitSummary   // closes the Tab cycle
    Keys.onUpPressed: {
        if (currentIndex > 0) {
            currentIndex--
            if (repoVm) repoVm.selectFileAtRow(currentIndex)
        }
    }
    Keys.onDownPressed: {
        if (currentIndex < count - 1) {
            currentIndex++
            if (repoVm) repoVm.selectFileAtRow(currentIndex)
        }
    }
    Keys.onSpacePressed: {
        if (currentIndex >= 0 && repoVm)
            repoVm.setFileChecked(currentIndex, currentItem.checkState !== 2)
    }
}
```

`historyList` exposes `property string oid: model.oid` in its delegate and calls
`repoVm.selectCommit(currentItem.oid)`. `commitFilesList` uses `selectCommitFileAtRow`.
`commitDescription` sets `KeyNavigation.tab: fileList` to close the Changes Tab cycle.

---

## 3. Shortcuts overlay (`ShortcutsHelpPopup.qml`)

A new `Popup` component instantiated once in `WorkingPane`. Uses the existing
`OverlayCard` as its background (consistent with dialogs). Fixed size ~360 × 280 px,
centered in the working pane.

Layout: title row "Keyboard shortcuts" + a two-column grid of key chips and action
labels. Key chips are small `Rectangle`s with `border.color: theme.border` and a
monospace `Label`. The table lists all bindings from §2.

Press `?` or `Escape` to close. `closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside`.

No new theme tokens beyond `focusBorder` (§1.3).

---

## 4. Scope exclusions

| Item | Reason |
|------|--------|
| Vim-style modal editing | Scope creep; first-cut ships a fixed default set |
| User-rebindable keymap | Natural follow-on once the focus model is stable |
| Command palette | Separate wish; requires search infrastructure |
| Diff pane keyboard navigation | No keyboard actions on read-only diff today |
| Enter in `commitSummary` moves focus to description | Enter has no special meaning in a single-line `TextField`; Tab suffices |

---

## 5. Files changed

**New `ui/qml/`:**
- `ShortcutsHelpPopup.qml`

**Modified `ui/qml/`:**
- `ChangesPane.qml` — `Keys` handlers on `fileList`; Tab order; Ctrl+Enter on commit fields; `ShortcutsHelpPopup` wiring
- `HistoryPane.qml` — `Keys` handlers on `historyList`; Tab order to `commitFilesList`
- `CommitDetail.qml` — `Keys` handlers on `commitFilesList`; `activeFocusOnTab: true`
- `WorkingPane.qml` — global `Shortcut` items; `anyTextInputActive` guard; focus handoff on tab switch; `ShortcutsHelpPopup` instance

**Modified `ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp`:**
- Add `Q_INVOKABLE void selectFileAtRow(int row)`
- Add `Q_INVOKABLE void selectCommitAtRow(int row)`
- Add `Q_INVOKABLE void selectCommitFileAtRow(int row)`

**Modified `ui/include/gittide/ui/theme.hpp` + `ui/src/theme.cpp`:**
- Add `focusBorder` token (= `accent` in both themes)

**Modified `ui/CMakeLists.txt`:**
- Register `ShortcutsHelpPopup.qml`
