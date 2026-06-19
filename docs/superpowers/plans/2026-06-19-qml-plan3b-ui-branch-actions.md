# QML Branch Actions (grouped dropdown + new/rename/delete) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax. (This plan was executed inline in its authoring session.)

**Goal:** Make the QML branch bar interactive — a grouped, filterable dropdown (Local / Worktrees / Remote) for switching branches, plus New / Rename / Delete flows — on top of the existing `RepoController` branch backends and the Plan 3a `BranchInfo` fields.

**Architecture:** A new `BranchListModel` (`QAbstractListModel`) turns `std::vector<BranchInfo>` into rows with a `section` role (Local/Worktrees/Remote), name, head flag, upstream, worktree path, and a remote flag, with a `filter` property. `RepoViewModel` owns it, feeds it from `branchesChanged`, exposes it as a `branches` property, and gains `Q_INVOKABLE` branch actions that drive the controller's `QCoro::Task` methods. QML gets a `BranchDropdown` popup and three dialogs; the branch chip in `BranchBar` becomes the dropdown trigger.

**Tech Stack:** Qt 6.8 Quick (Controls.Basic), C++23 ViewModel/models, Qt Test (headless) for the model + view-model, offscreen QML load smoke for views (visual QA deferred to user, per Plan 2).

## Global Constraints

- No Qt in `core/`; Qt types only at the ViewModel boundary. Colour from `theme` tokens only — no hex literals in QML.
- New `ui/` sources → `ui/CMakeLists.txt`; new QML files → `ui/qml/qml.qrc`; new tests → the UI list in `tests/CMakeLists.txt`.
- ViewModel kicks `QCoro::Task` methods via `QCoro::connect`; QML never calls tasks directly.
- Code style: `m_` members, Allman braces, lowercase file names.
- Section order is fixed: **Local, Worktrees, Remote**. Section strings are exactly `"Local"`, `"Worktrees"`, `"Remote"`.
- Stable `objectName`s for testability: `branchDropdown`, `branchFilterField`, `branchList`, `newBranchSentinel`, `renameBranchSentinel`, `deleteBranchSentinel`, `newBranchDialog`, `renameBranchDialog`, `deleteBranchDialog`.

---

### Task B1: `BranchListModel` — grouped, filterable branch list model

**Files:**
- Create: `ui/include/gittide/ui/branchlistmodel.hpp`, `ui/src/branchlistmodel.cpp`
- Modify: `ui/CMakeLists.txt` (add the two paths), `tests/CMakeLists.txt` (add test source)
- Test: `tests/ui/test_branch_list_model.cpp`

**Interfaces:**
- Consumes: `gittide::BranchInfo`, `gittide::BranchKind` (Plan 3a).
- Produces:
  ```cpp
  class BranchListModel : public QAbstractListModel {
      Q_OBJECT
      Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
      Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  public:
      enum Roles { NameRole = Qt::UserRole + 1, SectionRole, IsHeadRole, UpstreamRole, WorktreePathRole, RemoteRole };
      using QAbstractListModel::QAbstractListModel;
      int rowCount(const QModelIndex& = {}) const override;
      QVariant data(const QModelIndex&, int role) const override;
      QHash<int, QByteArray> roleNames() const override;
      void setBranches(const std::vector<gittide::BranchInfo>& branches);
      QString filter() const;
      void setFilter(const QString& f);
      Q_INVOKABLE QString nameAt(int row) const;          // visible-row name, "" if oob
      QStringList localBranchNames() const;               // for base-ref / rename pickers
  signals:
      void filterChanged();
      void countChanged();
  };
  ```
  Section rule: `kind == RemoteTracking` → `"Remote"` (RemoteRole true); else non-empty `worktreePath` && !isHead → `"Worktrees"`; else `"Local"`. Rows sorted by section order (Local<Worktrees<Remote) then case-insensitive name. `filter` does a case-insensitive substring match on name; empty filter shows all. Rebuild emits model reset + `countChanged`.

