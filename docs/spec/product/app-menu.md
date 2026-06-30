# App Menu, Custom Title Bar, and Options Dialog

| | |
|--|--|
| **Designed** | 2026-06-23 · menu bar 2026-06-30 |
| **Status** | `spec` |
| **Wishlist** | [docs/wishlist/app-menu.md](../../wishlist/app-menu.md) |
| **Touches** | window chrome, settings persistence, theme, pull default, repo actions (menu bar) |

## Overview

GitTide gains a custom frameless title bar with an app icon menu on the left,
replacing the native OS decorations. Behind it sits an **Options** dialog that
consolidates theme and pull-default settings in one place. App settings persist
via `QSettings` (platform-native storage). The window launches maximised with a
minimum size.

A classic horizontal **text menu bar** (File · Edit · View · Repository) sits to
the right of the app icon and hosts the per-repo actions — open folder, undo,
discard all, theme, merge, rebase, stash/pop. See [§7](#7-menu-bar-file--edit--view--repository).

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
(macOS). Click opens `appMenuPopup`. The text [menu bar](#7-menu-bar-file--edit--view--repository)
(§7) sits immediately to the right of the icon; the drag area takes the remaining
space.

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
below the icon button. It holds **app-level** items only — repo operations live in
the text [menu bar](#7-menu-bar-file--edit--view--repository) (§7):

```
Options…
About GitTide
─────────────
Quit
```

`Options…` → `optionsRequested()`, `About GitTide` → `aboutRequested()`,
`Quit` calls `Qt.quit()`. The repo actions formerly here (`Rebase current
branch…`, `Undo last commit`) move into the menu bar's **Repository** / **Edit**
menus (§7).

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

---

## 7. Menu bar (File · Edit · View · Repository)

| | |
|--|--|
| **Designed** | 2026-06-30 |
| **Status** | `spec` |

The title bar gains a classic horizontal **text menu bar** to the right of the app
icon. The app-icon popup (§3) keeps only app-level items (Options / About / Quit);
every repository operation lives under one of four text menus. This replaces
cramming repo actions under the icon and gives the actions room to grow.

### 7.1 Layout in the title bar

```
macOS:    [● ● ●] [icon] File Edit View Repository  [──── drag ────]
Win/Lin:  [icon]  File Edit View Repository  [──── drag ────]  [─][□][✕]
```

The bar is a `RowLayout` of `MenuBarButton`s placed in `TitleBar.qml` right after
the app-icon button, before the drag `Item`. Each button is a flat themed text
button (`theme.textPrimary`, hover `theme.surfaceHover`) that opens its own
`AppMenu` popup anchored below it. Hovering an adjacent button while a menu is open
switches to that menu (standard menu-bar behaviour), implemented with a shared
`openMenu` index the buttons read/write.

### 7.2 Menu contents

All repository items are **per-repo** and act on the current repo via `repoVm`
(the active `RepoViewModel`, already used by the existing icon-menu items). Each
item is an `AppMenuItem`; `destructive: true` marks danger (red) actions.

```
File                       Edit                      View
  Open repository folder     Undo last commit          Theme ▸
                             Discard all changes  (red)     System
                                                            Dark
Repository                                                  Light
  Merge into current branch…
  Rebase current branch…
  ─────────────
  Stash all changes
  Pop latest stash
```

Wiring (each item emits a `TitleBar` signal; `Main.qml` binds it, mirroring the
existing `optionsRequested`/`rebaseRequested` pattern):

| Item | Signal → Main.qml handler | Reaches |
|------|---------------------------|---------|
| **File** › Open repository folder | `openRepoFolderRequested()` → `repoVm.openRepoFolder()` | new VM `openRepoFolder()` |
| **Edit** › Undo last commit | `undoLastCommitRequested()` → `repoVm.undoLastCommit()` | existing |
| **Edit** › Discard all changes | `discardAllRequested()` → `discardAllDialog.open()` | confirm → `repoVm.discardAll()` |
| **View** › Theme › System/Dark/Light | sets `appSettings.themeMode` + `theme.setMode(v)` | mirrors OptionsDialog (§4) |
| **Repository** › Merge into current branch… | `mergeRequested()` → `mergeTargetDialog.open()` | picker → `repoVm.startMerge(ref)` |
| **Repository** › Rebase current branch… | `rebaseRequested()` → `rebaseTargetDialog.open()` | existing → `repoVm.startRebase(ref)` |
| **Repository** › Stash all changes | `stashRequested()` → `repoVm.stashChanges()` | new VM `stashChanges()` |
| **Repository** › Pop latest stash | `popStashRequested()` → `repoVm.popStash()` | new VM `popStash()` |

### 7.3 Enable / disable rules

Every repo item requires an open repo. Binding source in parentheses:

| Item | Enabled when |
|------|--------------|
| Open repository folder | a repo is open (`repoVm && repoVm.repoPath !== ""`) |
| Undo last commit | repo open **and** not `rebaseInProgress \|\| mergeInProgress` (existing rule) |
| Discard all changes | repo open **and** working tree dirty (`repoVm.dirty`) |
| Merge…/Rebase… | repo open **and** not `rebaseInProgress \|\| mergeInProgress` |
| Stash all changes | repo open **and** working tree dirty (`repoVm.dirty`) |
| Pop latest stash | repo open **and** `repoVm.stashAvailable` (new property) |
| Theme items | always |

The VM has no dirty flag today (`checkedCount` counts *checked* rows, not changed
ones). Add a `RepoViewModel::dirty` (`Q_PROPERTY bool`, true when the
`changedFiles` model is non-empty, NOTIFY `changedChanged`/`changed`).

### 7.4 New engine work

Most actions reuse existing wiring (rebase, merge, discard, undo all have full
core→VM stacks). Net-new pieces:

- **`GitRepo::discardAll()`** (`core`) — atomic full reset: `git_checkout_head`
  with `GIT_CHECKOUT_FORCE` and no path filter (resets all tracked), then iterate
  the status list and delete each `GIT_STATUS_WT_NEW` (untracked) entry. Returns
  `Expected<void>`. Surfaced via `AsyncRepo::discardAll` →
  `RepoController::discardAll` → `RepoViewModel::discardAll()` (`Q_INVOKABLE`),
  with the standard post-write refresh cascade (status + diff). TDD: a `TempRepo`
  test asserting both a modified tracked file and a new untracked file are gone.
- **Stash exposure on the VM.** `stashSave`/`stashPop` already exist on
  `GitRepo`/`AsyncRepo`; add `RepoController::stashChanges()`/`popStash()` and
  `RepoViewModel::stashChanges()`/`popStash()` (`Q_INVOKABLE`) that drive them
  with the refresh cascade. `stashChanges()` maps to `stashSave("")` (no message
  prompt in the first cut).
- **`GitRepo::stashCount()`** (`core`) — counts entries via `git_stash_foreach`,
  returns `Expected<int>`. Drives a new `RepoViewModel::stashAvailable`
  (`Q_PROPERTY bool`, `count > 0`), refreshed in the same cascade as status so
  **Pop latest stash** enables/disables correctly. (Full stash stack/list UI stays
  out of scope — that is the separate [stash-management](../../wishlist/stash-management.md)
  wish; this is only save + pop-top.)
- **`RepoViewModel::openRepoFolder()`** (`Q_INVOKABLE`) —
  `QDesktopServices::openUrl(QUrl::fromLocalFile(repoPath()))`. Note this opens the
  **repo root itself** (unlike `revealInFileManager`, which opens a file's *parent*).
  Per the open-targets rule: **folders/repos open in the OS-native handler**;
  individual files (diff/history) keep opening in the OS-default editor via the
  existing `openInEditor` (unchanged).

### 7.5 Branch-picker reuse (merge)

`RebaseTargetDialog.qml` is already a clean local-branch picker emitting
`accepted(ref)`. Generalise it into a reusable **`BranchPickerDialog.qml`**
(parameterised `title`, `actionLabel`, and the prompt text; same
`repo.branches` model, same `selectedRef`). Both the rebase route and the new
merge route instantiate it. Per the repo code rule, **split the rename/extract
from any behavioural change into a separate commit** to preserve history.

### 7.6 Files created / modified (this change)

#### New files

| File | Purpose |
|------|---------|
| `ui/qml/AppMenuBar.qml` | Horizontal text menu bar (File/Edit/View/Repository) |
| `ui/qml/MenuBarButton.qml` | Flat themed text button that opens its `AppMenu` |
| `ui/qml/BranchPickerDialog.qml` | Reusable branch picker (generalised from `RebaseTargetDialog`) |

#### Modified files

| File | Change |
|------|--------|
| `ui/qml/TitleBar.qml` | Trim icon popup to app-level items; add `AppMenuBar`; declare new signals |
| `ui/qml/Main.qml` | Instantiate `AppMenuBar`; add `mergeTargetDialog` + `discardAllDialog`; bind new signals |
| `ui/qml/RebaseTargetDialog.qml` | Becomes `BranchPickerDialog` (rename) + merge instance |
| `ui/qml/DiscardChangesDialog.qml` | Reused for the repo-wide discard-all confirm (or a thin variant) |
| `core/include/gittide/gitrepo.hpp` · `core/src/gitrepo.cpp` | Add `discardAll()`, `stashCount()` |
| `ui/include/gittide/ui/asyncrepo.hpp` · `*.cpp` | Add `discardAll`, `stashCount` |
| `ui/include/gittide/ui/repocontroller.hpp` · `*.cpp` | Add `discardAll`, `stashChanges`, `popStash`, stash-count refresh |
| `ui/include/gittide/ui/repoviewmodel.hpp` · `*.cpp` | Add `discardAll()`, `stashChanges()`, `popStash()`, `openRepoFolder()`, `stashAvailable` + `dirty` properties |
| `ui/qml/CMakeLists.txt` | Register new QML files |
| `tests/CMakeLists.txt` | Register new core tests (`discardAll`, `stashCount`) |
