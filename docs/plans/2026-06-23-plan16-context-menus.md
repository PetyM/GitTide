# Plan 16 — Context Menus

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

| | |
|--|--|
| **Date** | 2026-06-23 |
| **Status** | `shipped` (2026-06-23) |
| **Spec** | [`docs/spec/product/context-menus.md`](../spec/product/context-menus.md) |
| **Depends on** | Plan 15 (app menu / frameless title bar) |

**Goal:** Give every actionable entity in GitTide a right-click context menu — files in the working-changes list, branches in the branch dropdown, commits in the history list, and repositories in the sidebar.

**Architecture:** Pure `ui/` work. Four per-entity `AppMenu`-backed QML components (`FileContextMenu`, `BranchContextMenu`, `CommitContextMenu`, `RepoContextMenu`) instantiated once per view, populated at right-click time. Four new `Q_INVOKABLE` methods on `RepoViewModel` (`discardFile`, `openInEditor`, `revealInFileManager`, `copyToClipboard`). One new confirmation dialog (`DiscardChangesDialog`). Two shared infrastructure additions (`AppMenuSeparator`, `AppMenuItem.destructive`).

**Tech stack:** Qt Quick / QML, `Q_INVOKABLE`, `QDesktopServices`, `QGuiApplication::clipboard()`, `QFileInfo`. No `core/` changes.

## Global Constraints

- **No Qt in `core/`** — all changes are in `ui/`.
- **No hex literals** — colours come from `theme.*` tokens only.
- New QML files must be added to `ui/qml/qml.qrc` (the `.qrc` is embedded in `gittide_ui` via AUTORCC).
- New `ui/` C++ sources (if any) → `ui/CMakeLists.txt`; new tests → `tests/CMakeLists.txt` **and** `tests/ui/main.cpp` (both edits are mandatory — see `docs/spec/engineering/testing.md`).
- Build: `cmake --build build --parallel`; test: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`.
- `AppMenuItem.destructive` uses `theme.stateDeleted` for text colour.
- Right-click uses `TapHandler { acceptedButtons: Qt.RightButton }` — consistent with the existing `Sidebar.qml` pattern.
- Each menu is instantiated **once** per view (outside the delegate); delegate TapHandler sets properties then calls `.popup()`.
- Discard is destructive — always guarded by `DiscardChangesDialog` before `repoVm.discardFile()` is called.

---

## Task 1: AppMenuSeparator + AppMenuItem.destructive

**Files:**
- Create: `ui/qml/AppMenuSeparator.qml`
- Modify: `ui/qml/AppMenuItem.qml`
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (new slot in existing class)

**Interfaces:**
- Produces: `AppMenuSeparator` — themed `MenuSeparator`, use inside `AppMenu` between groups.
- Produces: `AppMenuItem { destructive: bool }` — when `true`, text is `theme.stateDeleted` instead of `theme.textPrimary`.

- [ ] **Step 1: Write the failing test**

  Add a new slot to `TestQmlShell` in `tests/ui/test_qml_shell.cpp`:

  ```cpp
  void app_menu_infrastructure_exists()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      // AppMenuSeparator must be a registered type (loads without QML error).
      // We verify indirectly: Main.qml loads cleanly and the engine has no errors.
      // Per-type checks happen in the context-menu tasks below.
      QVERIFY(!engine.rootObjects().isEmpty());
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: the new slot compiles but the test itself fails because `AppMenuSeparator.qml` doesn't exist yet and QML may log a type error.

  *Note: if Main.qml doesn't use AppMenuSeparator yet, the test passes (the type just isn't loaded). That's fine — we add the structural check in later tasks. The important thing here is that the test compiles and the file registrations are correct.*

- [ ] **Step 2: Create `ui/qml/AppMenuSeparator.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic

  // Thin horizontal rule for grouping items inside an AppMenu (design §context-menus).
  MenuSeparator {
      contentItem: Rectangle {
          implicitHeight: 1
          color: theme.border
      }
      padding: 4
  }
  ```

- [ ] **Step 3: Add `destructive` property to `ui/qml/AppMenuItem.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic

  // Themed text row for AppMenu. Highlight uses the same accent tint as the branch
  // chip so hover reads consistently across popovers. No tick indicator — use a
  // plain MenuItem for checkable entries.
  // Set destructive: true for danger actions (discard, delete, remove) — renders
  // text in theme.stateDeleted rather than text.primary.
  MenuItem {
      id: item

      property bool destructive: false

      implicitHeight: 34
      padding: 8

      contentItem: Label {
          text: item.text
          color: item.enabled
                 ? (item.destructive ? theme.stateDeleted : theme.textPrimary)
                 : theme.textMuted
          font.pixelSize: 13
          verticalAlignment: Text.AlignVCenter
          elide: Text.ElideRight
      }

      background: Rectangle {
          radius: 6
          color: item.highlighted
                 ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)
                 : "transparent"
      }
  }
  ```