- [ ] **Step 1: Failing test** — `tests/ui/test_branch_list_model.cpp`:
```cpp
#include <QtTest>
#include "gittide/ui/branchlistmodel.hpp"
using gittide::ui::BranchListModel;
using gittide::BranchInfo;
using gittide::BranchKind;

class TestBranchListModel : public QObject { Q_OBJECT
private slots:
    void groups_and_orders_sections()
    {
        BranchListModel m;
        m.setBranches({
            BranchInfo{.name="origin/main", .kind=BranchKind::RemoteTracking},
            BranchInfo{.name="feature", .kind=BranchKind::Local},
            BranchInfo{.name="wt", .kind=BranchKind::Local, .worktreePath="/tmp/wt"},
            BranchInfo{.name="main", .isHead=true, .kind=BranchKind::Local},
        });
        QCOMPARE(m.rowCount(), 4);
        // Local section first (feature, main), then Worktrees (wt), then Remote.
        auto section = [&](int r){ return m.index(r).data(BranchListModel::SectionRole).toString(); };
        QCOMPARE(section(0), QStringLiteral("Local"));
        QCOMPARE(section(2), QStringLiteral("Worktrees"));
        QCOMPARE(section(3), QStringLiteral("Remote"));
        QCOMPARE(m.index(3).data(BranchListModel::RemoteRole).toBool(), true);
    }
    void filter_is_case_insensitive_substring()
    {
        BranchListModel m;
        m.setBranches({ BranchInfo{.name="main",.isHead=true}, BranchInfo{.name="feature/login"} });
        m.setFilter(QStringLiteral("LOG"));
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.index(0).data(BranchListModel::NameRole).toString(), QStringLiteral("feature/login"));
    }
    void local_branch_names_excludes_remote()
    {
        BranchListModel m;
        m.setBranches({ BranchInfo{.name="main",.isHead=true},
                        BranchInfo{.name="origin/main",.kind=BranchKind::RemoteTracking} });
        QCOMPARE(m.localBranchNames(), QStringList{QStringLiteral("main")});
    }
};
#include "test_branch_list_model.moc"
QTEST_MAIN(TestBranchListModel)
```
(If the UI tests use one shared `main.cpp` runner, register this `TestBranchListModel` the same way `test_repo_view_model.cpp` is registered rather than `QTEST_MAIN` — match the existing pattern in `tests/ui/`.)

- [ ] **Step 2: Run — expect FAIL** (header missing). `cmake --build build --parallel`.
- [ ] **Step 3: Implement** `branchlistmodel.{hpp,cpp}`; add to `ui/CMakeLists.txt` and the test to `tests/CMakeLists.txt`. Follow `changedfilesmodel.cpp` for the role/reset idiom.
- [ ] **Step 4: Run — expect PASS.** `ctest --test-dir build -R branch_list_model` (or the UI test binary).
- [ ] **Step 5: Commit** `feat(ui): BranchListModel — grouped/filterable branch list`.

---

### Task B2: `RepoViewModel` branch actions + branches model

**Files:**
- Modify: `ui/src/repoviewmodel.cpp`, `ui/include/gittide/ui/repoviewmodel.hpp`
- Test: `tests/ui/test_repo_view_model.cpp`

**Interfaces:**
- Consumes: `BranchListModel` (B1); `RepoController::createBranch(QString,QString,bool)`, `switchBranch(QString)`, `deleteBranch(QString,bool)`, `renameBranch(QString,QString)`, signals `branchesChanged`, `deleteFailedUnmerged(QString)`.
- Produces on `RepoViewModel`:
  ```cpp
  Q_PROPERTY(gittide::ui::BranchListModel* branches READ branches CONSTANT)
  BranchListModel* branches() const;
  Q_INVOKABLE void switchBranch(const QString& name);
  Q_INVOKABLE void createBranch(const QString& name, const QString& fromOid, bool checkout);
  Q_INVOKABLE void deleteBranch(const QString& name, bool force);
  Q_INVOKABLE void renameBranch(const QString& oldName, const QString& newName);
  Q_INVOKABLE QString branchFilter() const; // passthrough to model.filter (optional; QML may bind model directly)
  signals: void branchDeleteUnmerged(const QString& name);
  ```
  `onBranches` now also calls `m_branches->setBranches(...)`. `branchDeleteUnmerged` forwards the controller's `deleteFailedUnmerged`. Each action routes through `QCoro::connect(m_controller->...(), this, []{});` (controller refreshes branches/head itself on success).

