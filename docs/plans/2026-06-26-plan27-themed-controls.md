# Plan 27 — Themed controls (AppButton + AppComboBox)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax; tick them as you go.
> REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-06-26 |
| **Status** | `done` |
| **Spec** | [spec/product/2026-06-26-themed-controls-design.md](../spec/product/2026-06-26-themed-controls-design.md) |
| **Depends on** | — |

**Goal:** Replace the remaining un-themed Qt Quick **Basic** controls (raw dialog
buttons, the submodule Init `ToolButton`, raw `ComboBox`es incl.
DeleteBranchDialog) with one themed `AppButton` (primary/secondary/danger +
compact) and one themed `AppComboBox`.

**Architecture:** Two new drop-in QML components in `ui/qml/` styled exactly like
the existing `AppCheckBox`/`AppRadioButton` (subclass the Basic control, override
`contentItem`/`background` off `theme.*`). Then a pure-QML migration of every
plain action button and the three combos to use them. No C++ change; every
`objectName` preserved so the existing UI test suite is the regression net.

**Tech stack:** Qt 6 Quick/QML (Controls.Basic), QtTest headless runner.

## Global constraints

- **Pure QML restyle — no C++/behaviour change.** Click handlers, model bindings,
  `enabled` conditions, and the delete-confirm arming logic stay identical.
- **Every `objectName` preserved verbatim.** A dropped/renamed `objectName`
  silently breaks a test and a context hook — this is the #1 risk.
- **Colour from `theme.*` tokens only** — no hex. Reuse the existing palette
  (`accent`, `accentHover`, `surfaceBase`, `surfaceRaised`, `surfaceOverlay`,
  `border`, `textPrimary`, `textMuted`, `stateDeleted`).
- **Components live in `ui/qml/` and are registered in `ui/qml/qml.qrc`.** Same-dir
  QML types resolve without an `import` (as `AppCheckBox` does today).
- **Out of scope:** `AppTextField` (TextFields already themed); `MainTab`,
  `WindowButton`, `AppRadioButton`/`AppCheckBox`, `EmptyState.Cta` (keep bespoke).
- **TDD:** failing test first for the new components (Tasks 1–2). The migration
  tasks (3–5) are regression-guarded by the existing suite.

### Build & test — REAL invocations

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/michal/Qt/6.8.3/gcc_64 -DGITGUI_BUILD_QML=ON   # once
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests        # ONE QtTest binary, all classes
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure   # full suite
```

UI tests are **QtTest** `QObject` classes run by the shared `tests/ui/main.cpp`.
A NEW ui test class needs three edits: add the source to `gittide_ui_test_sources`
in `tests/CMakeLists.txt`, `#include "test_<name>.cpp"` in `tests/ui/main.cpp`, and
`RUN(TestClass);` in its `main()`. New QML files must be added to `ui/qml/qml.qrc`.
Mirror `tests/ui/test_qml_history.cpp` (QML load via `QQmlComponent`/`engine.load`
+ `findChild`).

---

## Task 1: `AppButton.qml` + load test

**Files:**
- Create: `ui/qml/AppButton.qml`
- Modify: `ui/qml/qml.qrc` (register the file)
- Create: `tests/ui/test_qml_appcontrols.cpp` (+ register in CMake & `main.cpp`)

**Interfaces:**
- Produces: `AppButton` — a `Button` with `property string variant` (`"primary"` |
  `"secondary"` | `"danger"`, default `"primary"`) and `property bool compact`
  (default false). All Button API (`text`/`enabled`/`onClicked`/`objectName`/
  `Layout.*`) passes through.

- [ ] **Step 1: Create `ui/qml/AppButton.qml`:**

