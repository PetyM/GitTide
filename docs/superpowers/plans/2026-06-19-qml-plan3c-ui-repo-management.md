# QML Repo / Project Management Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax. (Executed inline in its authoring session.)

**Goal:** Make the QML sidebar's "Add repository" button live — add-existing / initialize / clone / new-project flows, a clone-progress modal, repo removal, and brand-centered empty states — over the existing `ProjectController` backends.

**Architecture:** `ProjectController` slots are already QML-callable; the only C++ addition is a `Q_INVOKABLE startClone(url, dest)` that kicks the `QCoro::Task cloneRepo` (QML cannot await a Task). The sidebar's button opens a menu wiring `addExistingRepo` / `initRepo` / `startClone` / `createProject`; new QML dialogs collect inputs (folder pickers via `QtQuick.Dialogs`). A clone-progress modal binds to `cloneProgress`. When the repo list is empty, an empty-state card shows the documented CTAs.

**Tech Stack:** Qt 6.8 Quick (Controls.Basic, QtQuick.Dialogs), C++23 ProjectController, Qt Test (headless) for `startClone`, qmllint + offscreen load smoke for views.

## Global Constraints

- No Qt in `core/`; colour from `theme` tokens only.
- New QML → `ui/qml/qml.qrc`. New tests → UI list in `tests/CMakeLists.txt` + `tests/ui/main.cpp`.
- QML kicks `QCoro::Task` via a C++ `Q_INVOKABLE`, never directly.
- Stable object names (design §12): `createProjectCta`, `addExistingCta`, `cloneCta`, `initRepoCta`, `manifestProjectCta`; dialogs `addRepoMenu`, `initRepoDialog`, `cloneRepoDialog`, `newProjectDialog`, `cloneProgressDialog`.
- "Add repository" label is plain — no `+`, no chevron (design §8). The manifest CTA is present-but-deferred (disabled, tooltip "Coming soon").

---

### Task C1: `ProjectController::startClone` (QML clone kicker)

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp`, `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp`

**Interfaces:**
- Consumes: `QCoro::Task<void> cloneRepo(QString,QString)`.
- Produces: `Q_INVOKABLE void startClone(const QString& url, const QString& dest);` — fire-and-forget; runs `cloneRepo` via `QCoro::connect`, surfacing the usual `repoAdded` / `repoAddFailed` / `cloneProgress` signals.

- [ ] **Step 1: Failing test** (mirror `cloneRepo_file_url_succeeds_and_emits_repoAdded`, but drive `startClone` + wait on the spy):
```cpp
void startClone_file_url_emits_repoAdded()
{
    // ... build a source repo + file:// URL exactly as the existing clone test ...
    ProjectController controller(&store, {});
    QSignalSpy added(&controller, &ProjectController::repoAdded);
    controller.startClone(QString::fromStdString(srcUrl), QString::fromStdString(destDir.generic_string()));
    QVERIFY(added.wait(5000));
    std::filesystem::remove_all(destDir);
}
```
- [ ] **Step 2: Run — FAIL** (no `startClone`).
- [ ] **Step 3: Implement** `startClone` as `QCoro::connect(cloneRepo(url, dest), this, []{});` (include `<qcorotask.h>`/`qcoro/QCoro` as the controller already does).
- [ ] **Step 4: Run — PASS.**
- [ ] **Step 5: Commit** `feat(ui): ProjectController::startClone — QML-invokable clone kicker`.

---

### Task C2: "Add repository" menu

**Files:** Modify `ui/qml/Sidebar.qml`.

- [ ] Replace the dead `addRepoButton` click with opening a `Menu` (`objectName: "addRepoMenu"`) of `MenuItem`s: "Add existing repository…" → `addExistingFolder.open()`; "Initialize new repository…" → `initRepoDialog.openDialog()`; "Clone repository…" → `cloneRepoDialog.openDialog()`; "New project…" → `newProjectDialog.openDialog()`. Keep the plain label (no `+`/chevron). Build + offscreen smoke. Commit.

---

### Task C3: Add-existing + Initialize

**Files:** Create `ui/qml/InitRepoDialog.qml`; modify `Sidebar.qml`, `qml.qrc`.

- [ ] Add-existing: a `FolderDialog` (`objectName: "addExistingFolder"`) → on accept `projectController.addExistingRepo(selectedFolder)` (strip the `file://` scheme via `selectedFolder.toString().replace(/^file:\/\//, "")`).
- [ ] Initialize: `InitRepoDialog` (radius-18 card) with a parent-folder `FolderDialog` trigger + a name `TextField` → `projectController.initRepo(parentDir, name)`. Disable confirm when either is empty.
- [ ] Surface `repoAddFailed(message)` via an inline error label in the dialog/sidebar. Register, build, offscreen smoke. Commit `feat(ui): add-existing + initialize-repo flows`.

