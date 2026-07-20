# Plan 35 — macOS native chrome & system menu bar

| | |
|--|--|
| **Date** | 2026-07-06 |
| **Status** | `done` |
| **Spec** | [`spec/product/app-menu.md §8`](../spec/product/app-menu.md#8-macos-native-chrome--system-menu-bar) |
| **Depends on** | [Plan 15](2026-06-23-plan15-app-menu.md), [Plan 29](2026-06-30-plan29-menu-bar.md) |

**Goal:** On macOS, restore native window behaviour — the OS title bar with real
traffic lights and working native fullscreen — and move the application menu into
the system menu bar, matching other Mac apps. Windows/Linux keep the frameless
custom title bar and its in-window menu bar unchanged.

**Architecture:** Pure QML, selected by `window.isMac` (`Qt.platform.os === "osx"`);
no C++/Objective-C. macOS drops `Qt.FramelessWindowHint` (native decorations →
native fullscreen) and hides the custom `TitleBar`; a new `NativeMenuBar.qml`
built on `Qt.labs.platform` renders in the system menu bar. It emits the **same
signals** the `TitleBar` does, so `Main.qml` wires both front-ends to identical
handlers — one behaviour, two shells.

**Tech stack:** Qt Quick, `Qt.labs.platform` (native `MenuBar`/`Menu`/`MenuItem`,
with `AboutRole`/`PreferencesRole`/`QuitRole`), Qt 6.11.

## Global constraints

- No Qt in `core/` (untouched — this is UI-only). No native platform code.
- New `ui/` QML → `ui/qml/qml.qrc`. New assertions → `tests/ui/test_qml_shell.cpp`.
- Must keep passing: `title_bar_is_present` and `menu_bar_items_invoke_repo_actions`
  — hence `TitleBar` stays **instantiated** (just `visible: false`) on macOS so
  its `objectName` lookups and action wiring still resolve.

---

## Task 1: NativeMenuBar.qml (macOS system menu)

**Files:** Create `ui/qml/NativeMenuBar.qml`; Modify `ui/qml/qml.qrc`.

**Interfaces:** a `Qt.labs.platform.MenuBar` exposing `appSettings`/`repo`
properties and the signals `optionsRequested`, `aboutRequested`,
`openRepoFolderRequested`, `undoLastCommitRequested`, `discardAllRequested`,
`mergeRequested`, `rebaseRequested`, `stashRequested`, `popStashRequested`.

- [x] **Step 1** — Component mirroring `AppMenuBar` (§7) + the app-icon popup (§3),
  with role-tagged app-menu items; register in `qml.qrc`.
- [x] **Step 2** — Enable rules and theme wiring identical to §7.3–§7.4.

## Task 2: Platform-conditional chrome in Main.qml

**Files:** Modify `ui/qml/Main.qml`.

- [x] **Step 1** — `readonly property bool isMac`; `flags: isMac ? Qt.Window :
  (Qt.FramelessWindowHint | Qt.Window)`; `title: "GitTide"`.
- [x] **Step 2** — `TitleBar { visible: !window.isMac }`; add `!window.isMac` to
  every `EdgeResizer.active`.
- [x] **Step 3** — `Loader { active: window.isMac; source: "NativeMenuBar.qml" }`
  wiring `appSettings`/`repoVm` in `onLoaded`; a `Connections` on the loader item
  routing each signal to the same handler as the `TitleBar` bindings.

## Task 3: Test + verify

**Files:** Modify `tests/ui/test_qml_shell.cpp`.

- [x] **Step 1: Failing test** — `chrome_is_native_on_mac_custom_elsewhere`:
  loads `Main.qml`; on macOS asserts `nativeMenuBar` present and `titleBar` hidden,
  else `titleBar` visible and no native bar (`#ifdef Q_OS_MACOS`). Fails before the
  loader/visibility wiring exists.
- [x] **Step 2** — Make it pass; run the suite.

---

## Outcome

- **Shipped:** macOS now uses native window decorations (real traffic lights,
  working native fullscreen) and hosts File/Edit/View/Repository + About /
  Preferences (⌘,) / Quit (⌘Q) in the system menu bar. Windows/Linux are
  byte-for-byte unchanged (frameless custom `TitleBar` + in-window `AppMenuBar`).
- **Spec updated:** [`spec/product/app-menu.md §8`](../spec/product/app-menu.md#8-macos-native-chrome--system-menu-bar);
  §2 flags block and the Overview now note the platform split.
- **Code:** `ui/qml/NativeMenuBar.qml` (new, `Qt.labs.platform`);
  `ui/qml/Main.qml` (platform-conditional flags/title, hidden `TitleBar`, gated
  `EdgeResizer`s, native-menu `Loader` + `Connections`); `ui/qml/qml.qrc`.
  Covered by `tests/ui/test_qml_shell.cpp::chrome_is_native_on_mac_custom_elsewhere`.
- **Follow-up fix:** restored windowed geometry is now clamped to the current
  screen's available area (`window.restoreGeometry()` in `Main.qml`), fixing a
  window that launched partly off-screen when the saved position no longer fit the
  monitor arrangement. Spec: [`app-menu §2`](../spec/product/app-menu.md#window-flags).
- **Note:** the native menu bar's on-screen rendering and native fullscreen are
  inherently GUI-session behaviours — the headless test confirms the component
  loads and the chrome switches; visual confirmation is a manual Mac check.
