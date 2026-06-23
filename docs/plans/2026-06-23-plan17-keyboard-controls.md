# Plan 17 — Keyboard Controls

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

| | |
|--|--|
| **Date** | 2026-06-23 |
| **Status** | `planned` |
| **Spec** | [`docs/spec/product/keyboard-controls.md`](../spec/product/keyboard-controls.md) |
| **Depends on** | Plan 16 (context menus) |

**Goal:** Wire full keyboard navigation for the review-and-commit loop — arrow keys through file and history lists, Space to stage/unstage, Ctrl+Enter to commit, global chords for tab switching and refresh, and a `?` shortcut overlay.

**Architecture:** Pure `ui/` — no `core/` changes. QML `Keys`/`Shortcut` handlers on existing `ListView` widgets. Three new `Q_INVOKABLE` row-selection methods on `RepoViewModel` (`selectFileAtRow`, `selectCommitAtRow`, `selectCommitFileAtRow`) so key handlers can address model items without reaching into C++ model internals from QML. A new `focusBorder` theme token for the focus ring. Each pane exposes `takeFocus()` so `WorkingPane`'s global shortcuts can route focus without referencing private IDs. New `ShortcutsHelpPopup.qml` for the `?` overlay.

**Tech stack:** Qt Quick / QML `Keys`, `KeyNavigation`, `Shortcut`, `activeFocusOnTab`. C++ `Q_INVOKABLE`. No `core/` changes.

## Global Constraints