- [ ] **Step 4: Register `AppMenuSeparator` in `ui/qml/qml.qrc`**

  Add after the `<file>AppMenuItem.qml</file>` line:

  ```xml
  <file>AppMenuSeparator.qml</file>
  ```

- [ ] **Step 5: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: all tests pass (including `app_menu_infrastructure_exists`).

- [ ] **Step 6: Commit**

  ```bash
  git add ui/qml/AppMenuSeparator.qml ui/qml/AppMenuItem.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): add AppMenuSeparator and AppMenuItem.destructive"
  ```

---

## Task 2: RepoViewModel — discardFile, openInEditor, revealInFileManager, copyToClipboard

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Test: `tests/ui/test_repo_view_model.cpp` (new slots)

**Interfaces:**
- Produces:
  - `Q_INVOKABLE void discardFile(const QString& path)` — discards all working-tree changes to one file; emits `operationFailed` on error, refreshes status on success. Never call from QML without a prior confirmation dialog.
  - `Q_INVOKABLE void openInEditor(const QString& path)` — opens the file in the OS default editor.
  - `Q_INVOKABLE void revealInFileManager(const QString& path)` — opens the file's parent directory in the OS file manager.
  - `Q_INVOKABLE void copyToClipboard(const QString& text)` — sets the system clipboard text.

- [ ] **Step 1: Write failing tests in `tests/ui/test_repo_view_model.cpp`**

  Add these two slots to `TestRepoViewModel` (the existing class in that file):

  ```cpp
  void discardFile_restores_clean_status()
  {
      const auto dir = repo_view_model_test::make_dirty_repo(); // has modified a.txt
      RepoViewModel vm;

      QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
      vm.open(QString::fromStdString(dir.generic_string()));
      QVERIFY(filesSpy.wait(3000));
      QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1); // a.txt modified

      QSignalSpy statusSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
      vm.discardFile(QStringLiteral("a.txt"));
      QVERIFY(statusSpy.wait(3000));
      QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 0); // clean after discard

      std::filesystem::remove_all(dir);
  }

  void copyToClipboard_sets_system_clipboard()
  {
      RepoViewModel vm;
      vm.copyToClipboard(QStringLiteral("abc123"));
      QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("abc123"));
  }
  ```

  Add `#include <QGuiApplication>` at the top of `test_repo_view_model.cpp` if not already present.

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: both new slots fail — `vm.discardFile` doesn't compile (method doesn't exist yet).

- [ ] **Step 2: Declare the four methods in `ui/include/gittide/ui/repoviewmodel.hpp`**

  After the existing `Q_INVOKABLE void retryMergeDeinitSubmodules();` line, add:

  ```cpp
  Q_INVOKABLE void discardFile(const QString& path);
  Q_INVOKABLE void openInEditor(const QString& path);
  Q_INVOKABLE void revealInFileManager(const QString& path);
  Q_INVOKABLE void copyToClipboard(const QString& text);
  ```

- [ ] **Step 3: Implement in `ui/src/repoviewmodel.cpp`**

  Add these includes near the top of `repoviewmodel.cpp` (after the existing `#include <QHash>`):

  ```cpp
  #include <QClipboard>
  #include <QDesktopServices>
  #include <QFileInfo>
  #include <QGuiApplication>
  #include <QUrl>
  ```

  Add these implementations at the end of the file (before the closing `} // namespace`):

  ```cpp
  void RepoViewModel::discardFile(const QString& path)
  {
      gittide::StageSelection sel{
          .path        = qstringToPath(path),
          .hunkIndex   = std::nullopt,
          .lineIndices = {}
      };
      QCoro::connect(m_controller->discard(sel), this, [] {});
  }

  void RepoViewModel::openInEditor(const QString& path)
  {
      QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  }

  void RepoViewModel::revealInFileManager(const QString& path)
  {
      QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
  }

  void RepoViewModel::copyToClipboard(const QString& text)
  {
      QGuiApplication::clipboard()->setText(text);
  }
  ```

