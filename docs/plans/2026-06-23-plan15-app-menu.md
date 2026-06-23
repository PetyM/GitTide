# Plan 15 — App Menu, Custom Title Bar, Options Dialog

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the native OS window decorations with a custom frameless title bar carrying an app-icon menu; add an Options dialog for theme and pull-default settings persisted via `QSettings`; launch the window maximised by default.

**Architecture:** `QSettings` (Qt, in `ui/` and `app/`) handles all app-level persistence — no new `core/` type. A single `Settings { id: appSettings }` QML object in `Main.qml` owns theme mode and pull-default in memory and on disk; C++ reads `QSettings` at startup to apply the stored theme before QML renders. `TitleBar.qml` uses `DragHandler + window.startSystemMove()` for native drag (Snap on Windows, correct macOS behaviour) and platform-conditional layout (traffic lights left on macOS, glyphs right elsewhere).

**Tech Stack:** Qt 6 Quick / Controls Basic, `QtCore.Settings` QML type, `QSettings` C++, `window.startSystemMove()` / `startSystemResize()`, `Qt.FramelessWindowHint`.

## Global Constraints

- No Qt in `core/` — `QSettings` lives only in `ui/` and `app/`.
- All new C++ files in `ui/` → list in `ui/CMakeLists.txt`; all new QML files → list in `ui/qml/qml.qrc`.
- New UI test file requires **two** edits: add to `gittide_ui_test_sources` in `tests/CMakeLists.txt` **and** add `#include` + `RUN()` in `tests/ui/main.cpp`. Missing either compiles fine but silently skips the test.
- Build command: `cmake --build build --parallel`; test command: `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui`
- `AppRadioButton` text uses `theme.textPrimary`; indicator uses `theme.accent` / `theme.border` / `theme.surfaceBase`.
- Window control glyph colors: close hover = `#C42B1C` bg + white glyph (Windows/Linux). macOS circle colors: close `#FF5F56`, minimise `#FFBD2E`, maximise `#27C93F`.

---

### Task 1: RepoViewModel — global pull default

Remove the per-repo git-config pull strategy; add `applyPullDefault(bool)` that simply sets `m_pullRebase` without touching git config. The global default is injected from QML at startup and on settings change.

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Modify: `tests/ui/test_qml_sync.cpp` (rewrite `pullRebaseRoundTrips`)
- Modify: `tests/ui/test_repo_view_model.cpp` (update close-state assertion)

**Interfaces:**
- Removes: `Q_INVOKABLE void setPullRebase(bool rebase)` (was writing to git config)
- Adds: `Q_INVOKABLE void applyPullDefault(bool rebase)` — sets `m_pullRebase`, emits `pullRebaseChanged()`
- `pullRebase()` and `pullRebaseChanged` signal remain unchanged (QML still reads them)
- `close()` no longer resets `m_pullRebase` (global default stays set across repo switches)

- [ ] **Step 1: Write the failing test — `applyPullDefault` sets the property**

Open `tests/ui/test_qml_sync.cpp`. Replace the entire `pullRebaseRoundTrips` slot:

```cpp
void TestQmlSync::pullRebaseRoundTrips()
{
    // applyPullDefault sets m_pullRebase without opening a repo or touching git config
    RepoViewModel vm;
    QCOMPARE(vm.pullRebase(), false);

    vm.applyPullDefault(true);
    QCOMPARE(vm.pullRebase(), true);

    vm.applyPullDefault(false);
    QCOMPARE(vm.pullRebase(), false);
}
```

- [ ] **Step 2: Run the test to confirm it fails**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui -V 2>&1 | grep -A5 "pullRebase"
```

Expected: compile error `applyPullDefault not declared` (or linker error once header is updated first).

- [ ] **Step 3: Update `repoviewmodel.hpp`**

In `ui/include/gittide/ui/repoviewmodel.hpp`, remove `setPullRebase` and add `applyPullDefault`:

```cpp
    // Remove this line:
    // Q_INVOKABLE void setPullRebase(bool rebase);

    // Add this line (after publishBranch):
    Q_INVOKABLE void applyPullDefault(bool rebase);
```

- [ ] **Step 4: Update `repoviewmodel.cpp` — replace `setPullRebase`, update `open()` and `close()`**

In `ui/src/repoviewmodel.cpp`:

**In `open()` — remove the `loadPullStrategy` line (line 122):**
```cpp
// Remove:
QCoro::connect(m_controller->loadPullStrategy(), this, [] {});
```

**In `close()` — remove `m_pullRebase` reset and its emit:**
```cpp
// Remove these two lines (currently around lines 147 and 153):
m_pullRebase     = false;
// ...
emit pullRebaseChanged();
```
(Keep the other `emit` calls in `close()` — only remove the pullRebase ones.)

**Replace `setPullRebase` implementation with `applyPullDefault`:**

Find and replace the entire `setPullRebase` function body:
```cpp
// OLD (remove entirely):
void RepoViewModel::setPullRebase(bool rebase)
{
    QCoro::connect(m_controller->setPullStrategy(rebase ? gittide::PullStrategy::Rebase
                                                        : gittide::PullStrategy::FastForwardOnly),
                   this, [] {});
}