- **No Qt in `core/`** — all changes are in `ui/`.
- **No hex literals** — colours via `theme.*` tokens only. Focus rings use `theme.focusBorder`.
- New QML files must be added to `ui/qml/qml.qrc`.
- New tests require TWO edits: add to `gittide_ui_test_sources` in `tests/CMakeLists.txt` **and** `#include` + `RUN()` in `tests/ui/main.cpp`. Missing either silently skips all tests in that file.
- Build: `cmake --build build --parallel`; test: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`.
- `Qt.ControlModifier` maps to Cmd on macOS automatically — no platform ifdefs.
- All list `Keys` handlers guard with `if (repoVm)` before calling ViewModel methods.

---

## Task 1: `focusBorder` theme token

**Files:**
- Modify: `ui/include/gittide/ui/theme.hpp`
- Modify: `ui/src/theme.cpp`
- Modify: `ui/include/gittide/ui/qmltheme.hpp`
- Modify: `ui/src/qmltheme.cpp`
- Test: `tests/ui/test_theme.cpp` (existing file — add one slot)

**Interfaces:**
- Produces: `Theme::focusBorder` — `QString` hex colour, equal to `accent` in both themes.
- Produces: `QmlTheme::focusBorder()` — `QColor`, Q_PROPERTY `focusBorder` `NOTIFY changed`.
- QML can now write `border.color: theme.focusBorder`.

- [ ] **Step 1: Write the failing test**

  Open `tests/ui/test_theme.cpp`. The class is `TestTheme`. Add a new slot:

  ```cpp
  void focus_border_token_exists()
  {
      const auto dark  = gittide::ui::darkTheme();
      const auto light = gittide::ui::lightTheme();
      // focusBorder is defined as accent in both themes.
      QCOMPARE(dark.focusBorder,  dark.accent);
      QCOMPARE(light.focusBorder, light.accent);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: compile error — `focusBorder` does not exist on `Theme`.

- [ ] **Step 2: Add `focusBorder` to `Theme` struct**

  In `ui/include/gittide/ui/theme.hpp`, add `focusBorder` after `shadow`:

  ```cpp
  struct Theme
  {
      bool dark;
      QString surfaceBase, surfaceRaised, surfaceOverlay, border;
      QString textPrimary, textSecondary, textMuted;
      QString accent, accentHover, head;
      QString stateAdded, stateModified, stateDeleted, stateUntracked, stateConflict, stateIncoming;
      QString shadow;
      QString focusBorder;
  };
  ```

- [ ] **Step 3: Set `focusBorder` in both theme factories**

  In `ui/src/theme.cpp`, append `.focusBorder = QStringLiteral("#22D3EE")` in `darkTheme()` and `.focusBorder = QStringLiteral("#0891B2")` in `lightTheme()` — matching their respective `accent` values:

  ```cpp
  // darkTheme() — append inside the initialiser list:
  .focusBorder    = QStringLiteral("#22D3EE"),  // = accent

  // lightTheme() — append inside the initialiser list:
  .focusBorder    = QStringLiteral("#0891B2"),  // = accent
  ```

- [ ] **Step 4: Run test — expect PASS**

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: `focus_border_token_exists` PASS.

- [ ] **Step 5: Expose `focusBorder` on `QmlTheme`**

  In `ui/include/gittide/ui/qmltheme.hpp`, add after the `shadow` property:

  ```cpp
  Q_PROPERTY(QColor focusBorder READ focusBorder NOTIFY changed)
  ```

  And in the public section:

  ```cpp
  QColor focusBorder() const;
  ```

  In `ui/src/qmltheme.cpp`, add after `shadow()`:

  ```cpp
  QColor QmlTheme::focusBorder() const
  {
      return QColor(theme().focusBorder);
  }
  ```

- [ ] **Step 6: Build to confirm no errors**

  Run: `cmake --build build --parallel`
  Expected: clean build.

- [ ] **Step 7: Commit**

  ```bash
  git add ui/include/gittide/ui/theme.hpp ui/src/theme.cpp \
          ui/include/gittide/ui/qmltheme.hpp ui/src/qmltheme.cpp \
          tests/ui/test_theme.cpp
  git commit -m "feat(ui): add focusBorder theme token for keyboard focus rings"
  ```

---

## Task 2: ViewModel row-selection methods

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Test: `tests/ui/test_repo_view_model.cpp` (existing file — add slots)

**Interfaces:**
- Produces: `Q_INVOKABLE void selectFileAtRow(int row)` — calls `selectFile(m_files->pathAt(row))`.
- Produces: `Q_INVOKABLE void selectCommitAtRow(int row)` — reads OID via `m_history->data(…, OidRole)`, calls `selectCommit(oid)`.
- Produces: `Q_INVOKABLE void selectCommitFileAtRow(int row)` — calls `selectCommitFile(m_commitFiles->pathAt(row))`.
- These are used by QML `Keys` handlers that live on the `ListView` item (not a delegate) and cannot read individual row data from a C++ `QAbstractItemModel` via `model.get()`.

- [ ] **Step 1: Write failing tests**

  In `tests/ui/test_repo_view_model.cpp`, add three new slots to `TestRepoViewModel`:

  ```cpp
  void select_file_at_row_selects_correct_file()
  {
      const auto dir = repo_view_model_test::make_dirty_repo();

      RepoViewModel vm;
      QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
      vm.open(QString::fromStdString(dir.generic_string()));
      QVERIFY(filesSpy.wait(3000));
      QVERIFY(vm.changedFiles()->rowCount() > 0);

      QSignalSpy activeSpy(&vm, &RepoViewModel::activeFileChanged);
      vm.selectFileAtRow(0);
      // selectFileAtRow triggers an async diff load; activeFileChanged fires immediately.
      QVERIFY(activeSpy.wait(1000));
      QVERIFY(!vm.activeFile().isEmpty());

      std::filesystem::remove_all(dir);
  }

  void select_file_at_row_ignores_out_of_bounds()
  {
      RepoViewModel vm;
      // No repo open — changedFiles is empty. Should not crash.
      vm.selectFileAtRow(-1);
      vm.selectFileAtRow(0);
      vm.selectFileAtRow(99);
      // Just verify no crash.
      QVERIFY(true);
  }

  void select_commit_file_at_row_ignores_out_of_bounds()
  {
      RepoViewModel vm;
      vm.selectCommitFileAtRow(-1);
      vm.selectCommitFileAtRow(0);
      QVERIFY(true);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: compile error — methods do not exist yet.

- [ ] **Step 2: Declare in header**

  In `ui/include/gittide/ui/repoviewmodel.hpp`, add after `selectFile`:

  ```cpp
  Q_INVOKABLE void selectFileAtRow(int row);
  Q_INVOKABLE void selectCommitAtRow(int row);
  Q_INVOKABLE void selectCommitFileAtRow(int row);
  ```

- [ ] **Step 3: Implement in repoviewmodel.cpp**

  Add after the `selectFile` implementation (around line 163):

  ```cpp
  void RepoViewModel::selectFileAtRow(int row)
  {
      if (row < 0 || row >= m_files->rowCount())
          return;
      selectFile(m_files->pathAt(row));
  }

  void RepoViewModel::selectCommitAtRow(int row)
  {
      if (!m_history || row < 0 || row >= m_history->rowCount())
          return;
      const QString oid = m_history->data(m_history->index(row, 0),
                                          HistoryListModel::OidRole).toString();
      selectCommit(oid);
  }

  void RepoViewModel::selectCommitFileAtRow(int row)
  {
      if (row < 0 || row >= m_commitFiles->rowCount())
          return;
      selectCommitFile(m_commitFiles->pathAt(row));
  }
  ```

- [ ] **Step 4: Run tests — expect PASS**

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: all three new slots PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/include/gittide/ui/repoviewmodel.hpp \
          ui/src/repoviewmodel.cpp \
          tests/ui/test_repo_view_model.cpp
  git commit -m "feat(ui): add selectFileAtRow/selectCommitAtRow/selectCommitFileAtRow to RepoViewModel"
  ```

---

## Task 3: ChangesPane keyboard navigation

**Files:**
- Modify: `ui/qml/ChangesPane.qml`
- Test: `tests/ui/test_qml_shell.cpp` (existing file — add slots)

**Interfaces:**
- Produces: `ChangesPane::takeFocus()` — `function takeFocus()` that calls `fileList.forceActiveFocus()`. Used by `WorkingPane` global shortcuts.
- Produces: `ChangesPane::commitSummaryActive` — `readonly property bool`, alias of `commitSummary.activeFocus`. Used by `WorkingPane`'s `anyTextInputActive` guard.
- Produces: `ChangesPane::commitDescriptionActive` — `readonly property bool`, alias of `commitDescription.activeFocus`.

- [ ] **Step 1: Write the failing test**

  In `tests/ui/test_qml_shell.cpp`, add a new slot to `TestQmlShell`:

  ```cpp
  void changes_pane_exposes_take_focus()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      QObject* pane = engine.rootObjects().first()->findChild<QObject*>(
          QStringLiteral("changesTabBody"));
      QVERIFY(pane != nullptr);
      // takeFocus() must be callable without crashing.
      bool ok = QMetaObject::invokeMethod(pane, "takeFocus");
      QVERIFY(ok);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: FAIL — `takeFocus` not found.

- [ ] **Step 2: Modify `ChangesPane.qml` — add public API and focus ring**

  Replace the top-level `SplitView` opening lines (add the three new properties and function before the `handle:` block):

  ```qml
  SplitView {
      id: changesPane
      objectName: "changesPane"
      orientation: Qt.Horizontal

      // Public API used by WorkingPane global shortcuts (spec §2.2).
      function takeFocus() { fileList.forceActiveFocus() }
      readonly property bool commitSummaryActive:     commitSummary.activeFocus
      readonly property bool commitDescriptionActive: commitDescription.activeFocus

      handle: Rectangle {
  ```

- [ ] **Step 3: Wrap `fileList` in a focus-ring Item**

  The current `fileList` `ListView` uses `Layout.fillWidth/Height` inside a `ColumnLayout`. Replace the bare `ListView` with an `Item` wrapper that hosts a focus-ring overlay. Keep all existing `ListView` properties intact:

  ```qml
  // ---- Files list with focus-ring overlay ----
  Item {
      Layout.fillWidth: true
      Layout.fillHeight: true

      ListView {
          id: fileList
          objectName: "fileList"
          anchors.fill: parent
          clip: true
          model: repoVm ? repoVm.changedFiles : null

          // Keyboard navigation (spec §2.3).
          activeFocusOnTab: true
          KeyNavigation.tab: commitSummary
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
              if (currentIndex >= 0 && repoVm && currentItem)
                  repoVm.setFileChecked(currentIndex, currentItem.fileCheckState !== 2)
          }

          delegate: Rectangle {
              // Expose checkState so Keys.onSpacePressed can read it via currentItem.
              property int fileCheckState: model.checkState

              width: ListView.view.width
              height: 30
              color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

              MouseArea {
                  anchors.fill: parent
                  acceptedButtons: Qt.LeftButton
                  onClicked: {
                      fileList.currentIndex = index
                      if (repoVm) repoVm.selectFile(model.filePath)
                  }
              }

              TapHandler {
                  acceptedButtons: Qt.RightButton
                  onTapped: {
                      fileMenu.filePath   = model.filePath
                      fileMenu.fileName   = model.fileName
                      fileMenu.statusKind = model.statusKind
                      fileMenu.checkState = model.checkState
                      fileMenu.rowIndex   = index
                      fileMenu.popup()
                  }
              }

              RowLayout {
                  anchors.fill: parent
                  anchors.leftMargin: 12
                  anchors.rightMargin: 12
                  spacing: 8

                  AppCheckBox {
                      checkState: model.checkState === 2 ? Qt.Checked
                                  : model.checkState === 1 ? Qt.PartiallyChecked
                                  : Qt.Unchecked
                      onClicked: if (repoVm) repoVm.setFileChecked(index, model.checkState !== 2)
                  }
                  Label {
                      Layout.fillWidth: true
                      elide: Text.ElideMiddle
                      font.family: "monospace"
                      font.pixelSize: 12
                      textFormat: Text.StyledText
                      text: "<font color='" + theme.textMuted + "'>" + model.fileDir + "</font>"
                            + "<font color='" + theme.textPrimary + "'>" + model.fileName + "</font>"
                  }
                  Label {
                      text: model.statusLetter
                      font.family: "monospace"
                      font.pixelSize: 12
                      font.weight: Font.Bold
                      color: model.statusKind === "added" ? theme.stateAdded
                             : model.statusKind === "deleted" ? theme.stateDeleted
                             : model.statusKind === "untracked" ? theme.stateUntracked
                             : theme.stateModified
                  }
              }
          }
      }

      // Focus ring — overlay Rectangle whose 1px border lights up when fileList
      // has active focus. Drawn on top so it never insets the list content.
      Rectangle {
          anchors.fill: parent
          color: "transparent"
          border.color: fileList.activeFocus ? theme.focusBorder : "transparent"
          border.width: 1
          // Pointer-transparent so mouse events pass through to the ListView.
          enabled: false
      }
  }
  ```

- [ ] **Step 4: Add Ctrl+Enter to `commitSummary`**

  Inside the `commitSummary` `TextField`, add after `placeholderText`:

  ```qml
  Keys.onReturnPressed: {
      if ((event.modifiers & Qt.ControlModifier) && commitButton.enabled) {
          repoVm.commit(commitSummary.text, commitDescription.text)
          event.accepted = true
      }
  }
  ```

- [ ] **Step 5: Add Ctrl+Enter and Tab-back to `commitDescription`**

  Inside the `commitDescription` `TextArea`, add after `placeholderText`:

  ```qml
  KeyNavigation.tab: fileList
  Keys.onReturnPressed: {
      if ((event.modifiers & Qt.ControlModifier) && commitButton.enabled) {
          repoVm.commit(commitSummary.text, commitDescription.text)
          event.accepted = true
      }
      // else: default TextArea behaviour inserts newline — do not accept event.
  }
  ```

- [ ] **Step 6: Run tests — expect PASS**

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: `changes_pane_exposes_take_focus` PASS; no regressions.

- [ ] **Step 7: Commit**

  ```bash
  git add ui/qml/ChangesPane.qml tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): keyboard navigation in ChangesPane — arrow keys, Space, Ctrl+Enter"
  ```

---

## Task 4: HistoryPane + CommitDetail keyboard navigation

**Files:**
- Modify: `ui/qml/HistoryPane.qml`
- Modify: `ui/qml/CommitDetail.qml`
- Test: `tests/ui/test_qml_shell.cpp` (add slot)

**Interfaces:**
- Produces: `HistoryPane::takeFocus()` — `function takeFocus()` that calls `historyList.forceActiveFocus()`.
- Produces: `CommitDetail::takeFocus()` — `function takeFocus()` that calls `commitFilesList.forceActiveFocus()`.
- Produces: `CommitDetail::tabBackward` — `signal tabBackward()`, emitted when Tab is pressed from `commitFilesList`; `HistoryPane` connects this to `historyList.forceActiveFocus()`.

- [ ] **Step 1: Write the failing test**

  In `tests/ui/test_qml_shell.cpp`, add:

  ```cpp
  void history_pane_exposes_take_focus()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      QObject* pane = engine.rootObjects().first()->findChild<QObject*>(
          QStringLiteral("historyTabBody"));
      QVERIFY(pane != nullptr);
      bool ok = QMetaObject::invokeMethod(pane, "takeFocus");
      QVERIFY(ok);

      QObject* detail = engine.rootObjects().first()->findChild<QObject*>(
          QStringLiteral("commitDetail"));
      QVERIFY(detail != nullptr);
      bool ok2 = QMetaObject::invokeMethod(detail, "takeFocus");
      QVERIFY(ok2);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: FAIL — `takeFocus` not yet defined.

- [ ] **Step 2: Modify `CommitDetail.qml` — signal, takeFocus, wrapped fileList**

  At the top of `CommitDetail.qml`, before the header `RowLayout`, add:

  ```qml
  signal tabBackward()
  function takeFocus() { commitFilesList.forceActiveFocus() }
  ```

  Wrap `commitFilesList` in a focus-ring `Item` (same pattern as Task 3 Step 3). Replace the bare `ListView` with:

  ```qml
  Item {
      Layout.fillWidth: true
      Layout.preferredHeight: 160

      ListView {
          id: commitFilesList
          objectName: "commitFilesList"
          anchors.fill: parent
          clip: true
          model: repoVm ? repoVm.commitFiles : null

          activeFocusOnTab: true
          Keys.onUpPressed: {
              if (currentIndex > 0) {
                  currentIndex--
                  if (repoVm) repoVm.selectCommitFileAtRow(currentIndex)
              }
          }
          Keys.onDownPressed: {
              if (currentIndex < count - 1) {
                  currentIndex++
                  if (repoVm) repoVm.selectCommitFileAtRow(currentIndex)
              }
          }
          Keys.onTabPressed: {
              commitDetail.tabBackward()
              event.accepted = true
          }

          delegate: Rectangle {
              width: ListView.view.width
              height: 28
              color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

              MouseArea {
                  anchors.fill: parent
                  onClicked: {
                      commitFilesList.currentIndex = index
                      if (repoVm) repoVm.selectCommitFile(model.filePath)
                  }
              }

              RowLayout {
                  anchors.fill: parent
                  anchors.leftMargin: 12
                  anchors.rightMargin: 12
                  spacing: 8
                  Label {
                      Layout.fillWidth: true
                      elide: Text.ElideMiddle
                      font.family: "monospace"
                      font.pixelSize: 12
                      textFormat: Text.RichText
                      text: "<font color='" + theme.textMuted + "'>" + model.fileDir + "</font>"
                            + "<font color='" + theme.textPrimary + "'>" + model.fileName + "</font>"
                  }
                  Label {
                      text: model.statusLetter
                      font.family: "monospace"
                      font.pixelSize: 12
                      font.weight: Font.Bold
                      color: model.statusKind === "added" ? theme.stateAdded
                             : model.statusKind === "deleted" ? theme.stateDeleted
                             : model.statusKind === "untracked" ? theme.stateUntracked
                             : theme.stateModified
                  }
              }
          }
      }

      Rectangle {
          anchors.fill: parent
          color: "transparent"
          border.color: commitFilesList.activeFocus ? theme.focusBorder : "transparent"
          border.width: 1
          enabled: false
      }
  }
  ```

  The `Keys.onTabPressed` references `commitDetail` which is the `id` of `CommitDetail`'s root `ColumnLayout`. Add `id: commitDetail` to the root `ColumnLayout` if not present (it may be implicit — use the existing `commitDetail` objectName id or add `id: commitDetail` at the top).

  In `CommitDetail.qml` the root is currently `ColumnLayout { id: commitDetail ... }`. Confirm `id: commitDetail` exists — if the root has no id, add `id: commitDetail` to the `ColumnLayout`.

- [ ] **Step 3: Modify `HistoryPane.qml` — takeFocus, wrapped historyList, Tab forwarding**

  At the top of `HistoryPane.qml`, inside the root `RowLayout`, before the `CommitContextMenu`, add:

  ```qml
  function takeFocus() { historyList.forceActiveFocus() }
  ```

  Wrap `historyList` in a focus-ring `Item`. Replace the bare `ListView` with:

  ```qml
  Item {
      Layout.preferredWidth: 420
      Layout.fillHeight: true

      ListView {
          id: historyList
          objectName: "historyList"
          anchors.fill: parent
          clip: true
          model: repoVm ? repoVm.history : null

          activeFocusOnTab: true
          Keys.onUpPressed: {
              if (currentIndex > 0) {
                  currentIndex--
                  if (repoVm) repoVm.selectCommitAtRow(currentIndex)
              }
          }
          Keys.onDownPressed: {
              if (currentIndex < count - 1) {
                  currentIndex++
                  if (repoVm) repoVm.selectCommitAtRow(currentIndex)
              }
          }
          Keys.onTabPressed: {
              commitDetail.takeFocus()
              event.accepted = true
          }

          delegate: Rectangle {
              width: ListView.view.width
              height: 48
              color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

              Rectangle {
                  visible: parent.ListView.isCurrentItem
                  width: 2
                  height: parent.height
                  color: theme.accent
              }

              MouseArea {
                  anchors.fill: parent
                  acceptedButtons: Qt.LeftButton | Qt.RightButton
                  onClicked: function(mouse) {
                      if (mouse.button === Qt.RightButton) {
                          historyList.currentIndex = index
                          commitMenu.oid             = model.oid
                          commitMenu.shortOid        = model.shortOid
                          commitMenu.localBranchName = model.localBranchName ?? ""
                          commitMenu.isHead          = model.isHead
                          commitMenu.popup()
                      } else {
                          historyList.currentIndex = index
                          if (repoVm) repoVm.selectCommit(model.oid)
                      }
                  }
              }

              RowLayout {
                  anchors.fill: parent
                  anchors.leftMargin: 8
                  anchors.rightMargin: 12
                  spacing: 8

                  GraphColumn {
                      Layout.fillHeight: true
                      Layout.preferredWidth: implicitWidth
                      graphRow: model.graphRow
                      laneColors: theme.laneColors
                      headColor: theme.head
                      laneCount: repoVm && repoVm.history ? repoVm.history.laneCount : 1
                      head: model.isHead
                  }

                  Avatar {
                      name: model.author
                      Layout.alignment: Qt.AlignVCenter
                  }

                  ColumnLayout {
                      Layout.fillWidth: true
                      spacing: 2
                      Label {
                          Layout.fillWidth: true
                          elide: Text.ElideRight
                          text: model.summary
                          color: theme.textPrimary
                          font.pixelSize: 13
                      }
                      RowLayout {
                          spacing: 8
                          Label {
                              text: model.author
                              color: theme.textMuted
                              font.pixelSize: 11
                          }
                          Label {
                              text: model.shortOid
                              color: theme.textMuted
                              font.family: "monospace"
                              font.pixelSize: 11
                          }
                          Label {
                              Layout.fillWidth: true
                              horizontalAlignment: Text.AlignRight
                              text: model.date
                              color: theme.textMuted
                              font.pixelSize: 11
                          }
                      }
                  }
              }
          }
      }

      Rectangle {
          anchors.fill: parent
          color: "transparent"
          border.color: historyList.activeFocus ? theme.focusBorder : "transparent"
          border.width: 1
          enabled: false
      }
  }
  ```

  Connect `commitDetail.tabBackward` to `historyList.forceActiveFocus()`. Add a `Connections` block after the `CommitDetail` instance:

  ```qml
  Connections {
      target: commitDetail
      function onTabBackward() { historyList.forceActiveFocus() }
  }
  ```

- [ ] **Step 4: Run tests — expect PASS**

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: `history_pane_exposes_take_focus` PASS; no regressions.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/qml/HistoryPane.qml ui/qml/CommitDetail.qml \
          tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): keyboard navigation in HistoryPane and CommitDetail"
  ```

---

## Task 5: WorkingPane global shortcuts + focus-on-open

**Files:**
- Modify: `ui/qml/WorkingPane.qml`
- Test: `tests/ui/test_qml_shell.cpp` (add slot)

**Interfaces:**
- Consumes: `changesTabBody.takeFocus()`, `historyTabBody.takeFocus()`, `changesTabBody.commitSummaryActive`, `changesTabBody.commitDescriptionActive` (all produced by Tasks 3–4).
- Produces: global keyboard shortcuts Ctrl+1, Ctrl+2, Ctrl+R, bound to `WorkingPane`.

- [ ] **Step 1: Write failing test**

  In `tests/ui/test_qml_shell.cpp`, add:

  ```cpp
  void working_pane_has_keyboard_shortcuts()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);
      // WorkingPane and its shortcuts load without error.
      QObject* pane = engine.rootObjects().first()->findChild<QObject*>(
          QStringLiteral("workingPane"));
      QVERIFY(pane != nullptr);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: PASS (workingPane already has objectName). This baseline confirms no regression from the QML edits to come.