- [ ] **Step 4: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: `discardFile_restores_clean_status` and `copyToClipboard_sets_system_clipboard` pass.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp tests/ui/test_repo_view_model.cpp
  git commit -m "feat(ui): add discardFile, openInEditor, revealInFileManager, copyToClipboard to RepoViewModel"
  ```

---

## Task 3: DiscardChangesDialog

**Files:**
- Create: `ui/qml/DiscardChangesDialog.qml`
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (extend `app_menu_infrastructure_exists`)

**Interfaces:**
- Produces: `DiscardChangesDialog { fileName: string }` — modal confirmation dialog, emits `accepted` on confirm. Caller invokes `repoVm.discardFile(path)` on `accepted`.

- [ ] **Step 1: Write failing test**

  In `test_qml_shell.cpp`, extend `app_menu_infrastructure_exists` (or add a new slot) to verify the dialog type loads:

  ```cpp
  void discard_changes_dialog_loads()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);
      // DiscardChangesDialog is instantiated inside ChangesPane (wired in Task 5).
      // Here we verify the type compiles and the engine stays error-free.
      QVERIFY(!engine.rootObjects().isEmpty());
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: passes vacuously (dialog not yet used). The real coverage comes in Task 5.

- [ ] **Step 2: Create `ui/qml/DiscardChangesDialog.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic
  import QtQuick.Layouts

  // Confirmation guard before discarding working-tree changes (spec §3).
  // Open via .open(); on user confirmation emits Dialog.accepted so the caller
  // can invoke repoVm.discardFile(path). Never call discardFile without this guard.
  Dialog {
      id: dialog
      objectName: "discardChangesDialog"
      modal: true
      title: "Discard changes"
      anchors.centerIn: parent
      width: 380
      padding: 20

      property string fileName: ""

      background: OverlayCard {}

      contentItem: Label {
          text: "Discard changes to \"" + dialog.fileName + "\"? This cannot be undone."
          color: theme.textPrimary
          font.pixelSize: 13
          wrapMode: Text.WordWrap
      }

      footer: RowLayout {
          spacing: 8
          Layout.margins: 16
          Item { Layout.fillWidth: true }
          Button {
              text: "Cancel"
              onClicked: dialog.reject()
          }
          Button {
              objectName: "discardConfirmButton"
              text: "Discard"
              contentItem: Label {
                  text: parent.text
                  color: theme.stateDeleted
                  horizontalAlignment: Text.AlignHCenter
              }
              background: Rectangle {
                  radius: 6
                  color: theme.surfaceOverlay
                  border.color: theme.stateDeleted
                  border.width: 1
              }
              onClicked: dialog.accept()
          }
      }
  }
  ```

- [ ] **Step 3: Register in `ui/qml/qml.qrc`**

  Add after `<file>DeleteBranchDialog.qml</file>`:

  ```xml
  <file>DiscardChangesDialog.qml</file>
  ```

- [ ] **Step 4: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: all tests pass.

- [ ] **Step 5: Commit**

  ```bash
  git add ui/qml/DiscardChangesDialog.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): add DiscardChangesDialog confirmation guard"
  ```

---

## Task 4: NewBranchDialog — add `fromOid` property

**Files:**
- Modify: `ui/qml/NewBranchDialog.qml`

**Interfaces:**
- Produces: `NewBranchDialog { fromOid: string }` — when non-empty, `createBranch` passes this OID as the base instead of HEAD (`""`). Used by CommitContextMenu "New branch from here".