```qml
import QtQuick
import QtQuick.Controls.Basic

// Themed action button. Drop-in for Basic Button (same text/enabled/onClicked
// API). `variant` picks primary (filled accent), secondary (outline), or danger
// (filled red); `compact` shrinks it for inline use (e.g. the submodule Init pill).
Button {
    id: btn
    property string variant: "primary"   // "primary" | "secondary" | "danger"
    property bool   compact: false

    implicitHeight: compact ? 22 : 30
    leftPadding:  compact ? 8 : 14
    rightPadding: compact ? 8 : 14
    topPadding: 0
    bottomPadding: 0

    readonly property bool _filled: variant === "primary" || variant === "danger"
    readonly property color _fill: variant === "danger" ? theme.stateDeleted : theme.accent
    readonly property color _fillHover: variant === "danger"
                                        ? Qt.darker(theme.stateDeleted, 1.2)
                                        : theme.accentHover

    contentItem: Label {
        text: btn.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: btn.compact ? 11 : 12
        color: !btn.enabled ? theme.textMuted
               : btn._filled ? theme.surfaceBase
               : theme.textPrimary
    }

    background: Rectangle {
        radius: 6
        color: !btn.enabled ? theme.surfaceOverlay
               : btn._filled ? (btn.hovered ? btn._fillHover : btn._fill)
               : (btn.hovered ? theme.surfaceOverlay : "transparent")
        border.width: btn.variant === "secondary" ? 1 : 0
        border.color: theme.border
    }
}
```

- [ ] **Step 2: Register in `ui/qml/qml.qrc`** — add next to `AppCheckBox.qml`:

```xml
    <file>AppButton.qml</file>
```

- [ ] **Step 3: Write the failing test** `tests/ui/test_qml_appcontrols.cpp` as a
  QtTest class `TestQmlAppControls`. Mirror `test_qml_history.cpp`'s
  `QQmlComponent`/engine setup (it needs the `theme` context property — construct a
  `QmlTheme` and set it as a context property, or load via the same harness the
  history test uses for `GraphColumn`/`CommitDetail` component tests). First slot:

```cpp
    void app_button_instantiates_all_variants_and_compact()
    {
        QQmlEngine engine;
        // theme context property — set up exactly as the other qml component
        // tests in this suite do (QmlTheme instance as context property).
        installThemeContext(engine);  // mirror the helper / inline setup used by neighbours

        for (const QString& v : {QStringLiteral("primary"),
                                 QStringLiteral("secondary"),
                                 QStringLiteral("danger")})
        {
            QQmlComponent comp(&engine);
            comp.setData(("import QtQuick\nAppButton { variant: \"" + v +
                          "\"; text: \"X\"; objectName: \"b\" }").toUtf8(), QUrl());
            std::unique_ptr<QObject> obj(comp.create());
            QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));
            QCOMPARE(obj->property("text").toString(), QStringLiteral("X"));
            QCOMPARE(obj->objectName(), QStringLiteral("b"));
        }

        // compact shrinks implicitHeight.
        QQmlComponent reg(&engine);
        reg.setData("import QtQuick\nAppButton { text: \"X\" }", QUrl());
        std::unique_ptr<QObject> regular(reg.create());
        QQmlComponent cmp(&engine);
        cmp.setData("import QtQuick\nAppButton { text: \"X\"; compact: true }", QUrl());
        std::unique_ptr<QObject> compact(cmp.create());
        QVERIFY(compact->property("implicitHeight").toReal()
                < regular->property("implicitHeight").toReal());
    }
```
  Register the new file: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_appcontrols.cpp`
  to `gittide_ui_test_sources` in `tests/CMakeLists.txt`; add
  `#include "test_qml_appcontrols.cpp"` and `RUN(TestQmlAppControls);` to
  `tests/ui/main.cpp`. NOTE: `AppButton` resolves from the QRC same-dir import —
  if `setData` can't resolve the bare type, load it via
  `QQmlComponent(&engine, QUrl("qrc:/qml/AppButton.qml"))` and set properties
  through QML by wrapping, OR add `import "qrc:/qml" as App` and use `App.AppButton`.
  Use whichever resolves the type in this harness (check how a neighbour test
  instantiates `GraphColumn`/`CommitDetail`).

- [ ] **Step 4: Build + run, verify RED then implement to GREEN:**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```
First run before the qrc/component exist → FAIL/compile error; after Steps 1–2 → the
slot passes.

- [ ] **Step 5: Commit.**

```bash
git add ui/qml/AppButton.qml ui/qml/qml.qrc tests/ui/test_qml_appcontrols.cpp \
        tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): themed AppButton (primary/secondary/danger + compact)"
```

---

## Task 2: `AppComboBox.qml` + load test

**Files:**
- Create: `ui/qml/AppComboBox.qml`
- Modify: `ui/qml/qml.qrc`
- Test: extend `tests/ui/test_qml_appcontrols.cpp` (`TestQmlAppControls`)

**Interfaces:**
- Produces: `AppComboBox` — a themed `ComboBox`; all ComboBox API
  (`model`/`currentIndex`/`currentText`/`textRole`/`objectName`/signals) passes
  through.

- [ ] **Step 1: Create `ui/qml/AppComboBox.qml`:**

```qml
import QtQuick
import QtQuick.Controls.Basic