- [ ] **Step 2: Add shortcuts and focus-on-open to `WorkingPane.qml`**

  Inside the root `Item` (after the `StackLayout` closing brace but before the end of the `ColumnLayout`), add the following block. It references `changesTabBody` and `historyTabBody` which are the existing `objectName` identifiers on the `ChangesPane` and `HistoryPane` instances — verify these are set; they are already present as `objectName: "changesTabBody"` and `objectName: "historyTabBody"` in the `StackLayout`.

  *Note:* QML `id` and `objectName` are different. The `Shortcut` items reference by `id`. Check the current `ChangesPane` and `HistoryPane` instantiation in `WorkingPane.qml` — they have `objectName` but may not have `id`. Add `id: changesTabBody` and `id: historyTabBody` to their instantiation lines.

  After the `StackLayout`:

  ```qml
  // ---- Global keyboard shortcuts (spec §2.2) ----

  readonly property bool anyTextInputActive:
      changesTabBody.commitSummaryActive || changesTabBody.commitDescriptionActive

  // Ctrl+1 / Ctrl+2 — switch tabs and route focus.
  Shortcut {
      sequence: "Ctrl+1"
      enabled: repoVm !== null && repoVm.repoOpen
      onActivated: {
          tabs.currentIndex = 0
          changesTabBody.takeFocus()
      }
  }
  Shortcut {
      sequence: "Ctrl+2"
      enabled: repoVm !== null && repoVm.repoOpen
      onActivated: {
          tabs.currentIndex = 1
          historyTabBody.takeFocus()
      }
  }

  // Ctrl+R — refresh history (status refresh is triggered by the controller automatically).
  Shortcut {
      sequence: "Ctrl+R"
      enabled: repoVm !== null && repoVm.repoOpen
      onActivated: repoVm.refreshHistory()
  }

  // ? — toggle shortcuts overlay (guarded: don't fire while typing in commit fields).
  Shortcut {
      sequence: "?"
      context: Qt.WindowShortcut
      enabled: repoVm !== null && repoVm.repoOpen && !anyTextInputActive
      onActivated: shortcutsPopup.visible ? shortcutsPopup.close() : shortcutsPopup.open()
  }

  // Focus fileList when a repo first opens.
  Connections {
      target: repoVm
      enabled: repoVm !== null
      function onChanged() {
          if (repoVm && repoVm.repoOpen)
              Qt.callLater(function() { changesTabBody.takeFocus() })
      }
  }
  ```

  The `shortcutsPopup` id is forward-referenced here — it will be defined in Task 6. The QML engine is lenient about forward references within the same component. If a build error occurs, temporarily comment out the `?` Shortcut until Task 6 is complete.