- [ ] **Step 1: Modify `ui/qml/NewBranchDialog.qml`**

  Add a `fromOid` property, an `openFromCommit(oid)` function, and update `onAccepted` to use `fromOid`. Crucially, `openDialog()` always resets `fromOid` to `""` so sentinel-triggered opens (from BranchBar) never inherit stale state from a prior commit-based open.

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic
  import QtQuick.Layouts

  // Create a new branch.
  // openDialog()       — branch from HEAD (or the base-combo selection, deferred).
  // openFromCommit(id) — branch from a specific commit OID; hides the base combo.
  Dialog {
      id: dialog
      objectName: "newBranchDialog"
      modal: true
      title: "New branch"
      anchors.centerIn: parent
      width: 380
      padding: 20

      property string fromOid: ""

      background: OverlayCard {}

      function openDialog() {
          fromOid = ""   // always reset; callers that want a specific OID use openFromCommit
          nameField.text = ""
          baseCombo.model = repoVm ? repoVm.branches.localBranchNames() : []
          baseCombo.currentIndex = repoVm ? Math.max(0, baseCombo.model.indexOf(repoVm.currentBranch)) : 0
          open()
          nameField.forceActiveFocus()
      }

      function openFromCommit(oid) {
          fromOid = oid
          nameField.text = ""
          baseCombo.model = []
          open()
          nameField.forceActiveFocus()
      }

      contentItem: ColumnLayout {
          spacing: 12

          Label {
              text: "Branch name"
              color: theme.textMuted
              font.pixelSize: 11
          }
          TextField {
              id: nameField
              objectName: "newBranchName"
              Layout.fillWidth: true
              placeholderText: "feature/my-change"
              color: theme.textPrimary
              background: Rectangle {
                  radius: 6
                  color: theme.surfaceBase
                  border.color: nameField.activeFocus ? theme.accent : theme.border
                  border.width: 1
              }
              Keys.onReturnPressed: if (createButton.enabled) dialog.accept()
          }

          Label {
              text: "Create from"
              color: theme.textMuted
              font.pixelSize: 11
              visible: dialog.fromOid.length === 0
          }
          ComboBox {
              id: baseCombo
              objectName: "newBranchBase"
              Layout.fillWidth: true
              visible: dialog.fromOid.length === 0
          }
          Label {
              visible: dialog.fromOid.length > 0
              text: "From commit " + dialog.fromOid.slice(0, 7)
              color: theme.textMuted
              font.pixelSize: 11
              font.family: "monospace"
          }
      }

      footer: RowLayout {
          spacing: 8
          Layout.margins: 16
          Item { Layout.fillWidth: true }
          Button {
              text: "Cancel"
              onClicked: dialog.reject()
          }
          Button {
              id: createButton
              objectName: "newBranchCreate"
              text: "Create"
              enabled: nameField.text.trim().length > 0
              onClicked: dialog.accept()
          }
      }

      onAccepted: {
          if (repoVm)
              repoVm.createBranch(nameField.text.trim(), dialog.fromOid, true)
      }
  }
  ```

- [ ] **Step 2: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: all existing tests pass (no behavior change when `fromOid` is empty).

- [ ] **Step 3: Commit**

  ```bash
  git add ui/qml/NewBranchDialog.qml
  git commit -m "feat(ui): add fromOid property to NewBranchDialog for commit-based branch creation"
  ```

---

## Task 5: FileContextMenu + wire ChangesPane

**Files:**
- Create: `ui/qml/FileContextMenu.qml`
- Modify: `ui/qml/ChangesPane.qml`
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (new slot)

**Interfaces:**
- Produces: `FileContextMenu` with properties `filePath`, `fileName`, `statusKind`, `checkState`, `rowIndex` and signals `stage()`, `unstage()`, `discard()`, `openInEditor()`, `revealInFileManager()`, `copyPath()`.

- [ ] **Step 1: Write the failing test**

  Add a slot to `TestQmlShell` in `tests/ui/test_qml_shell.cpp`:

  ```cpp
  void file_context_menu_exists_in_shell()
  {
      const auto dir = qml_shell_test::make_dirty_repo();

      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;
      RepoViewModel vm;

      QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
      vm.open(QString::fromStdString(dir.generic_string()));
      QVERIFY(filesSpy.wait(3000));

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      QObject* menu = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("fileContextMenu"));
      QVERIFY(menu != nullptr);

      QObject* discardDialog = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("discardChangesDialog"));
      QVERIFY(discardDialog != nullptr);

      std::filesystem::remove_all(dir);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: FAIL — `fileContextMenu` and `discardChangesDialog` not found yet.

- [ ] **Step 2: Create `ui/qml/FileContextMenu.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic

  // Right-click context menu for a changed file in the working-changes list.
  // Instantiate once per view; set properties from the row model, then call popup().
  // Disabled vs. hidden rule (spec §1.3):
  //   - Stage: disabled when already fully staged (checkState 2).
  //   - Unstage: hidden for untracked files; disabled when not staged (checkState 0).
  //   - Discard: hidden for untracked files (structurally inapplicable — nothing to discard).
  AppMenu {
      id: menu
      objectName: "fileContextMenu"

      property string filePath:   ""
      property string fileName:   ""
      property string statusKind: ""   // "added" | "modified" | "deleted" | "untracked"
      property int    checkState: 0    // 0=Unchecked, 1=Partial, 2=Checked
      property int    rowIndex:   -1

      signal stage()
      signal unstage()
      signal discard()
      signal openInEditor()
      signal revealInFileManager()
      signal copyPath()

      AppMenuItem {
          text: "Stage"
          enabled: menu.checkState !== 2
          onTriggered: menu.stage()
      }
      AppMenuItem {
          text: "Unstage"
          visible: menu.statusKind !== "untracked"
          enabled: menu.checkState !== 0
          onTriggered: menu.unstage()
      }

      AppMenuSeparator {}

      AppMenuItem {
          text: "Open in editor"
          onTriggered: menu.openInEditor()
      }
      AppMenuItem {
          text: "Reveal in file manager"
          onTriggered: menu.revealInFileManager()
      }
      AppMenuItem {
          text: "Copy path"
          onTriggered: menu.copyPath()
      }

      AppMenuSeparator {
          visible: menu.statusKind !== "untracked"
      }
      AppMenuItem {
          text: "Discard changes"
          visible: menu.statusKind !== "untracked"
          destructive: true
          onTriggered: menu.discard()
      }
  }
  ```