// NEW (add in its place):
void RepoViewModel::applyPullDefault(bool rebase)
{
    m_pullRebase = rebase;
    emit pullRebaseChanged();
}
```

- [ ] **Step 5: Update `test_repo_view_model.cpp` — remove stale `pullRebase` close assertion**

Find line 170: `QCOMPARE(vm.pullRebase(), false);   // session state reset`

Replace that line with a comment explaining the new design:
```cpp
// pullRebase is no longer reset on close — it reflects the global default
// set via applyPullDefault(), which persists across repo switches.
```

- [ ] **Step 6: Build and run all UI tests**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp \
        ui/src/repoviewmodel.cpp \
        tests/ui/test_qml_sync.cpp \
        tests/ui/test_repo_view_model.cpp
git commit -m "feat(vm): add applyPullDefault; remove per-repo pull-strategy persistence"
```

---

### Task 2: QSettings theme persistence + appVersion context property

Two changes to the app startup: (1) read stored theme mode from `QSettings` before the QML engine loads (avoids a flash of the wrong theme); (2) expose `appVersion` as a QML context property. Neither requires a new C++ class.

**Files:**
- Modify: `app/qml_main.cpp`
- Modify: `ui/src/qmlcontext.cpp`
- Modify: `ui/include/gittide/ui/qmlcontext.hpp`

**Interfaces:**
- `installQmlContext` gains a new trailing parameter `const QString& appVersion = {}` — default empty so all existing callers (tests) compile unchanged.
- The `"appVersion"` QML context property is always set (empty string in tests, real version in production).

- [ ] **Step 1: Write the failing test — `appVersion` is available in QML context**

In `tests/ui/test_qml_shell.cpp`, add a new slot to `TestQmlShell`:

```cpp
void appVersion_context_property_is_set()
{
    ThemeManager mgr;
    mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr,
                      nullptr, QStringLiteral("1.2.3"));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    QCOMPARE(engine.rootObjects().size(), 1);
    QVariant v = engine.rootContext()->contextProperty(QStringLiteral("appVersion"));
    QCOMPARE(v.toString(), QStringLiteral("1.2.3"));
}
```

- [ ] **Step 2: Run test to confirm it fails**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui -V 2>&1 | grep -A3 "appVersion"
```

Expected: compile error — `installQmlContext` does not accept 7 arguments.

- [ ] **Step 3: Update `qmlcontext.hpp` — add `appVersion` parameter**

```cpp
void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel,
                       ProjectController* projectController, RepoViewModel* repoVm,
                       QmlLog* log = nullptr, const QString& appVersion = {});
```

- [ ] **Step 4: Update `qmlcontext.cpp` — wire the new property**

Inside `installQmlContext`, add after the last `setContextProperty` call:

```cpp
ctx->setContextProperty(QStringLiteral("appVersion"), appVersion);
```

- [ ] **Step 5: Update `qml_main.cpp` — read theme from QSettings and pass appVersion**

Replace the current line:
```cpp
theme.setMode(ThemeManager::Mode::System);
```
with:
```cpp
{
    QSettings s;
    const int storedMode = s.value(QStringLiteral("themeMode"), 0).toInt();
    theme.setMode(static_cast<ThemeManager::Mode>(storedMode));
}
```

Also update the `installQmlContext` call to pass `appVersion`:
```cpp
// Add this include at the top of qml_main.cpp:
#include "gittide/version.hpp"

// Update the installQmlContext call (last argument added):
installQmlContext(engine.rootContext(), &qmlTheme, controller.repos(), &controller, &repoVm,
                  nullptr,
                  QString::fromStdString(std::string(gittide::kVersion)));
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui
```

Expected: all pass, including the new `appVersion_context_property_is_set`.

- [ ] **Step 7: Commit**

```bash
git add app/qml_main.cpp \
        ui/src/qmlcontext.cpp \
        ui/include/gittide/ui/qmlcontext.hpp \
        tests/ui/test_qml_shell.cpp
git commit -m "feat(app): read theme from QSettings at startup; expose appVersion to QML"
```

---

### Task 3: AppRadioButton.qml

New themed radio button with a text label — used in `OptionsDialog` for theme and pull-default sections.

**Files:**
- Create: `ui/qml/AppRadioButton.qml`
- Modify: `ui/qml/qml.qrc`

**Interfaces:**
- Produces: `AppRadioButton` QML type — same API as `RadioButton` (inherits it). `text`, `checked`, `ButtonGroup.group`, `onClicked`.