- [ ] **Step 3: Add `id` to ChangesPane and HistoryPane instances**

  In the `StackLayout` block, the two pane instances currently have `objectName` but no `id`. Add ids:

  ```qml
  ChangesPane {
      id: changesTabBody
      objectName: "changesTabBody"
  }

  HistoryPane {
      id: historyTabBody
      objectName: "historyTabBody"
  }
  ```

- [ ] **Step 4: Build and run tests**

  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: clean build, all tests PASS.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/qml/WorkingPane.qml tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): WorkingPane global shortcuts Ctrl+1/2/R and focus-on-open"
  ```

---

## Task 6: ShortcutsHelpPopup

**Files:**
- Create: `ui/qml/ShortcutsHelpPopup.qml`
- Modify: `ui/qml/qml.qrc`
- Modify: `ui/qml/WorkingPane.qml` (add instance)
- Test: `tests/ui/test_qml_shell.cpp` (add slot)

**Interfaces:**
- Produces: `ShortcutsHelpPopup` — `Popup` with `objectName: "shortcutsPopup"`, `open()` / `close()` / `visible`. Used by the `?` Shortcut in WorkingPane.

- [ ] **Step 1: Write the failing test**

  In `tests/ui/test_qml_shell.cpp`, add:

  ```cpp
  void shortcuts_popup_exists_in_shell()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      QObject* popup = engine.rootObjects().first()->findChild<QObject*>(
          QStringLiteral("shortcutsPopup"));
      QVERIFY(popup != nullptr);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: FAIL — `shortcutsPopup` not found.