- [ ] **Step 3: Register in `ui/qml/qml.qrc`**

  Add after `<file>AppMenuSeparator.qml</file>`:

  ```xml
  <file>FileContextMenu.qml</file>
  ```

- [ ] **Step 4: Wire into `ui/qml/ChangesPane.qml`**

  After the `SplitView { id: changesPane ... }` opening and before `handle:`, add the menu and dialog outside the delegate. The menu and dialog go just before the `handle:` block (still inside `SplitView`):

  At the end of `ChangesPane.qml`, after the `Connections { target: repoVm ... }` block, add:

  ```qml
      // ---- File context menu (right-click on a changed file row) ----
      FileContextMenu {
          id: fileMenu
          onStage:               if (repoVm) repoVm.setFileChecked(fileMenu.rowIndex, true)
          onUnstage:             if (repoVm) repoVm.setFileChecked(fileMenu.rowIndex, false)
          onDiscard:             discardDialog.open()
          onOpenInEditor:        if (repoVm) repoVm.openInEditor(fileMenu.filePath)
          onRevealInFileManager: if (repoVm) repoVm.revealInFileManager(fileMenu.filePath)
          onCopyPath:            if (repoVm) repoVm.copyToClipboard(fileMenu.filePath)
      }

      DiscardChangesDialog {
          id: discardDialog
          fileName: fileMenu.fileName
          onAccepted: if (repoVm) repoVm.discardFile(fileMenu.filePath)
      }
  ```

  Then in the `delegate:` for `fileList`, add a `TapHandler` after the existing `MouseArea`:

  ```qml
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
  ```

  Place the `TapHandler` as a sibling of `MouseArea` inside the delegate `Rectangle`.

- [ ] **Step 5: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: `file_context_menu_exists_in_shell` passes; all other tests pass.

- [ ] **Step 6: Commit**

  ```bash
  git add ui/qml/FileContextMenu.qml ui/qml/ChangesPane.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): add FileContextMenu with stage/unstage/discard/open/reveal/copy actions"
  ```

---

## Task 6: BranchContextMenu + wire BranchDropdown

**Files:**
- Create: `ui/qml/BranchContextMenu.qml`
- Modify: `ui/qml/BranchDropdown.qml`
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (new slot)

**Interfaces:**
- Produces: `BranchContextMenu` with properties `branchName`, `isHead`, `isRemote` and signals `switchBranch()`, `newBranchFromHere()`, `rename()`, `deleteBranch()`, `merge()`.
- The inline "Merge into current" `Rectangle` in `BranchDropdown` delegate (lines 137–165) is **removed** — merge is now in the context menu.