- [ ] **Step 1: Create `ui/qml/AppRadioButton.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic

// Themed radio button with a text label. Same API as RadioButton — use with
// ButtonGroup for mutually exclusive groups. Indicator is a 16px circle:
// hollow when unchecked, accent-filled with a white inner dot when checked.
RadioButton {
    id: rb

    implicitHeight: 28
    spacing: 8
    padding: 0

    indicator: Rectangle {
        width: 16
        height: 16
        anchors.verticalCenter: rb.verticalCenter
        radius: 8
        color: rb.checked ? theme.accent : "transparent"
        border.color: rb.checked ? theme.accent
                                 : (rb.hovered ? theme.textSecondary : theme.border)
        border.width: 1

        Rectangle {
            anchors.centerIn: parent
            visible: rb.checked
            width: 6
            height: 6
            radius: 3
            color: theme.surfaceBase
        }
    }

    contentItem: Label {
        leftPadding: rb.indicator.width + rb.spacing
        text: rb.text
        color: theme.textPrimary
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
    }
}
```

- [ ] **Step 2: Register in `ui/qml/qml.qrc`**

Add inside the `<qresource prefix="/qml">` block:
```xml
<file>AppRadioButton.qml</file>
```

- [ ] **Step 3: Build to verify no errors**

```bash
cmake --build build --parallel 2>&1 | tail -5
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add ui/qml/AppRadioButton.qml ui/qml/qml.qrc
git commit -m "feat(ui): add AppRadioButton themed component"
```

---

### Task 4: WindowButton.qml + EdgeResizer.qml

Two low-level window-chrome primitives. `WindowButton` is a cross-platform window-control button (macOS colored circle or Win/Linux flat glyph). `EdgeResizer` is a transparent drag zone that calls `window.startSystemResize(edges)`.

**Files:**
- Create: `ui/qml/WindowButton.qml`
- Create: `ui/qml/EdgeResizer.qml`
- Modify: `ui/qml/qml.qrc`

**Interfaces:**
- `WindowButton`: properties `macOs: bool`, `circleColor: color`, `glyph: string`, `glyphColor: color`, `hoverColor: color`. Inherits `AbstractButton` — connect `onClicked`.
- `EdgeResizer`: properties `edges: int` (a `Qt.Edge` flag), `active: bool`. Place with anchors; fires `window.startSystemResize(edges)` on drag.

- [ ] **Step 1: Create `ui/qml/WindowButton.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic

// Cross-platform window control button.
// macOs=true: colored circle (close/minimise/maximise traffic lights).
// macOs=false: flat glyph button (Win/Linux style).
AbstractButton {
    id: btn

    property bool macOs: false
    property color circleColor: "transparent"
    property string glyph: ""
    property color glyphColor: theme.textSecondary
    property color hoverColor: theme.surfaceOverlay

    implicitWidth: macOs ? 28 : 46
    implicitHeight: 40
    padding: 0

    contentItem: Item {
        // macOS circle with glyph shown on hover
        Rectangle {
            visible: btn.macOs
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: btn.circleColor
            opacity: btn.hovered ? 1.0 : 0.85

            Label {
                anchors.centerIn: parent
                visible: btn.hovered
                text: btn.glyph
                color: "#333333"
                font.pixelSize: 7
                font.weight: Font.Bold
            }
        }

        // Win/Linux glyph
        Label {
            visible: !btn.macOs
            anchors.centerIn: parent
            text: btn.glyph
            color: btn.glyphColor
            font.pixelSize: 13
        }
    }

    background: Rectangle {
        visible: !btn.macOs
        color: btn.hovered ? btn.hoverColor : "transparent"
    }
}
```

- [ ] **Step 2: Create `ui/qml/EdgeResizer.qml`**

```qml
import QtQuick

// Transparent resize handle for a frameless window. Anchor to a window edge;
// set `edges` to the Qt.Edge flag(s) for this zone. Set `active: false` when
// the window is maximised to disable resizing.
Item {
    id: root

    property int edges: Qt.LeftEdge
    property bool active: true

    HoverHandler {
        cursorShape: {
            const e = root.edges
            if (e === (Qt.TopEdge | Qt.LeftEdge) || e === (Qt.BottomEdge | Qt.RightEdge))
                return Qt.SizeFDiagCursor
            if (e === (Qt.TopEdge | Qt.RightEdge) || e === (Qt.BottomEdge | Qt.LeftEdge))
                return Qt.SizeBDiagCursor
            if (e === Qt.LeftEdge || e === Qt.RightEdge)
                return Qt.SizeHorCursor
            return Qt.SizeVerCursor
        }
    }

    DragHandler {
        target: null
        enabled: root.active
        onActiveChanged: if (active) window.startSystemResize(root.edges)
    }
}
```

- [ ] **Step 3: Register both files in `ui/qml/qml.qrc`**