- [ ] **Step 2: Create `ui/qml/ShortcutsHelpPopup.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic
  import QtQuick.Layouts

  // Keyboard shortcuts reference overlay. Opened by the ? shortcut in WorkingPane.
  Popup {
      id: root
      objectName: "shortcutsPopup"

      width: 380
      height: 292
      anchors.centerIn: parent

      modal: false
      closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
      padding: 0

      background: OverlayCard {}

      contentItem: ColumnLayout {
          spacing: 0

          // Title row
          RowLayout {
              Layout.fillWidth: true
              Layout.margins: 16
              Layout.bottomMargin: 12

              Label {
                  text: "Keyboard shortcuts"
                  color: theme.textPrimary
                  font.pixelSize: 13
                  font.weight: Font.DemiBold
                  Layout.fillWidth: true
              }
              Label {
                  text: "Esc"
                  color: theme.textMuted
                  font.pixelSize: 11
                  font.family: "monospace"
              }
          }

          // Hairline under title
          Rectangle {
              Layout.fillWidth: true
              height: 1
              color: theme.border
          }

          // Shortcut rows
          component Row: RowLayout {
              property string keys: ""
              property string action: ""

              Layout.fillWidth: true
              Layout.leftMargin: 16
              Layout.rightMargin: 16
              Layout.topMargin: 6
              Layout.bottomMargin: 6
              spacing: 12

              Rectangle {
                  implicitWidth: keyLabel.implicitWidth + 10
                  implicitHeight: 20
                  radius: 3
                  color: "transparent"
                  border.color: theme.border
                  border.width: 1
                  Label {
                      id: keyLabel
                      anchors.centerIn: parent
                      text: parent.parent.keys
                      color: theme.textSecondary
                      font.family: "monospace"
                      font.pixelSize: 11
                  }
              }
              Label {
                  text: parent.action
                  color: theme.textSecondary
                  font.pixelSize: 12
                  Layout.fillWidth: true
              }
          }

          Row { keys: "↑ / ↓";        action: "Navigate files or commits" }
          Row { keys: "Space";         action: "Stage / unstage file" }
          Row { keys: "Tab";           action: "Next pane" }
          Row { keys: "Ctrl+Enter";    action: "Commit" }
          Row { keys: "Ctrl+1 / +2";  action: "Changes / History tab" }
          Row { keys: "Ctrl+R";        action: "Refresh" }
          Row { keys: "?";             action: "Show / hide this panel" }

          Item { Layout.fillHeight: true }
      }
  }
  ```