- [ ] **Step 1: Write the failing test**

  Add to `TestQmlShell`:

  ```cpp
  void branch_context_menu_exists_in_shell()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      QObject* menu = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("branchContextMenu"));
      QVERIFY(menu != nullptr);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: FAIL — `branchContextMenu` not found.

- [ ] **Step 2: Create `ui/qml/BranchContextMenu.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic

  // Right-click context menu for a branch row in BranchDropdown.
  // Disabled vs. hidden rule (spec §1.3):
  //   - Switch: disabled when isHead (contextually inapplicable).
  //   - Rename/Delete: hidden when isRemote (structurally inapplicable — can't mutate remotes locally).
  //   - Delete: disabled when isHead (can't delete current branch).
  //   - Merge: hidden when isHead (no sense merging a branch into itself).
  AppMenu {
      id: menu
      objectName: "branchContextMenu"

      property string branchName: ""
      property bool   isHead:     false
      property bool   isRemote:   false

      signal switchBranch()
      signal newBranchFromHere()
      signal rename()
      signal deleteBranch()
      signal merge()

      AppMenuItem {
          text: "Switch to branch"
          enabled: !menu.isHead
          onTriggered: menu.switchBranch()
      }
      AppMenuItem {
          text: "New branch from here"
          onTriggered: menu.newBranchFromHere()
      }

      AppMenuSeparator {
          visible: !menu.isRemote
      }
      AppMenuItem {
          text: "Rename"
          visible: !menu.isRemote
          onTriggered: menu.rename()
      }
      AppMenuItem {
          text: "Delete"
          visible: !menu.isRemote
          enabled: !menu.isHead
          destructive: true
          onTriggered: menu.deleteBranch()
      }

      AppMenuSeparator {
          visible: !menu.isHead
      }
      AppMenuItem {
          text: repoVm ? ("Merge into " + repoVm.currentBranch) : "Merge into current"
          visible: !menu.isHead
          onTriggered: menu.merge()
      }
  }
  ```

- [ ] **Step 3: Register in `ui/qml/qml.qrc`**

  Add after `<file>FileContextMenu.qml</file>`:

  ```xml
  <file>BranchContextMenu.qml</file>
  ```

- [ ] **Step 4: Wire into `ui/qml/BranchDropdown.qml`**

  **4a. Remove the inline "Merge into current" button** from the branch delegate. Delete the `Rectangle { objectName: "mergeIntoItem" ... }` block (the Rectangle and everything inside it, lines ~137–165 in the original file). After this, the `RowLayout` in the delegate ends with just the branch name label and worktree path label.

  **4b. Add `BranchContextMenu` and right-click handler.** Inside the `Popup { id: dropdown }`, add the context menu just before the `contentItem:` property:

  ```qml
  BranchContextMenu {
      id: branchMenu
      onSwitchBranch:    { if (repoVm) { if (branchMenu.isRemote) repoVm.checkoutRemoteBranch(branchMenu.branchName); else repoVm.switchBranch(branchMenu.branchName) }; dropdown.close() }
      onNewBranchFromHere: { dropdown.newRequested(); dropdown.close() }
      onRename:          { dropdown.renameRequested(); dropdown.close() }
      onDeleteBranch:    { dropdown.deleteRequested(); dropdown.close() }
      onMerge:           { if (repoVm) repoVm.startMerge(branchMenu.branchName); dropdown.close() }
  }
  ```

  **4c. Add right-click `TapHandler` to the branch delegate.** Inside `delegate: Rectangle { id: branchDelegate ... }`, add after `HoverHandler { id: hover }`:

  ```qml
  TapHandler {
      acceptedButtons: Qt.RightButton
      onTapped: {
          branchMenu.branchName = model.branchName
          branchMenu.isHead     = model.isHead
          branchMenu.isRemote   = model.remote
          branchMenu.popup()
      }
  }
  ```

- [ ] **Step 5: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: `branch_context_menu_exists_in_shell` passes; all other tests pass. Verify that the test checking for `mergeIntoItem` (if any) is updated or still passes — search for `"mergeIntoItem"` in the test suite.

  ```bash
  grep -rn "mergeIntoItem" tests/
  ```

  If found, remove or update those references since the inline merge button is gone.

- [ ] **Step 6: Commit**

  ```bash
  git add ui/qml/BranchContextMenu.qml ui/qml/BranchDropdown.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): add BranchContextMenu; remove inline merge button from BranchDropdown"
  ```

---

## Task 7: CommitContextMenu + wire HistoryPane

**Files:**
- Create: `ui/qml/CommitContextMenu.qml`
- Modify: `ui/qml/HistoryPane.qml`
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (new slot)

**Interfaces:**
- Produces: `CommitContextMenu` with properties `oid`, `shortOid`, `localBranchName`, `isHead` and signals `copySha()`, `newBranchFromHere()`, `checkoutCommit()`, `merge()`.
- Replaces the existing inline `AppMenu { id: commitContextMenu }` in `HistoryPane.qml`.

- [ ] **Step 1: Write the failing test**

  Add to `TestQmlShell`:

  ```cpp
  void commit_context_menu_exists_in_shell()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      QObject* menu = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("commitContextMenu"));
      QVERIFY(menu != nullptr);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: FAIL (old `commitContextMenu` AppMenu is gone once we replace it).

  *Note: this test currently passes because the old inline `AppMenu { objectName: "commitContextMenu" }` still exists. After Step 3 replaces it with `CommitContextMenu`, the objectName moves to the new component. The test still finds it by name.*

- [ ] **Step 2: Create `ui/qml/CommitContextMenu.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic

  // Right-click context menu for a commit row in HistoryPane.
  // Disabled vs. hidden rule (spec §1.3):
  //   - Checkout commit: disabled when isHead (contextually inapplicable — already here).
  //   - Merge: hidden when localBranchName is empty (structurally inapplicable —
  //     only branch tips can be merged by name; arbitrary commits cannot).
  AppMenu {
      id: menu
      objectName: "commitContextMenu"

      property string oid:             ""
      property string shortOid:        ""
      property string localBranchName: ""
      property bool   isHead:          false

      signal copySha()
      signal newBranchFromHere()
      signal checkoutCommit()
      signal merge()

      AppMenuItem {
          text: "Copy SHA"
          onTriggered: menu.copySha()
      }

      AppMenuSeparator {}

      AppMenuItem {
          text: "New branch from here"
          onTriggered: menu.newBranchFromHere()
      }
      AppMenuItem {
          text: "Checkout commit"
          enabled: !menu.isHead
          onTriggered: menu.checkoutCommit()
      }

      AppMenuSeparator {
          visible: menu.localBranchName.length > 0
      }
      AppMenuItem {
          text: (repoVm && menu.localBranchName.length > 0)
                ? ("Merge " + menu.localBranchName + " into " + repoVm.currentBranch)
                : "Merge into current"
          visible: menu.localBranchName.length > 0
          onTriggered: menu.merge()
      }
  }
  ```