// Themed dropdown. Drop-in for Basic ComboBox (same model/currentIndex/currentText
// API). Themed field, popup card, and delegates off the surface palette.
ComboBox {
    id: combo
    implicitHeight: 30

    contentItem: Label {
        leftPadding: 10
        rightPadding: combo.indicator.width + 10
        text: combo.displayText
        color: theme.textPrimary
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 6
        color: theme.surfaceBase
        border.width: 1
        border.color: (combo.activeFocus || combo.pressed) ? theme.accent : theme.border
    }

    indicator: Label {
        x: combo.width - width - 10
        anchors.verticalCenter: combo.verticalCenter
        text: "▾"
        color: theme.textMuted
        font.pixelSize: 12
    }

    delegate: ItemDelegate {
        id: itemDel
        width: combo.width
        height: 28
        highlighted: combo.highlightedIndex === index
        contentItem: Label {
            text: combo.textRole.length ? (model[combo.textRole] ?? "")
                                        : (modelData ?? "")
            color: theme.textPrimary
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
        }
        background: Rectangle {
            color: itemDel.highlighted ? theme.surfaceOverlay : "transparent"
        }
    }

    popup: Popup {
        y: combo.height + 2
        width: combo.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 240)
        padding: 1
        background: Rectangle {
            radius: 6
            color: theme.surfaceRaised
            border.color: theme.border
            border.width: 1
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
            ScrollBar.vertical: AppScrollBar {}
        }
    }
}
```
  (The three target combos use plain string-list models, so `modelData` feeds the
  delegate; the `textRole` branch covers any object model.)

- [ ] **Step 2: Register in `ui/qml/qml.qrc`:**

```xml
    <file>AppComboBox.qml</file>
```

- [ ] **Step 3: Add a failing slot** to `TestQmlAppControls`:

```cpp
    void app_combobox_exposes_model_and_current()
    {
        QQmlEngine engine;
        installThemeContext(engine);
        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/AppComboBox.qml")));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));
        obj->setProperty("model", QStringList{"a", "b", "c"});
        QCOMPARE(obj->property("count").toInt(), 3);
        obj->setProperty("currentIndex", 1);
        QCOMPARE(obj->property("currentText").toString(), QStringLiteral("b"));
    }
```

- [ ] **Step 4: Build + run RED→GREEN:**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```

- [ ] **Step 5: Commit.**

```bash
git add ui/qml/AppComboBox.qml ui/qml/qml.qrc tests/ui/test_qml_appcontrols.cpp
git commit -m "feat(ui): themed AppComboBox"
```

---

## Task 3: Migrate dialog-footer buttons → `AppButton`

**Files (modify):** `ui/qml/AboutDialog.qml`, `CloneRepoDialog.qml`,
`CloneProgressDialog.qml`, `CredentialDialog.qml`, `DiscardChangesDialog.qml`,
`InitRepoDialog.qml`, `NewBranchDialog.qml`, `NewProjectDialog.qml`,
`OptionsDialog.qml`, `RebaseTargetDialog.qml`, `RenameBranchDialog.qml`,
`ReorderConfirmDialog.qml`, `RewordDialog.qml`, `Main.qml` (delete-project footer).
(DeleteBranchDialog + RebaseTodoDialog buttons are handled in Tasks 4/5 alongside
their combos.)

**Transformation rule (apply per button):** replace `Button { … }` with
`AppButton { … }`, keep every `objectName`/`text`/`enabled`/`onClicked`/`Layout.*`
binding verbatim, **delete** any inline `contentItem`/`background` (AppButton
supplies them), and add `variant:`:
- Cancel / Close / Later / "Keep" → `variant: "secondary"`
- the affirmative action (Save / Create / Initialize / Clone / OK / Apply / Add) →
  `variant: "primary"`
- a destructive affirmative (Discard, Delete, Remove) → `variant: "danger"`

**Worked example — `RewordDialog.qml` footer (lines ~104–112):**

Before:
```qml
        Button { text: "Cancel"; onClicked: dialog.close() }
        Button {
            id: saveButton
            objectName: "rewordSave"
            text: "Save"
            enabled: summaryField.text.trim().length > 0
            onClicked: dialog.accept()
        }
```
After:
```qml
        AppButton { variant: "secondary"; text: "Cancel"; onClicked: dialog.close() }
        AppButton {
            id: saveButton
            objectName: "rewordSave"
            variant: "primary"
            text: "Save"
            enabled: summaryField.text.trim().length > 0
            onClicked: dialog.accept()
        }
```