- [ ] **Step 1: Failing test** — add to `tests/ui/test_repo_view_model.cpp` (uses `make_dirty_repo`):
```cpp
void create_and_switch_branch_updates_model_and_head()
{
    const auto dir = repo_view_model_test::make_dirty_repo();
    RepoViewModel vm;
    QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
    vm.open(QString::fromStdString(dir.generic_string()));
    QVERIFY(filesSpy.wait(3000));

    QSignalSpy branchesSpy(vm.branches(), &QAbstractItemModel::modelReset);
    vm.createBranch(QStringLiteral("feature"), QString(), true);
    QVERIFY(branchesSpy.wait(3000));
    // model now lists at least main + feature
    QVERIFY(vm.branches()->rowCount() >= 2);

    QSignalSpy headSpy(&vm, &RepoViewModel::branchChanged);
    vm.switchBranch(QStringLiteral("feature"));
    QVERIFY(headSpy.wait(3000));
    QCOMPARE(vm.currentBranch(), QStringLiteral("feature"));
    std::filesystem::remove_all(dir);
}
```
- [ ] **Step 2: Run — expect FAIL** (no `branches()` / `createBranch`).
- [ ] **Step 3: Implement** the property, model wiring in `onBranches`, the four invokables, and the `branchDeleteUnmerged` forward.
- [ ] **Step 4: Run — expect PASS.**
- [ ] **Step 5: Commit** `feat(ui): RepoViewModel branch actions + branches model`.

---

### Task B3: `BranchDropdown` popup + `BranchBar` trigger (switch only)

**Files:**
- Create: `ui/qml/BranchDropdown.qml`
- Modify: `ui/qml/BranchBar.qml`, `ui/qml/qml.qrc`

**Interfaces:**
- Consumes: `repoVm.branches` (model with `branchName`/`section`/`isHead`/`upstream`/`worktreePath`/`remote` roles), `repoVm.switchBranch(name)`, `repoVm.createBranch/renameBranch/deleteBranch` (sentinels open dialogs from B4–B6 — wire the sentinel `signal`s now, connect dialogs later).

- [ ] **Step 1:** Make the branch chip a clickable trigger (MouseArea/Button) that opens `branchDropdown` (a `Popup`, `objectName: "branchDropdown"`). Clamp width + max height + internal scroll + elevation (design §9).
- [ ] **Step 2:** Dropdown contents: `TextField` (`objectName: "branchFilterField"`) bound to `repoVm.branches.filter`; `ListView` (`objectName: "branchList"`, `model: repoVm.branches`) with `section.property: "section"` and a section header delegate; row delegate shows name, a head check/accent for `isHead`, dimmed + `☁` for `remote`, the path for `worktreePath`. Clicking a non-remote row → `repoVm.switchBranch(branchName)` then close.
- [ ] **Step 3:** Below a separator, three sentinel rows: `newBranchSentinel`, `renameBranchSentinel`, `deleteBranchSentinel`, each emitting a dropdown signal (`newRequested`, `renameRequested`, `deleteRequested`).
- [ ] **Step 4:** Add `BranchDropdown.qml` to `qml.qrc`. Build; load `gittide_qml_app` under `QT_QPA_PLATFORM=offscreen` and confirm no QML errors.
- [ ] **Step 5: Commit** `feat(ui): grouped branch dropdown with switch + action sentinels`.

---

### Task B4: `NewBranchDialog` (name + base-ref picker)

**Files:** Create `ui/qml/NewBranchDialog.qml`; modify `qml.qrc`, `BranchDropdown.qml`/`Main.qml` to host it.