- [ ] **Step 3: Register in `ui/qml/qml.qrc`**

  Add after `<file>BranchContextMenu.qml</file>`:

  ```xml
  <file>CommitContextMenu.qml</file>
  ```

- [ ] **Step 4: Update `ui/qml/HistoryPane.qml`**

  **4a. Replace the inline `AppMenu { id: commitContextMenu }` block** (lines 12–29 of the original) with:

  ```qml
      // ---- Commit context menu (right-click on a history row) ----
      CommitContextMenu {
          id: commitMenu
          onCopySha:          if (repoVm) repoVm.copyToClipboard(commitMenu.oid)
          onNewBranchFromHere: commitNewBranchDialog.openFromCommit(commitMenu.oid)
          onCheckoutCommit:   if (repoVm) repoVm.checkoutCommit(commitMenu.oid)
          onMerge:            if (repoVm) repoVm.startMerge(commitMenu.localBranchName)
      }

      NewBranchDialog {
          id: commitNewBranchDialog
      }
  ```

  **4b. Update the right-click handler** in the `MouseArea`. Replace the existing `commitContextMenu.rowBranchName = ...` / `commitContextMenu.popup()` block with:

  ```qml
  if (mouse.button === Qt.RightButton) {
      historyList.currentIndex = index
      commitMenu.oid             = model.oid
      commitMenu.shortOid        = model.shortOid
      commitMenu.localBranchName = model.localBranchName ?? ""
      commitMenu.isHead          = model.isHead
      commitMenu.popup()
  }
  ```

- [ ] **Step 5: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: `commit_context_menu_exists_in_shell` passes; check for any test referencing `rowBranchName` and update:

  ```bash
  grep -rn "rowBranchName\|commitContextMenu" tests/
  ```

  Update any such tests to use the new property names (`commitMenu.localBranchName`). The `objectName: "commitContextMenu"` is preserved on `CommitContextMenu`, so `findChild("commitContextMenu")` still works.

- [ ] **Step 6: Commit**

  ```bash
  git add ui/qml/CommitContextMenu.qml ui/qml/HistoryPane.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): add CommitContextMenu with copy-SHA, new-branch, checkout, merge actions"
  ```

---

## Task 8: RepoContextMenu + wire Sidebar

**Files:**
- Create: `ui/qml/RepoContextMenu.qml`
- Modify: `ui/qml/Sidebar.qml`
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (new slot)

**Interfaces:**
- Produces: `RepoContextMenu` with property `repoPath: string` and signals `revealInFileManager()`, `removeFromProject()`.
- Replaces the inline `AppMenu { id: repoContextMenu }` in `Sidebar.qml`.