---

### Task C4: Clone + progress modal

**Files:** Create `ui/qml/CloneRepoDialog.qml`, `ui/qml/CloneProgressDialog.qml`; modify `Sidebar.qml`, `qml.qrc`.

- [ ] `CloneRepoDialog`: URL `TextField` + destination `FolderDialog` trigger → `projectController.startClone(url, dest)`; on start, open `cloneProgressDialog`.
- [ ] `CloneProgressDialog` (`objectName: "cloneProgressDialog"`, modal): a `ProgressBar` bound to `cloneProgress(received,total)` (indeterminate when `total<=0`), a Cancel button → `projectController.cancelClone()`. Close on `repoAdded` or `repoAddFailed`. Use `Connections { target: projectController }`.
- [ ] Register, build, offscreen smoke. Commit `feat(ui): clone-repo dialog + progress modal`.

---

### Task C5: New project + remove repo

**Files:** Create `ui/qml/NewProjectDialog.qml`; modify `Sidebar.qml`, `qml.qrc`.

- [ ] `NewProjectDialog`: name `TextField` → `projectController.createProject(name)`; on `projectCreated(id)` → `projectController.activate(id)` then close.
- [ ] Repo removal: a right-click `Menu` on a `TreeViewDelegate` row with "Remove repository" → `projectController.removeRepo(model.repoPath)` (a confirm step is optional; removal only de-registers, it does not delete files — note this in the menu text: "Remove from project").
- [ ] Register, build, offscreen smoke. Commit `feat(ui): new-project dialog + remove-repo menu`.

---

### Task C6: Empty states

**Files:** Create `ui/qml/EmptyState.qml`; modify `Sidebar.qml` (or `WorkingPane.qml`), `qml.qrc`.

- [ ] When `repoModel` has zero rows, show a brand-centered card (icon, 22px headline, secondary subtext) with ghost-button CTAs: `addExistingCta`, `cloneCta`, `initRepoCta`, `createProjectCta`, and a disabled `manifestProjectCta` ("Coming soon"). Each CTA invokes the matching flow from C2–C5.
- [ ] Build + offscreen smoke. Commit `feat(ui): empty-state card with add/clone/init/project CTAs`.

---

## Outcome

Realises design spec §8 (Add repository — plain label), §9 (clamped overlays / radius-18 dialogs), §12 (empty states + CTA object names). Manifest project creation (§4) is present-but-deferred (disabled CTA) — it needs the pugixml dependency in `core/` and is out of scope. Multi-window/session restore and recursive submodule depth remain separate items.

## Self-review notes

- §8 plain Add label → C2. §12 CTA object names → C6 (and reused by dialogs). Clone progress (§ overlays) → C4.
- Type consistency: `startClone(url,dest)` signature identical in C1 and its callers (C4). Folder URLs are stripped of `file://` before reaching the controller (which expects filesystem paths).
- Deferred-but-documented: manifest CTA disabled; remove-repo is de-register only.