**Interfaces:** Consumes `repoVm.createBranch(name, fromOid, true)`. Base-ref picker defaults to the current branch; populate choices from `repoVm.branches.localBranchNames()` plus the current branch. (fromOid: pass `""` for "current HEAD"; for another base ref, the simplest correct path is to switch semantics to branch-from-name — if the controller needs an OID, resolve via a new `repoVm` helper. For this milestone, default base = current branch with `fromOid=""`; a non-default base is wired when a name→OID resolver exists. Document this limitation in the dialog.)

- [ ] **Step 1:** Dialog (`objectName: "newBranchDialog"`, radius-18 card, accent focus ring per design §9/§10) with a name `TextField`, a base-ref `ComboBox` (default current), Create/Cancel.
- [ ] **Step 2:** Create → `repoVm.createBranch(name, "", true)`; close on success (`committedOk`-style: there is no per-action success signal — close optimistically and rely on `operationFailed` to surface errors via a toast/log). Disable Create when name empty.
- [ ] **Step 3:** Register in `qml.qrc`; open from the `newRequested` sentinel. Build + offscreen smoke.
- [ ] **Step 4: Commit** `feat(ui): new-branch dialog with base-ref picker`.

---

### Task B5: `RenameBranchDialog`

**Files:** Create `ui/qml/RenameBranchDialog.qml`; modify `qml.qrc`, host.

**Interfaces:** Consumes `repoVm.renameBranch(oldName, newName)`. Old name preselected (current branch, or the row the dropdown had focused).

- [ ] **Step 1:** Dialog (`objectName: "renameBranchDialog"`) showing old name (read-only) + new-name `TextField` + Rename/Cancel; Rename disabled when empty or unchanged.
- [ ] **Step 2:** Rename → `repoVm.renameBranch(old, new)`; close optimistically; errors via `operationFailed`.
- [ ] **Step 3:** Register + open from `renameRequested`. Build + offscreen smoke.
- [ ] **Step 4: Commit** `feat(ui): rename-branch dialog`.

---

### Task B6: `DeleteBranchDialog` (click-confirm, unmerged warning, don't-ask-again)

**Files:** Create `ui/qml/DeleteBranchDialog.qml`; modify `qml.qrc`, host.

**Interfaces:** Consumes `repoVm.deleteBranch(name, force)` and `repoVm.branchDeleteUnmerged(name)`. Per design §11: confirmation-by-click (not type-to-confirm) — a double-click-armed danger button; an unmerged-branch warning bar shown when `branchDeleteUnmerged` fires (retry with `force=true`); a **"Don't ask for confirmation again"** checkbox that suppresses the second confirmation thereafter (store the flag in a QML singleton/property for the session — persistence to settings is out of scope here).

- [ ] **Step 1:** Dialog (`objectName: "deleteBranchDialog"`) with the target name, a danger button that arms on first click and deletes on second (unless "don't ask again" is set, then one click deletes), and the "don't ask again" `CheckBox`.
- [ ] **Step 2:** First attempt `deleteBranch(name, false)`. On `branchDeleteUnmerged`, reveal a warning bar + a force button → `deleteBranch(name, true)`.
- [ ] **Step 3:** Register + open from `deleteRequested`. Build + offscreen smoke.
- [ ] **Step 4: Commit** `feat(ui): delete-branch dialog with unmerged warning + click-confirm`.

---

## Outcome

Realises QML migration design spec §6 (interactive branch bar), §7 (grouped/filtered dropdown), §10 (new-branch base-ref picker), §11 (delete confirm flow). Remote branches are display/dimmed only (no checkout-of-remote yet — that needs fetch/track, deferred to the Fetch/Pull/Push plan). Non-default base ref in New-branch is deferred until a name→OID resolver exists.

## Self-review notes

- §7 grouping/filter → B1 + B3. §6 trigger → B3. §10 base-ref → B4. §11 delete → B6. Rename (sentinel in §7) → B5.
- Type consistency: `branches` property type `BranchListModel*`; role names (`branchName` etc.) must match between B1 `roleNames()` and B3 delegate bindings — define them once in B1 and reuse.
- Deferred-but-documented: non-default base ref (B4), don't-ask persistence (B6), remote checkout.