- [ ] **Step 3: Register in `ui/qml/qml.qrc`**

  Add `<file>ShortcutsHelpPopup.qml</file>` to `ui/qml/qml.qrc` inside the `<qresource prefix="/qml">` block (after the last existing entry, before `</qresource>`).

- [ ] **Step 4: Instantiate in `WorkingPane.qml`**

  Inside the root `Item` of `WorkingPane.qml`, after the `Connections` block added in Task 5, add:

  ```qml
  ShortcutsHelpPopup {
      id: shortcutsPopup
  }
  ```

- [ ] **Step 5: Run tests — expect PASS**

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: `shortcuts_popup_exists_in_shell` PASS; no regressions.

- [ ] **Step 6: Commit**

  ```bash
  git add ui/qml/ShortcutsHelpPopup.qml ui/qml/qml.qrc \
          ui/qml/WorkingPane.qml tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): add ShortcutsHelpPopup — ? overlay listing all keyboard shortcuts"
  ```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: keyboard navigation for the review-and-commit loop.
> - Spec updated: `docs/spec/product/keyboard-controls.md` (Status → `shipped`).
> - Code: `ui/qml/ChangesPane.qml`, `HistoryPane.qml`, `CommitDetail.qml`, `WorkingPane.qml`, `ShortcutsHelpPopup.qml`; `RepoViewModel` (`selectFileAtRow`, `selectCommitAtRow`, `selectCommitFileAtRow`); `Theme`/`QmlTheme` (`focusBorder`).
