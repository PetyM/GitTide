# App Menu, Custom Title Bar, and Options Dialog

| | |
|--|--|
| **Designed** | 2026-06-23 |
| **Status** | `spec` |
| **Wishlist** | [docs/wishlist/app-menu.md](../../wishlist/app-menu.md) |
| **Touches** | window chrome, settings persistence, theme, pull default |

## Overview

GitTide gains a custom frameless title bar with an app icon menu on the left,
replacing the native OS decorations. Behind it sits an **Options** dialog that
consolidates theme and pull-default settings in one place. App settings persist
via `QSettings` (platform-native storage). The window launches maximised with a
minimum size.

---

## 1. Settings persistence

App-level preferences (theme mode, pull default) are stored via **`QSettings`**
with organisation `"gittide"` and application `"gittide"`. This is a Qt
mechanism — no code in `core/`; `QSettings` lives entirely in `ui/` and `app/`.

**Keys:**

| Key | Type | Default | Values |
|-----|------|---------|--------|
| `themeMode` | `int` | `0` (System) | 0 System · 1 Dark · 2 Light |
| `pullRebase` | `bool` | `true` | true = rebase · false = merge |

**Startup (C++ in `qml_main.cpp`):**

```cpp
QSettings s;
theme.setMode(static_cast<ThemeManager::Mode>(s.value("themeMode", 0).toInt()));
```

This must happen before the QML engine loads to avoid a flash of the wrong theme.

**From QML:** a `Settings { }` block (from `QtCore`) declared at the
`Main.qml` level acts as the single live store. `OptionsDialog` reads and writes
it. Any write is auto-persisted by Qt immediately.

**Window geometry** is also persisted via the same `Settings` block
(`x`, `y`, `width`, `height`, `visibility`). Default-maximised applies only on
first launch (when no stored geometry exists).

### Pull default → RepoViewModel wiring

`Settings.pullRebase` is propagated to `RepoViewModel` from QML:

```qml
// in Main.qml
Component.onCompleted: repoVm.applyPullDefault(appSettings.pullRebase)
Connections {
    target: appSettings
    function onPullRebaseChanged() { repoVm.applyPullDefault(appSettings.pullRebase) }
}
```

`RepoViewModel` gets a new `Q_INVOKABLE void applyPullDefault(bool rebase)` that
sets `m_pullRebase` without touching git config.

**Removed behaviour:**
- `RepoViewModel::loadPullStrategy()` is no longer called on repo open.
- `RepoViewModel::setPullRebase(bool)` (the old git-config writer) is removed.
- The `⋯` pull-strategy button in `BranchBar.qml` is removed.
- The theme-toggle button in `Sidebar.qml` header is removed.

---

## 2. Frameless window + TitleBar

### Window flags

`Main.qml` sets:

```qml
flags: Qt.FramelessWindowHint | Qt.Window
minimumWidth: 860
minimumHeight: 560
```

Geometry and visibility are restored from `Settings` on startup. On first launch
(no stored geometry), the `ApplicationWindow` default `visibility: Window.Maximized`
takes effect. Subsequent launches restore the last saved state.

### TitleBar.qml

New component, height 40 px, `color: theme.surfaceRaised`. Layout differs by
platform:

```
macOS:    [● ● ●]  [icon]  [──────── drag area ────────]
Win/Lin:  [icon]   [──────── drag area ────────]  [─] [□] [✕]
```

**Platform flag** (read-only):
```qml
readonly property bool isMac: Qt.platform.os === "osx"
```

**Drag:**
```qml
DragHandler {
    target: null
    onActiveChanged: if (active) window.startSystemMove()
}
```

**Double-click to maximise/restore:**
```qml
TapHandler {
    numberOfTapsRequired: 2
    onTapped: window.visibility === Window.Maximized
              ? window.showNormal() : window.showMaximized()
}
```

**App icon button:** 32×32 px, left side (Win/Linux) or after traffic lights
(macOS). Click opens `appMenuPopup`.

**Window controls — macOS** (left side, before icon):
Colored circles: close `#FF5F56`, minimise `#FFBD2E`, maximise `#27C93F`.
Calls `window.close()`, `window.showMinimized()`, toggle max.

