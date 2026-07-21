# App Menu, Custom Title Bar, and Options Dialog

| | |
|--|--|
| **Designed** | 2026-06-23 · menu bar 2026-06-30 |
| **Status** | `shipped` |
| **Wishlist** | [docs/wishlist/app-menu.md](../../wishlist/app-menu.md) |
| **Touches** | window chrome, settings persistence, theme, pull default, repo actions (menu bar) |

## Overview

GitTide gains a custom frameless title bar with an app icon menu on the left,
replacing the native OS decorations **on Windows and Linux**. On **macOS** the
window keeps native decorations and the menu moves to the system menu bar — see
[§8](#8-macos-native-chrome--system-menu-bar). Behind it sits a **tabbed Options**
dialog that consolidates appearance, git, identity, and account settings in one
place ([§4](#4-optionsdialogqml)). App settings persist via `QSettings`
(platform-native storage). The window launches maximised with a minimum size.

A classic horizontal **text menu bar** (File · Edit · Repository) sits to
the right of the app icon and hosts the per-repo actions — open folder, undo,
discard all, merge, rebase, stash/pop. See [§7](#7-menu-bar-file--edit--repository).
Theme lives only in Options → Appearance ([§4](#4-optionsdialogqml)) on every
platform — neither the in-window bar nor the macOS native system menu bar
([§8.2](#82-native-system-menu-bar--nativemenubarqml)) carries a View menu.

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

The geometry-persistence handlers (`onXChanged`/`onWidthChanged`/
`onVisibilityChanged` etc.) are **gated on a `_restored` flag** that stays
`false` until `Component.onCompleted` has read the stored values and applied
them. Without this gate the transient `Windowed` state the window passes through
while mapping fires those handlers *before* `onCompleted` runs, overwriting the
stored `visibility` (e.g. Maximized → Windowed) so the next launch never
restores maximised. See [§2](#2-frameless-window--titlebar).

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

`Main.qml` sets the flags **per platform** — frameless only off macOS, which
keeps native decorations so the OS traffic lights and native fullscreen work
([§8](#8-macos-native-chrome--system-menu-bar)):

```qml
readonly property bool isMac: Qt.platform.os === "osx"
flags: isMac ? Qt.Window : (Qt.FramelessWindowHint | Qt.Window)
title: "GitTide"          // shown in the native macOS title bar
minimumWidth: 860
minimumHeight: 560
```

Geometry and visibility are restored from `Settings` on startup. On first launch
(no stored geometry), the `ApplicationWindow` default `visibility: Window.Maximized`
takes effect. Subsequent launches restore the last saved state — a saved
Maximized state calls `window.showMaximized()`, otherwise the windowed geometry
is restored. Restored windowed geometry is **clamped to the current screen's
available area** (`window.restoreGeometry()`, using `Screen.desktopAvailable*`
offset by the virtual-desktop origin) so a window saved on a now-absent or
rearranged monitor — or with stale/negative coordinates — cannot launch partly
off-screen. The `_restored` gate ([§1](#1-settings-persistence)) protects the
stored visibility from being clobbered by the transient states the window passes
through while mapping.

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
(macOS). Click opens `appMenuPopup`. The text [menu bar](#7-menu-bar-file--edit--repository)
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
the text [menu bar](#7-menu-bar-file--edit--repository) (§7):

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

Modal dialog, `OverlayCard` background, width 560 px. **Tabbed**: a `TabBar`
(objectName `optionsTabBar`) over a `StackLayout` with four tabs, each its own
component file. No OK/Cancel — settings apply instantly on each change (live).
Footer has a single **Close** button.

### Tabs

```
[Appearance] [Git] [Identity] [Accounts]
```

- **Appearance** (`OptionsAppearanceTab.qml`) — theme mode
  `○ System   ○ Dark   ○ Light`.
- **Git** (`OptionsGitTab.qml`) — pull default
  `○ Merge (fast-forward)   ○ Rebase`.
- **Identity** (`OptionsIdentityTab.qml`) — the named git identity catalogue
  (add name + email) and Global / Project / Repo assignment per identity, backed
  by `CredentialManager` / `IdentityListModel`. This is where the [named
  identities](product.md#identity--credentials) flow now lives — the standalone
  `IdentityDialog.qml` credentials dialog was removed and its content folded in
  here and into Accounts below.
- **Accounts** (`OptionsAccountsTab.qml`) — forge host accounts (HTTPS tokens,
  validated against the host API and saved to the keychain) and SSH keys
  (keyfile paths + keychain passphrase), backed by `HostListModel` /
  `SshKeyListModel`.

Each of Appearance/Git/Identity/Accounts is scrollable independently (Identity
and Accounts wrap their content in a `Flickable` capped at 480 px tall) so the
dialog height stays bounded as identities/accounts grow.

Radio-group behaviour (Appearance, Git): `ButtonGroup` containing `AppRadioButton`
items per section.

On `AppRadioButton` check:
- **Theme** (Appearance tab) → `theme.setMode(value)` + `appSettings.themeMode = value`
- **Pull** (Git tab) → `appSettings.pullRebase = value` (propagates to
  RepoViewModel via the `Main.qml` `Connections` above)

### AppRadioButton.qml

New ~25-line component. A `RadioButton` with custom indicator: 16 px circle,
hollow when unchecked, accent-filled with a smaller white circle when checked.
Includes a text `Label` to its right.

### AppTabButton.qml

Shared flat tab-button primitive (also used by `WorkingPane`'s tab strip): active
tab is `theme.textPrimary` (demibold) with a 2 px accent underline, inactive is
`theme.textSecondary`, hover tints an inactive tab. Used inside the Options
dialog's `TabBar`.

### First-run identity seed

If no identities exist yet when `CredentialManager` is constructed and the
user's global git config already has `user.name` + `user.email` (read via the
static `GitRepo::globalIdentity()`, no repo needed), one Global identity is
seeded from it — so the Identity tab isn't empty on first run. One-time: guarded
on the identity store being empty, so it never resurrects a deleted identity or
re-runs once any identity exists.

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
| `ui/qml/OptionsDialog.qml` | Tabbed settings dialog (Appearance/Git/Identity/Accounts) |
| `ui/qml/AboutDialog.qml` | App info |
| `ui/qml/AppRadioButton.qml` | Themed radio button with label |
| `ui/qml/AppTabButton.qml` | Shared flat tab button (Options dialog + WorkingPane) |
| `ui/qml/OptionsAppearanceTab.qml` | Options → Appearance tab (theme mode) |
| `ui/qml/OptionsGitTab.qml` | Options → Git tab (pull default) |
| `ui/qml/OptionsIdentityTab.qml` | Options → Identity tab (named identities + assignment) |
| `ui/qml/OptionsAccountsTab.qml` | Options → Accounts tab (forge tokens + SSH keys) |

**Removed:** `ui/qml/IdentityDialog.qml` (Plan 40) — the standalone Credentials
dialog. Its content moved into `OptionsIdentityTab.qml` / `OptionsAccountsTab.qml`
above; there is no longer a "Manage identities…" entry point outside Options.

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

## 7. Menu bar (File · Edit · Repository)

| | |
|--|--|
| **Designed** | 2026-06-30 · View menu removed 2026-07-21 |
| **Status** | `shipped` |
| **Plan** | [Plan 29](../../plans/2026-06-30-plan29-menu-bar.md), [Plan 40](../../plans/2026-07-21-plan40-tabbed-options-dialog.md) |

The title bar gains a classic horizontal **text menu bar** to the right of the app
icon. The app-icon popup (§3) keeps only app-level items (Options / About / Quit);
every repository operation lives under one of three text menus. This replaces
cramming repo actions under the icon and gives the actions room to grow. There is
no View menu here — theme lives only in Options → Appearance (§4). The macOS
native system menu bar (§8.2) dropped its View ▸ Theme submenu for the same
reason, so no platform exposes a View menu.

### 7.1 Layout in the title bar

```
macOS:    [● ● ●] [icon] File Edit Repository  [──── drag ────]
Win/Lin:  [icon]  File Edit Repository  [──── drag ────]  [─][□][✕]
```

The bar is a `RowLayout` of `MenuBarButton`s placed in `TitleBar.qml` right after
the app-icon button, before the drag `Item`. Each button is a flat themed text
button (`theme.textPrimary`, hover `theme.surfaceOverlay`) that opens its own
`AppMenu` popup anchored below it on click.

### 7.2 Menu contents

All repository items are **per-repo** and act on the current repo via `repoVm`
(the active `RepoViewModel`, already used by the existing icon-menu items). Each
item is an `AppMenuItem`; `destructive: true` marks danger (red) actions.

```
File                       Edit                      Repository
  Open repository folder     Undo last commit          Merge into current branch…
                             Discard all changes  (red) Rebase current branch…
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
| **Repository** › Merge into current branch… | `mergeRequested()` → `mergeTargetDialog.open()` | picker → `repoVm.startMerge(ref)` |
| **Repository** › Rebase current branch… | `rebaseRequested()` → `rebaseTargetDialog.open()` | existing → `repoVm.startRebase(ref)` |
| **Repository** › Stash all changes | `stashRequested()` → `repoVm.stashChanges()` | new VM `stashChanges()` |
| **Repository** › Pop latest stash | `popStashRequested()` → `repoVm.popStash()` | new VM `popStash()` |

### 7.3 Enable / disable rules

Every repo item requires an open repo. Binding source in parentheses:

| Item | Enabled when |
|------|--------------|
| Open repository folder | a repo is open (`repoVm && repoVm.repoOpen`) |
| Undo last commit | repo open **and** not `rebaseInProgress \|\| mergeInProgress` (existing rule) |
| Discard all changes | repo open **and** working tree dirty (`repoVm.dirty`) |
| Merge…/Rebase… | repo open **and** not `rebaseInProgress \|\| mergeInProgress` |
| Stash all changes | repo open **and** working tree dirty (`repoVm.dirty`) |
| Pop latest stash | repo open **and** `repoVm.stashAvailable` (new property) |

The VM has no dirty flag today (`checkedCount` counts *checked* rows, not changed
ones). Add a `RepoViewModel::dirty` (`Q_PROPERTY bool`, true when the
`changedFiles` model is non-empty). Its `NOTIFY` is a dedicated `dirtyChanged()`
signal emitted at the end of `onStatus()` — `changed()` is **not** emitted on the
status-refresh path (only on open/close), so reusing it would leave the binding
stale on dirty↔clean transitions.

### 7.4 New engine work

Most actions reuse existing wiring (rebase, merge, discard, undo all have full
core→VM stacks). Net-new pieces:

- **`GitRepo::discardAll()`** (`core`) — full working-tree reset: hard-reset to
  HEAD (`git_reset(GIT_RESET_HARD)`, which also drops staged changes; on an unborn
  HEAD the index is cleared instead), then a second pass enumerates untracked
  entries (status, `GIT_STATUS_WT_NEW`, no `RECURSE_UNTRACKED_DIRS`) and
  `std::filesystem::remove_all`s each — `git clean -fd` parity, removing untracked
  files *and* directories while leaving ignored files alone. Returns
  `Expected<void>`. Surfaced via `AsyncRepo::discardAll` →
  `RepoController::discardAll` → `RepoViewModel::discardAll()` (`Q_INVOKABLE`),
  with the standard post-write refresh cascade (status). TDD: `TempRepo` tests
  asserting a modified tracked file, a new untracked file, an untracked directory,
  and a staged new file are all gone and the tree is clean.
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
| `ui/qml/AppMenuBar.qml` | Horizontal text menu bar (File/Edit/Repository) |
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

---

## 8. macOS native chrome & system menu bar

| | |
|--|--|
| **Designed** | 2026-07-06 |
| **Status** | `shipped` |
| **Plan** | [Plan 35](../../plans/2026-07-06-plan35-macos-native-chrome.md) |

The frameless custom title bar (§2) with its fake traffic lights and in-window
menu bar (§7) is a **Windows/Linux** shell. On **macOS** it fought the platform:
the frameless flag disables native fullscreen (the green "traffic light" merely
toggled maximise), and menus belonged in the window rather than the system menu
bar. macOS therefore takes a native path instead — selected purely by
`window.isMac` (`Qt.platform.os === "osx"`); no C++/Objective-C.

### 8.1 Native window decorations

macOS drops `Qt.FramelessWindowHint`, so the OS draws the title bar with **real**
traffic lights and **native fullscreen** works (green button / `⌃⌘F`). The custom
`TitleBar` is `visible: false` on macOS — kept instantiated so the shared action
wiring and `objectName` lookups still resolve, but excluded from the
`ColumnLayout` so no empty bar remains. The seven frameless `EdgeResizer` zones
gain `!window.isMac` to their `active` binding (the native frame owns resizing).
The window sets `title: "GitTide"` for the native bar.

### 8.2 Native system menu bar — `NativeMenuBar.qml`

A new component built on **`Qt.labs.platform`** (`MenuBar` / `Menu` / `MenuItem`),
which renders in the macOS system menu bar at the top of the screen. `Main.qml`
instantiates it through a `Loader` with `active: window.isMac`, passing
`appSettings` and `repoVm` in `onLoaded`. Windows/Linux never load it and keep
the in-window `AppMenuBar` (§7).

It mirrors the §7 action set and the §3 app-icon popup, and **emits the same
signals** `TitleBar` does, so `Main.qml` binds them (via a `Connections` on the
loader item) to the *identical* handlers as the custom bar — one source of
behaviour, two front-ends. Like §7's in-window bar, this native bar has **no
View menu** — theme lives only in Options → Appearance (§4). Menu layout:

```
GitTide (app menu)     File            Edit                 Repository
  About GitTide          Open repo       Undo last commit     Merge into current branch…
  Preferences… (⌘,)      folder          Discard all changes  Rebase current branch…
  Quit (⌘Q)                                                   ─────────────
                                                              Stash all changes
                                                              Pop latest stash
```

The application-menu items use `Qt.labs.platform` **roles** (`AboutRole`,
`PreferencesRole`, `QuitRole`) so macOS relocates them into the bold app menu
with their conventional shortcuts. Enable/disable rules are identical to
§7.3–§7.4 (bound to the same `repoVm` properties).

### 8.3 Files created / modified (this change)

| File | Change |
|------|--------|
| `ui/qml/NativeMenuBar.qml` | **New** — `Qt.labs.platform` system menu bar |
| `ui/qml/Main.qml` | Platform-conditional `flags`/`title`; hide `TitleBar` + gate `EdgeResizer`s on macOS; `Loader` + `Connections` for the native bar |
| `ui/qml/qml.qrc` | Register `NativeMenuBar.qml` |
| `tests/ui/test_qml_shell.cpp` | `chrome_is_native_on_mac_custom_elsewhere` |