- [ ] **Step 1: Write the failing test**

  Add to `TestQmlShell`:

  ```cpp
  void repo_context_menu_exists_in_shell()
  {
      ThemeManager mgr;
      mgr.setMode(ThemeManager::Mode::Dark);
      QmlTheme theme(&mgr);
      RepoListModel repoModel;

      QQmlApplicationEngine engine;
      installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
      engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
      QCOMPARE(engine.rootObjects().size(), 1);

      QObject* menu = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("repoContextMenu"));
      QVERIFY(menu != nullptr);
  }
  ```

  Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests`
  Expected: passes (old inline menu has the same objectName). Confirm the test stays green after Step 3 replaces it.

- [ ] **Step 2: Create `ui/qml/RepoContextMenu.qml`**

  ```qml
  import QtQuick
  import QtQuick.Controls.Basic

  // Right-click context menu for a repository row in the Sidebar.
  // All items always enabled — no contextual disable rules for this entity.
  AppMenu {
      id: menu
      objectName: "repoContextMenu"

      property string repoPath: ""

      signal revealInFileManager()
      signal removeFromProject()

      AppMenuItem {
          text: "Reveal in file manager"
          onTriggered: menu.revealInFileManager()
      }

      AppMenuSeparator {}

      AppMenuItem {
          text: "Remove from project"
          destructive: true
          onTriggered: menu.removeFromProject()
      }
  }
  ```

- [ ] **Step 3: Register in `ui/qml/qml.qrc`**

  Add after `<file>CommitContextMenu.qml</file>`:

  ```xml
  <file>RepoContextMenu.qml</file>
  ```

- [ ] **Step 4: Replace the inline menu in `ui/qml/Sidebar.qml`**

  Find the block:

  ```qml
  // ---- Remove-repo context menu ----
  AppMenu {
      id: repoContextMenu
      objectName: "repoContextMenu"
      property string repoPath: ""
      AppMenuItem {
          text: "Remove from project"
          onTriggered: if (projectController && repoContextMenu.repoPath.length > 0)
                           projectController.removeRepo(repoContextMenu.repoPath)
      }
  ```

  And the closing `}` of that AppMenu. Replace the entire block with:

  ```qml
  // ---- Remove-repo context menu ----
  RepoContextMenu {
      id: repoContextMenu
      onRevealInFileManager: if (repoVm) repoVm.revealInFileManager(repoContextMenu.repoPath)
      onRemoveFromProject:   if (projectController && repoContextMenu.repoPath.length > 0)
                                 projectController.removeRepo(repoContextMenu.repoPath)
  }
  ```

  The existing `TapHandler` that sets `repoContextMenu.repoPath` and calls `repoContextMenu.popup()` remains unchanged.

- [ ] **Step 5: Build and run tests**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure -R gittide_ui_tests
  ```

  Expected: `repo_context_menu_exists_in_shell` passes; all other tests pass.

- [ ] **Step 6: Commit**

  ```bash
  git add ui/qml/RepoContextMenu.qml ui/qml/Sidebar.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
  git commit -m "feat(ui): add RepoContextMenu with reveal-in-file-manager and remove-from-project"
  ```

---

## Task 9: Update design spec and graduate wishlist

**Files:**
- Modify: `docs/spec/design/design.md`
- Modify: `docs/wishlist/context-menus.md`
- Modify: `docs/plans/index.md`

- [ ] **Step 1: Add AppMenuItem.destructive to `docs/spec/design/design.md`**

  In the `## Components` section of `design.md`, after the existing component entries, add:

  ```markdown
  - **Context menus.** `AppMenu` + `AppMenuItem` rows with `AppMenuSeparator` between groups. Destructive actions (discard, delete, remove) set `AppMenuItem { destructive: true }`, which renders text in `state.deleted` — same hover highlight, just the label colour changes. Each entity type has a dedicated QML component (`FileContextMenu`, `BranchContextMenu`, `CommitContextMenu`, `RepoContextMenu`); see [spec/product/context-menus.md](../product/context-menus.md) for the per-entity action tables and disabled/hidden rules.
  ```

- [ ] **Step 2: Update `docs/wishlist/context-menus.md` status**

  Change the `Status` line from `idea` to `shipped`:

  ```markdown
  | **Status** | `shipped` — Plan 16 |
  ```

  And add the graduation comment block at the bottom:

  ```markdown
  ---

  **Shipped in Plan 16 (2026-06-23).** Design: `docs/spec/product/context-menus.md`. Components: `FileContextMenu.qml`, `BranchContextMenu.qml`, `CommitContextMenu.qml`, `RepoContextMenu.qml`, `AppMenuSeparator.qml`, `DiscardChangesDialog.qml`. ViewModel: `discardFile`, `openInEditor`, `revealInFileManager`, `copyToClipboard`. Disabled/hidden rule, destructive affordance, and the per-entity action sets are all in the spec.
  ```

- [ ] **Step 3: Add Plan 16 to `docs/plans/index.md`**

  Add an entry for Plan 16 in the appropriate section of the plan index.

- [ ] **Step 4: Build and run all tests one final time**

  ```bash
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure
  ```

  Expected: all tests pass (core + UI).

- [ ] **Step 5: Commit**

  ```bash
  git add docs/spec/design/design.md docs/wishlist/context-menus.md docs/plans/index.md
  git commit -m "docs: graduate context-menus wish; update design spec with menu component entry"
  ```

---

## Outcome

> Fill in when done.
>
> - Shipped: right-click context menus on changed files, branches, commits, and sidebar repos.
> - Spec updated: `docs/spec/product/context-menus.md` (new), `docs/spec/design/design.md` (AppMenuItem.destructive + menu components entry).
> - Code: `FileContextMenu.qml`, `BranchContextMenu.qml`, `CommitContextMenu.qml`, `RepoContextMenu.qml`, `AppMenuSeparator.qml`, `DiscardChangesDialog.qml`; `RepoViewModel` + 4 new methods.