**Window controls — Windows / Linux** (right side):
Flat buttons with Unicode glyphs `─` `⬜` `✕`. Same window calls.

### WindowButton.qml

Reusable 40×40 px button used by both control sets. Properties:
`color` (fill), `hoverColor`, `glyph: string`. Background is a `Rectangle`
with `radius: 20` on macOS (circle), `0` on Win/Linux.

### Edge resize

Seven resize zones anchored to window edges (3 px wide/tall): bottom + left +
right + four corners. The top edge is the title bar's drag area; no separate
top-resize zone. Each zone uses:

```qml
DragHandler {
    target: null
    onActiveChanged: if (active) window.startSystemResize(edge)
}
```

Defined once in `Main.qml` via a helper component `EdgeResizer.qml` (parameterised
by `edges: Qt.LeftEdge` etc.). Does not apply when maximised.

### Main.qml layout change

```
ApplicationWindow (frameless)
  ColumnLayout (fills parent)
    TitleBar               ← new
    RowLayout              ← existing sidebar + working pane
      Sidebar
      WorkingPane
```

---

## 3. App menu popup

Opens on app-icon click. Implemented as `AppMenu` (existing component) anchored
below the icon button:

```
Options…
About GitTide
Rebase current branch…
─────────────
Quit
```

`Quit` calls `Qt.quit()`. `Rebase current branch…` opens `RebaseTargetDialog` (a local branch picker; emits `accepted(ref)` → `repoVm.startRebase(ref)`).

---

## 4. OptionsDialog.qml

Modal dialog, `OverlayCard` background, width 360 px. No OK/Cancel — settings
apply instantly on each change (live). Footer has a single **Close** button.

### Layout

```
Theme
  ○ System   ○ Dark   ○ Light

Pull default
  ○ Merge (fast-forward)   ○ Rebase

                           [Close]
```

Radio-group behaviour: `ButtonGroup` containing three `AppRadioButton` items per
section.

On `AppRadioButton` check:
- **Theme** → `theme.setMode(value)` + `appSettings.themeMode = value`
- **Pull** → `appSettings.pullRebase = value` (propagates to RepoViewModel via
  the `Main.qml` `Connections` above)

### AppRadioButton.qml

New ~25-line component. A `RadioButton` with custom indicator: 16 px circle,
hollow when unchecked, accent-filled with a smaller white circle when checked.
Includes a text `Label` to its right.

---

## 5. AboutDialog.qml

Modal, `OverlayCard` background, width 320 px.

```
[app icon 48×48]
GitTide
Version 0.1.0
A multi-repo git client.
              [Close]
```

Version string exposed as a `QString` context property `"appVersion"` in
`qml_main.cpp`, set from `QString::fromStdString(std::string(gittide::kVersion))`.

---

## 6. Files created / modified

### New files

| File | Purpose |
|------|---------|
| `ui/qml/TitleBar.qml` | Custom frameless title bar |
| `ui/qml/WindowButton.qml` | Reusable window-control button |
| `ui/qml/EdgeResizer.qml` | Per-edge resize drag handler |
| `ui/qml/OptionsDialog.qml` | Theme + pull-default settings |
| `ui/qml/AboutDialog.qml` | App info |
| `ui/qml/AppRadioButton.qml` | Themed radio button with label |

### Modified files

| File | Change |
|------|--------|
| `app/qml_main.cpp` | Read `QSettings` theme at startup; apply to ThemeManager |
| `ui/qml/Main.qml` | Frameless flags; `Settings` block; `TitleBar`; ColumnLayout; wire pull default |
| `ui/qml/Sidebar.qml` | Remove theme-toggle button |
| `ui/qml/BranchBar.qml` | Remove `⋯` pull-strategy button |
| `ui/include/gittide/ui/repoviewmodel.hpp` | Add `applyPullDefault(bool)` |
| `ui/src/repoviewmodel.cpp` | Add `applyPullDefault`; remove `loadPullStrategy` call; remove `setPullRebase` git-config write |
| `ui/qml/CMakeLists.txt` | Register new QML files |