```xml
<file>WindowButton.qml</file>
<file>EdgeResizer.qml</file>
```

- [ ] **Step 4: Build**

```bash
cmake --build build --parallel 2>&1 | tail -5
```

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/WindowButton.qml ui/qml/EdgeResizer.qml ui/qml/qml.qrc
git commit -m "feat(ui): add WindowButton and EdgeResizer for frameless chrome"
```

---

### Task 5: TitleBar.qml + AboutDialog.qml

The app's custom title bar and the About dialog. `TitleBar` emits signals instead of referencing `Main.qml` IDs directly — `Main.qml` connects them to dialogs.

**Files:**
- Create: `ui/qml/TitleBar.qml`
- Create: `ui/qml/AboutDialog.qml`
- Modify: `ui/qml/qml.qrc`

**Interfaces:**
- `TitleBar`: signals `optionsRequested()`, `aboutRequested()`. `objectName: "titleBar"`. Height 40.
- `AboutDialog`: `objectName: "aboutDialog"`. Reads `appVersion` from QML context.

- [ ] **Step 1: Create `ui/qml/TitleBar.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Custom frameless title bar. Left side: macOS traffic lights (isMac) or app
// icon button (Win/Linux). Centre: drag area (startSystemMove). Right side:
// Win/Linux window controls (minimise/maximise/close). Platform flag: isMac.
Rectangle {
    id: titleBar
    objectName: "titleBar"
    height: 40
    color: theme.surfaceRaised

    signal optionsRequested()
    signal aboutRequested()

    readonly property bool isMac: Qt.platform.os === "osx"

    // Bottom border
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.border
    }

    // Native drag — gives Windows Snap and correct macOS move behaviour
    DragHandler {
        target: null
        onActiveChanged: if (active) window.startSystemMove()
    }

    // Double-click: toggle maximise / restore
    TapHandler {
        numberOfTapsRequired: 2
        onTapped: {
            if (window.visibility === Window.Maximized)
                window.showNormal()
            else
                window.showMaximized()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // macOS: traffic lights on the left
        RowLayout {
            visible: titleBar.isMac
            spacing: 0
            Layout.leftMargin: 8

            WindowButton {
                macOs: true
                circleColor: "#FF5F56"
                glyph: "✕"
                onClicked: window.close()
            }
            WindowButton {
                macOs: true
                circleColor: "#FFBD2E"
                glyph: "─"
                onClicked: window.showMinimized()
            }
            WindowButton {
                macOs: true
                circleColor: "#27C93F"
                glyph: window.visibility === Window.Maximized ? "❐" : "+"
                onClicked: window.visibility === Window.Maximized
                           ? window.showNormal() : window.showMaximized()
            }
        }

        // App icon button — opens the app menu
        Button {
            id: iconBtn
            objectName: "appIconButton"
            flat: true
            implicitWidth: 40
            implicitHeight: 40
            Layout.leftMargin: 4

            contentItem: Image {
                source: theme.iconSource
                sourceSize.width: 22
                sourceSize.height: 22
                anchors.centerIn: parent
            }
            background: Rectangle {
                color: iconBtn.hovered ? theme.surfaceOverlay : "transparent"
                radius: 4
            }
            onClicked: appMenuPopup.popup()

            AppMenu {
                id: appMenuPopup
                objectName: "appMenuPopup"

                AppMenuItem {
                    objectName: "optionsMenuItem"
                    text: "Options…"
                    onTriggered: titleBar.optionsRequested()
                }
                AppMenuItem {
                    objectName: "aboutMenuItem"
                    text: "About GitTide"
                    onTriggered: titleBar.aboutRequested()
                }
                MenuSeparator {
                    padding: 6
                    contentItem: Rectangle { implicitHeight: 1; color: theme.border }
                }
                AppMenuItem {
                    objectName: "quitMenuItem"
                    text: "Quit"
                    onTriggered: Qt.quit()
                }
            }
        }

        // Drag spacer fills the centre
        Item { Layout.fillWidth: true }

        // Win/Linux: window controls on the right
        RowLayout {
            visible: !titleBar.isMac
            spacing: 0

            WindowButton {
                glyph: "─"
                ToolTip.visible: hovered
                ToolTip.text: "Minimise"
                onClicked: window.showMinimized()
            }
            WindowButton {
                glyph: window.visibility === Window.Maximized ? "❐" : "⬜"
                ToolTip.visible: hovered
                ToolTip.text: window.visibility === Window.Maximized ? "Restore" : "Maximise"
                onClicked: window.visibility === Window.Maximized
                           ? window.showNormal() : window.showMaximized()
            }
            WindowButton {
                glyph: "✕"
                hoverColor: "#C42B1C"
                glyphColor: hovered ? "white" : theme.textSecondary
                ToolTip.visible: hovered
                ToolTip.text: "Close"
                onClicked: window.close()
            }
        }
    }
}
```

- [ ] **Step 2: Create `ui/qml/AboutDialog.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// About dialog: app icon, name, version (from appVersion context property), tagline.
Dialog {
    id: dialog
    objectName: "aboutDialog"
    modal: true
    anchors.centerIn: parent
    width: 320
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: OverlayCard {}

    contentItem: ColumnLayout {
        spacing: 12

        Image {
            source: theme.iconSource
            sourceSize.width: 48
            sourceSize.height: 48
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "GitTide"
            color: theme.textPrimary
            font.pixelSize: 20
            font.weight: Font.Bold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Version " + appVersion
            color: theme.textMuted
            font.pixelSize: 13
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "A multi-repo git client."
            color: theme.textSecondary
            font.pixelSize: 13
            Layout.alignment: Qt.AlignHCenter
        }
    }

    footer: RowLayout {
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        Button {
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
```

- [ ] **Step 3: Register both files in `ui/qml/qml.qrc`**

```xml
<file>TitleBar.qml</file>
<file>AboutDialog.qml</file>
```

- [ ] **Step 4: Build**

```bash
cmake --build build --parallel 2>&1 | tail -5
```

Expected: build succeeds. (Files exist but aren't instantiated yet — no runtime errors.)

- [ ] **Step 5: Commit**

```bash
git add ui/qml/TitleBar.qml ui/qml/AboutDialog.qml ui/qml/qml.qrc
git commit -m "feat(ui): add TitleBar and AboutDialog"
```

---

### Task 6: OptionsDialog.qml

Settings dialog for theme mode and pull default. Receives `appSettings` (Main.qml's `Settings` instance) as a required property so both dialog and Main.qml share one live object.

**Files:**
- Create: `ui/qml/OptionsDialog.qml`
- Modify: `ui/qml/qml.qrc`

**Interfaces:**
- Consumes: `required property var appSettings` — the `Settings { id: appSettings }` instance from Main.qml.
- On theme change: writes `appSettings.themeMode` (auto-persists) + calls `theme.setMode(value)` live.
- On pull change: writes `appSettings.pullRebase` (auto-persists); Main.qml Connections propagate it to `repoVm.applyPullDefault()`.

- [ ] **Step 1: Create `ui/qml/OptionsDialog.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// App-level settings: theme mode and pull default. Changes apply instantly (no
// OK/Cancel). Receives appSettings from Main.qml — writes go to the shared
// Settings instance so they auto-persist and trigger Main.qml bindings.
Dialog {
    id: dialog
    objectName: "optionsDialog"
    modal: true
    title: "Options"
    anchors.centerIn: parent
    width: 360
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    required property var appSettings

    background: OverlayCard {}

    contentItem: ColumnLayout {
        spacing: 20

        // Theme section
        ColumnLayout {
            spacing: 8

            Label {
                text: "Theme"
                color: theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ButtonGroup { id: themeGroup }

            RowLayout {
                spacing: 16

                AppRadioButton {
                    objectName: "themeSystemRadio"
                    text: "System"
                    ButtonGroup.group: themeGroup
                    checked: dialog.appSettings.themeMode === 0
                    onClicked: {
                        dialog.appSettings.themeMode = 0
                        theme.setMode(0)
                    }
                }
                AppRadioButton {
                    objectName: "themeDarkRadio"
                    text: "Dark"
                    ButtonGroup.group: themeGroup
                    checked: dialog.appSettings.themeMode === 1
                    onClicked: {
                        dialog.appSettings.themeMode = 1
                        theme.setMode(1)
                    }
                }
                AppRadioButton {
                    objectName: "themeLightRadio"
                    text: "Light"
                    ButtonGroup.group: themeGroup
                    checked: dialog.appSettings.themeMode === 2
                    onClicked: {
                        dialog.appSettings.themeMode = 2
                        theme.setMode(2)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.border
        }

        // Pull default section
        ColumnLayout {
            spacing: 8

            Label {
                text: "Pull default"
                color: theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ButtonGroup { id: pullGroup }

            RowLayout {
                spacing: 16

                AppRadioButton {
                    objectName: "pullMergeRadio"
                    text: "Merge"
                    ButtonGroup.group: pullGroup
                    checked: !dialog.appSettings.pullRebase
                    onClicked: dialog.appSettings.pullRebase = false
                }
                AppRadioButton {
                    objectName: "pullRebaseRadio"
                    text: "Rebase"
                    ButtonGroup.group: pullGroup
                    checked: dialog.appSettings.pullRebase
                    onClicked: dialog.appSettings.pullRebase = true
                }
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        Button {
            objectName: "optionsCloseButton"
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
```

- [ ] **Step 2: Register in `ui/qml/qml.qrc`**

```xml
<file>OptionsDialog.qml</file>
```

- [ ] **Step 3: Build**

```bash
cmake --build build --parallel 2>&1 | tail -5
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add ui/qml/OptionsDialog.qml ui/qml/qml.qrc
git commit -m "feat(ui): add OptionsDialog (theme + pull default)"
```

---

### Task 7: Main.qml — frameless window, TitleBar, Settings, pull wiring

The central integration task. Rewrites `Main.qml` to: set frameless flags and minimum size; add a `Settings` block for theme/pull/geometry; place `TitleBar` above the existing content; wire pull-default to `repoVm`; restore geometry on startup; add `OptionsDialog` and `AboutDialog`; add seven `EdgeResizer` zones.

**Files:**
- Modify: `ui/qml/Main.qml`

**Interfaces:**
- Consumes: `TitleBar`, `EdgeResizer`, `OptionsDialog`, `AboutDialog` (all created in Tasks 3–6).
- Consumes: `appSettings.pullRebase` → `repoVm.applyPullDefault(bool)` (Task 1).
- `appSettings` (`Settings` instance) is passed to `OptionsDialog` as `appSettings: appSettings`.

- [ ] **Step 1: Write the failing test — TitleBar is present in the loaded window**

In `tests/ui/test_qml_shell.cpp`, add a new slot to `TestQmlShell`:

```cpp
void title_bar_is_present()
{
    ThemeManager mgr;
    mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    QCOMPARE(engine.rootObjects().size(), 1);
    QObject* bar = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("titleBar"));
    QVERIFY(bar != nullptr);
}

void options_and_about_dialogs_exist()
{
    ThemeManager mgr;
    mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

    QCOMPARE(engine.rootObjects().size(), 1);
    QObject* root = engine.rootObjects().first();
    QVERIFY(root->findChild<QObject*>(QStringLiteral("optionsDialog")) != nullptr);
    QVERIFY(root->findChild<QObject*>(QStringLiteral("aboutDialog")) != nullptr);
    QVERIFY(root->findChild<QObject*>(QStringLiteral("appMenuPopup")) != nullptr);
}
```

- [ ] **Step 2: Run to confirm tests fail** (TitleBar not yet in Main.qml)

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui -V 2>&1 | grep -A3 "title_bar\|options_and_about"
```

Expected: FAIL — `titleBar` not found.

- [ ] **Step 3: Rewrite `Main.qml`**

Replace the entire contents of `ui/qml/Main.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: window
    objectName: "appWindow"
    visible: true
    flags: Qt.FramelessWindowHint | Qt.Window
    minimumWidth: 860
    minimumHeight: 560
    color: theme.surfaceBase

    // App-level settings: persisted via QSettings (platform-native storage).
    // themeMode: 0=System 1=Dark 2=Light; default System.
    // pullRebase: global pull strategy; default true (rebase).
    // Window geometry: restored on startup; default is Maximized on first run.
    Settings {
        id: appSettings
        property int themeMode: 0
        property bool pullRebase: true
        property int windowX: 0
        property int windowY: 0
        property int windowWidth: 1100
        property int windowHeight: 720
        property int windowVisibility: Window.Maximized
    }

    Component.onCompleted: {
        if (appSettings.windowVisibility === Window.Maximized) {
            window.showMaximized()
        } else {
            window.x = appSettings.windowX
            window.y = appSettings.windowY
            window.width = appSettings.windowWidth
            window.height = appSettings.windowHeight
            window.showNormal()
        }
        if (repoVm) repoVm.applyPullDefault(appSettings.pullRebase)
        openFirstRepo()
    }

    // Persist geometry (skip Minimized so closing while minimised doesn't restore tiny)
    onXChanged: if (window.visibility === Window.Windowed) appSettings.windowX = x
    onYChanged: if (window.visibility === Window.Windowed) appSettings.windowY = y
    onWidthChanged: if (window.visibility === Window.Windowed) appSettings.windowWidth = width
    onHeightChanged: if (window.visibility === Window.Windowed) appSettings.windowHeight = height
    onVisibilityChanged: {
        if (visibility !== Window.Minimized && visibility !== Window.Hidden)
            appSettings.windowVisibility = visibility
    }

    // Propagate pull-default changes from OptionsDialog to the view model
    Connections {
        target: appSettings
        function onPullRebaseChanged() {
            if (repoVm) repoVm.applyPullDefault(appSettings.pullRebase)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            id: titleBar
            Layout.fillWidth: true
            onOptionsRequested: optionsDialog.open()
            onAboutRequested: aboutDialog.open()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.fillHeight: true
                Layout.preferredWidth: 272
                onAddExistingRequested: addExistingFolder.open()
                onCloneRequested: cloneRepoDialog.openDialog()
                onInitRequested: initRepoDialog.openDialog()
                onNewProjectRequested: newProjectDialog.openDialog()
                onDeleteProjectRequested: deleteProjectDialog.open()
            }

            WorkingPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                onAddExistingRequested: addExistingFolder.open()
                onCloneRequested: cloneRepoDialog.openDialog()
                onInitRequested: initRepoDialog.openDialog()
                onNewProjectRequested: newProjectDialog.openDialog()
            }
        }
    }

    // ---- Edge resize zones (7 zones — no top: title bar drag covers it) ----
    // Left
    EdgeResizer {
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 4
        edges: Qt.LeftEdge
        active: window.visibility !== Window.Maximized
    }
    // Right
    EdgeResizer {
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 4
        edges: Qt.RightEdge
        active: window.visibility !== Window.Maximized
    }
    // Bottom
    EdgeResizer {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 4
        edges: Qt.BottomEdge
        active: window.visibility !== Window.Maximized
    }
    // Bottom-left corner
    EdgeResizer {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        width: 10; height: 10
        edges: Qt.LeftEdge | Qt.BottomEdge
        active: window.visibility !== Window.Maximized
    }
    // Bottom-right corner
    EdgeResizer {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 10; height: 10
        edges: Qt.RightEdge | Qt.BottomEdge
        active: window.visibility !== Window.Maximized
    }
    // Top-left corner
    EdgeResizer {
        anchors.left: parent.left; anchors.top: parent.top
        width: 10; height: 10
        edges: Qt.LeftEdge | Qt.TopEdge
        active: window.visibility !== Window.Maximized
    }
    // Top-right corner
    EdgeResizer {
        anchors.right: parent.right; anchors.top: parent.top
        width: 10; height: 10
        edges: Qt.RightEdge | Qt.TopEdge
        active: window.visibility !== Window.Maximized
    }

    // ---- Transient error banner ----
    Rectangle {
        id: errorBanner
        objectName: "errorBanner"
        property string message: ""
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 52
        width: Math.min(parent.width - 48, bannerLabel.implicitWidth + 32)
        height: 36
        radius: 10
        visible: message.length > 0
        color: theme.surfaceOverlay
        border.color: theme.stateDeleted
        border.width: 1
        z: 100

        Label {
            id: bannerLabel
            anchors.centerIn: parent
            text: errorBanner.message
            color: theme.textPrimary
            font.pixelSize: 12
        }

        Timer {
            id: bannerTimer
            interval: 5000
            onTriggered: errorBanner.message = ""
        }
        onMessageChanged: if (message.length > 0) bannerTimer.restart()
        function show(msg) { message = msg }
    }

    // ---- App dialogs ----
    OptionsDialog {
        id: optionsDialog
        appSettings: appSettings
    }
    AboutDialog { id: aboutDialog }

    // ---- Repo-management dialogs (window-scoped so they centre on the whole window) ----
    InitRepoDialog { id: initRepoDialog }
    CloneRepoDialog {
        id: cloneRepoDialog
        onCloneStarted: cloneProgressDialog.openDialog()
    }
    CloneProgressDialog { id: cloneProgressDialog }
    NewProjectDialog { id: newProjectDialog }
    CredentialDialog {
        id: credentialDialog
        onAccepted: if (repoVm) repoVm.submitCredentials(username, token)
    }

    Dialog {
        id: deleteProjectDialog
        objectName: "deleteProjectDialog"
        modal: true
        title: "Delete project"
        anchors.centerIn: parent
        width: 400
        padding: 20
        background: OverlayCard {}

        contentItem: Label {
            text: (projectController && projectController.activeProjectName.length > 0)
                  ? ("Remove the project “" + projectController.activeProjectName
                     + "”? The repositories stay on disk — only this grouping is removed.")
                  : "Remove this project?"
            color: theme.textPrimary
            wrapMode: Text.WordWrap
            font.pixelSize: 13
        }

        footer: RowLayout {
            spacing: 8
            Layout.margins: 16
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: deleteProjectDialog.reject() }
            Button {
                objectName: "deleteProjectConfirm"
                text: "Delete"
                onClicked: deleteProjectDialog.accept()
            }
        }

        onAccepted: if (projectController) projectController.removeProject()
    }

    FolderDialog {
        id: addExistingFolder
        title: "Choose a repository folder"
        onAccepted: if (projectController)
                        projectController.addExistingRepo(selectedFolder.toString().replace(/^file:\/\//, ""))
    }

    // ---- Auto-open a repository ----
    function openFirstRepo() {
        if (!repoVm) return
        if (projectController && projectController.activeProjectId.length > 0
                && repoModel && repoModel.rowCount() > 0)
            repoVm.open(repoModel.firstRepoPath())
        else
            repoVm.close()
    }

    Connections {
        target: projectController
        enabled: projectController !== null
        function onRepoAdded(path) { if (repoVm) repoVm.open(path) }
        function onActiveProjectChanged() { window.openFirstRepo() }
        function onRepoAddFailed(message) { errorBanner.show(message) }
    }

    Connections {
        target: repoVm
        enabled: repoVm !== null
        function onAuthRequired() { credentialDialog.openDialog() }
        function onOperationFailed(message) { errorBanner.show(message) }
    }
}
```

- [ ] **Step 4: Build and run all UI tests**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui
```

Expected: all tests pass including `title_bar_is_present` and `options_and_about_dialogs_exist`.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/Main.qml tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): frameless window with TitleBar, Settings, OptionsDialog, AboutDialog"
```

---

### Task 8: Remove obsolete UI controls — Sidebar theme toggle + BranchBar pull-strategy button

The theme toggle moves to OptionsDialog; the pull-strategy `⋯` button is replaced by the global default. Delete both from their parent components.

**Files:**
- Modify: `ui/qml/Sidebar.qml`
- Modify: `ui/qml/BranchBar.qml`
- Modify: `tests/ui/test_qml_shell.cpp`

- [ ] **Step 1: Write failing tests — controls are gone**

In `tests/ui/test_qml_shell.cpp`, add to `TestQmlShell`:

```cpp
void theme_toggle_removed_from_sidebar()
{
    ThemeManager mgr;
    mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QCOMPARE(engine.rootObjects().size(), 1);

    // themeToggle was removed from Sidebar — must not be found
    QObject* toggle = engine.rootObjects().first()->findChild<QObject*>(
        QStringLiteral("themeToggle"));
    QVERIFY(toggle == nullptr);
}
```

- [ ] **Step 2: Run to confirm test fails** (themeToggle still present)

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui -V 2>&1 | grep -A3 "theme_toggle"
```

Expected: FAIL — `toggle` is not nullptr.

- [ ] **Step 3: Remove theme-toggle button from `Sidebar.qml`**

In `ui/qml/Sidebar.qml`, delete the entire `Button { id: themeToggle ... }` block (lines 84–106 of the original file — the block that shows ☾/☀/◐ and calls `theme.cycleMode()`). The surrounding `RowLayout` and other buttons stay; only this one button is removed.

- [ ] **Step 4: Remove pull-strategy button from `BranchBar.qml`**

In `ui/qml/BranchBar.qml`, delete the entire pull-strategy button block at the end of the `RowLayout` — the `Button { text: "⋯" ... AppMenu { id: pullMenu ... } }` block (lines 203–231 of the original file).

- [ ] **Step 5: Build and run all UI tests**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R gittide_ui
```

Expected: all tests pass including `theme_toggle_removed_from_sidebar`.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/Sidebar.qml ui/qml/BranchBar.qml tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): remove sidebar theme toggle and branch-bar pull-strategy button"
```

---

## Self-Review

**Spec coverage:**

| Spec requirement | Task |
|-----------------|------|
| `QSettings` persistence for theme + pull default | Tasks 2, 7 |
| Read theme from QSettings before QML loads (no flash) | Task 2 |
| Window launches maximised by default | Task 7 |
| Window geometry restore | Task 7 |
| `minimumWidth: 860 / minimumHeight: 560` | Task 7 |
| `Qt.FramelessWindowHint` | Task 7 |
| `startSystemMove()` on drag area | Task 5 (TitleBar) |
| Double-click to maximise/restore | Task 5 (TitleBar) |
| App icon button → app menu popup | Task 5 (TitleBar) |
| App menu: Options / About / Quit | Task 5 (TitleBar) |
| macOS: traffic lights on left | Task 5 (TitleBar) |
| Win/Linux: controls on right | Task 5 (TitleBar) |
| 7 edge resize zones | Task 7 |
| `AppRadioButton` component | Task 3 |
| `WindowButton` component | Task 4 |
| `EdgeResizer` component | Task 4 |
| OptionsDialog — theme radio buttons | Task 6 |
| OptionsDialog — pull-default radio buttons | Task 6 |
| OptionsDialog — instant apply (no OK/Cancel) | Task 6 |
| AboutDialog — version from `appVersion` | Task 5 |
| `appVersion` context property | Task 2 |
| `applyPullDefault` replaces per-repo `setPullRebase` | Task 1 |
| `loadPullStrategy()` call removed from `open()` | Task 1 |
| Theme toggle removed from Sidebar | Task 8 |
| `⋯` pull-strategy button removed from BranchBar | Task 8 |

All spec requirements covered. ✓

**Type consistency check:**
- `applyPullDefault(bool)` used in Tasks 1, 7 — consistent.
- `appSettings.pullRebase` (bool) used in Tasks 6, 7 — consistent.
- `appSettings.themeMode` (int) used in Tasks 2, 6, 7 — consistent.
- `installQmlContext(..., const QString& appVersion = {})` — used in Tasks 2 and test — consistent.
- `TitleBar` signals `optionsRequested()` / `aboutRequested()` — emitted in Task 5, connected in Task 7 — consistent.
- `objectName: "titleBar"` — set in Task 5, asserted in Task 7 test — consistent.