- [ ] **Step 1:** Apply the rule to every dialog footer in the file list above.
  For `DiscardChangesDialog.qml` the confirm is destructive → `variant: "danger"`.
  For `Main.qml`'s delete-project footer the confirm (objectName for the delete
  action) is destructive → `variant: "danger"`; Cancel → `"secondary"`. Where a
  button already had a themed `contentItem`/`background` inline, remove that block.

- [ ] **Step 2: Build + run the full UI suite** — the existing tests reference these
  buttons by `objectName` and must stay green:

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```
Expected: 0 failures. If a slot fails to find an `objectName`, you dropped it —
restore it on the `AppButton`.

- [ ] **Step 3: Commit.**

```bash
git add ui/qml/*.qml
git commit -m "refactor(ui): migrate dialog-footer buttons to AppButton"
```

---

## Task 4: Migrate in-pane buttons + the submodule Init pill

**Files (modify):** `ui/qml/ChangesPane.qml` (`commitButton`),
`ui/qml/CommitDetail.qml` (`checkoutCommitButton`), `ui/qml/Sidebar.qml`
(`fetchAllButton`, `addRepoButton`, the Init `ToolButton`), `ui/qml/DiffView.qml`
(the 3 action buttons), `ui/qml/MergeBanner.qml` (the "Deinit submodules & retry"
button).

**Interfaces:** Consumes `AppButton` (Task 1).

- [ ] **Step 1: `commitButton`** (`ChangesPane.qml:279`) → `AppButton`:
```qml
            AppButton {
                id: commitButton
                objectName: "commitButton"
                variant: "primary"
                Layout.fillWidth: true
                enabled: repoVm && repoVm.checkedCount > 0 && commitSummary.text.length > 0
                text: repoVm
                      ? ("Commit " + repoVm.checkedCount + " file" + (repoVm.checkedCount === 1 ? "" : "s")
                         + " to " + repoVm.currentBranch)
                      : "Commit"
                onClicked: { if (repoVm) repoVm.commit(commitSummary.text, commitDescription.text) }
            }
```
  (Drop the inline `contentItem`/`background`; the dynamic label moves to `text:`.)

- [ ] **Step 2: `checkoutCommitButton`** (`CommitDetail.qml:43`) → `AppButton {
  variant: "secondary"; text: "Checkout"; … }`, keeping `objectName`/`visible`/
  `onClicked`; drop inline `contentItem`/`background`.

- [ ] **Step 3: Sidebar `fetchAllButton` (line 60) and `addRepoButton` (line 433)**
  → `AppButton`. Read each button's current inline styling first: if it is an
  icon/text action, keep its `text`/icon and pick `variant: "secondary"` (toolbar
  affordances) unless it currently reads as a filled primary. Preserve
  `objectName`/`onClicked`/`enabled`/tooltip.

- [ ] **Step 4: Submodule Init** (`Sidebar.qml:312`, currently `ToolButton { text:
  "Init" }`) → compact pill:
```qml
                    AppButton {
                        variant: "primary"
                        compact: true
                        visible: row.uninit && !model.submoduleBusy
                        text: "Init"
                        onClicked: { if (projectController) projectController.initSubmodule(model.ownerRepoPath, model.repoPath) }
                    }
```
  Keep it within the row's `RowLayout` with `Layout.alignment: Qt.AlignVCenter` so
  it sits inside the row height (the `compact` height 22 fits a list row).

- [ ] **Step 5: `DiffView.qml` buttons (87/94/101) and `MergeBanner.qml`'s
  "Deinit submodules & retry"** → `AppButton`. Read each first; map to `secondary`
  (neutral inline actions) or `danger` (the deinit-retry is recovery, not
  destructive of user data → `secondary`). Preserve `objectName`/`onClicked`/text.

- [ ] **Step 6: Build + run the full UI suite** — `commitButton`,
  `checkoutCommitButton`, `fetchAllButton`, `addRepoButton` are all referenced by
  tests; must stay green:
```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```

- [ ] **Step 7: Commit.**
```bash
git add ui/qml/*.qml
git commit -m "refactor(ui): migrate in-pane buttons to AppButton; compact Init pill"
```

---

## Task 5: Migrate ComboBoxes → `AppComboBox`; finish DeleteBranchDialog

**Files (modify):** `ui/qml/DeleteBranchDialog.qml`, `ui/qml/NewBranchDialog.qml`,
`ui/qml/RebaseTodoDialog.qml`.

**Interfaces:** Consumes `AppComboBox` (Task 2), `AppButton` (Task 1).

- [ ] **Step 1: `DeleteBranchDialog.qml`** — three changes, all behaviour-preserving:
  - `ComboBox { id: branchCombo; objectName: "deleteBranchTarget"; … }` →
    `AppComboBox { … }` (keep `objectName`, `Layout.fillWidth`,
    `onCurrentTextChanged`).
  - Cancel `Button { text: "Cancel"; onClicked: dialog.close() }` →
    `AppButton { variant: "secondary"; text: "Cancel"; onClicked: dialog.close() }`.
  - The danger `Button { id: dangerButton; objectName: "deleteBranchConfirm"; … }`
    → `AppButton { id: dangerButton; objectName: "deleteBranchConfirm"; variant:
    "danger"; enabled: branchCombo.count > 0; text: <unchanged ternary>; onClicked:
    <unchanged> }`. **Drop** the inline `contentItem`/`background` (AppButton danger
    supplies the red fill). Keep the `text:` ternary ("Force delete" / "Click again
    to delete" / "Delete") and the full `onClicked` arming logic verbatim.

- [ ] **Step 2: `NewBranchDialog.qml`** — `ComboBox { id: baseCombo; objectName:
  "newBranchBase"; … }` → `AppComboBox` (keep `objectName`/`Layout.fillWidth`/
  `visible`). Its footer Cancel/Create buttons → `AppButton` (`secondary`/`primary`)
  if not already done — keep `objectName: "newBranchCreate"` and the `enabled`
  binding.

- [ ] **Step 3: `RebaseTodoDialog.qml`** — the per-row action `ComboBox` (line 189)
  → `AppComboBox`, preserving its `model`/`currentIndex`/`textRole` and the
  handler that sets the row's action. Its footer buttons (210/217/261/265) →
  `AppButton` with appropriate variants, preserving `objectName`s.

- [ ] **Step 4: Build + run the full UI suite** — `deleteBranchTarget`,
  `deleteBranchConfirm`, `newBranchBase`, `newBranchCreate`, and any RebaseTodo
  hooks must stay green:
```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```

- [ ] **Step 5: Commit.**
```bash
git add ui/qml/*.qml
git commit -m "refactor(ui): migrate ComboBoxes to AppComboBox; theme DeleteBranchDialog"
```

---

## Task 6: Docs close-out

**Files:** `docs/spec/design/design.md`, the design doc, `docs/plans/index.md`,
this plan, `docs/decisions.md` (optional).

- [x] **Step 1:** In `docs/spec/design/design.md`, document `AppButton`
  (variants + compact) and `AppComboBox` as the standard themed controls — note
  that plain action buttons and dropdowns use these, not raw Basic controls. Flip
  the design doc (`2026-06-26-themed-controls-design.md`) **Status** to `shipped`.

- [x] **Step 2:** Add the Plan 27 row to `docs/plans/index.md` (mirror Plan 26's
  row format).

- [x] **Step 3:** Run the full suite to confirm nothing regressed:
```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

- [x] **Step 4:** Set this plan's **Status** to `done`, fill **Outcome**, commit:
```bash
git add docs/
git commit -m "docs: close out Plan 27 (themed controls)"
```

---

## Outcome

- Shipped: `AppButton` (primary / secondary / danger + compact) and `AppComboBox`
  as the two standard themed action controls; all dialog footers, in-pane buttons,
  and the three raw ComboBoxes migrated; the submodule Init affordance is now a
  compact primary pill; `DeleteBranchDialog` fully themed end-to-end.
- Spec updated: `design.md` §Components — AppButton / AppComboBox entries and the
  bespoke-controls note.
- Code: `ui/qml/AppButton.qml`, `ui/qml/AppComboBox.qml`, and all migrated
  dialogs/panes (`ChangesPane`, `CommitDetail`, `Sidebar`, `DiffView`,
  `MergeBanner`, `DeleteBranchDialog`, `NewBranchDialog`, `RebaseTodoDialog`, and
  all dialog footers). Tests: `tests/ui/test_qml_appcontrols.cpp`.
