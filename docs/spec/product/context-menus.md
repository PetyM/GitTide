# Context Menus

| | |
|--|--|
| **Designed** | 2026-06-23 |
| **Status** | `spec` |
| **Wishlist** | [docs/wishlist/context-menus.md](../../wishlist/context-menus.md) |
| **Touches** | ui: QML menu components, ViewModel additions, new dialog; design: destructive action colour |

## Overview

Every actionable entity in GitTide's shell gets a right-click context menu offering
the verbs that apply to it. This is a `ui/` concern only — no `core/` changes.
The work establishes a **consistent pattern** (per-entity menu components, shared
infrastructure, a disabled/hidden rule) and wires the verbs that are already
available. Verbs not yet implemented (revert, tags, ignore, fetch-per-repo) are
excluded from this pass; the menu structure is their natural home when those wishes
land.

---

## 1. Infrastructure

### 1.1 `AppMenuSeparator.qml`

A thin horizontal rule for grouping items inside an `AppMenu`. Styled with
`theme.border`, height 1px, vertical margin 4px (8px total height). Used exactly
like `AppMenuItem` — just placed between groups.

### 1.2 Destructive action colour

No dedicated token is added. Destructive menu items (`Discard changes`,
`Delete branch`, `Remove from project`) use **`theme.state.deleted`** (`#F85149`)
for their text colour. `AppMenuItem` gains an optional `destructive: bool` property
(default `false`); when `true`, text renders in `state.deleted` instead of
`text.primary`. Hover highlight colour is unchanged (accent tint).

### 1.3 Disabled vs. hidden rule

| Situation | Treatment |
|-----------|-----------|
| Action **structurally cannot apply** to this entity type (e.g. "Unstage" on an untracked file) | **Hidden** — item absent from menu |
| Action **contextually inapplicable** right now (e.g. "Switch to branch" on current branch) | **Disabled** — item shown greyed (`text.muted`), not clickable |

This rule is applied consistently across all four entity menus. It keeps menus
predictable: the shape is stable for a given entity type, items only disappear when
the verb can never make sense.

---

## 2. ViewModel additions (`ui/` only)

Four new `Q_INVOKABLE` methods in `RepoViewModel`:

| Method | Implementation | Notes |
|--------|---------------|-------|
| `discardFile(QString path)` | Calls `RepoController::discard()` with a single-file `StageSelection` | Emits `operationFailed` on error; refreshes status on success. **Never called directly from QML** — the calling menu opens `DiscardChangesDialog` first. |
| `openInEditor(QString path)` | `QDesktopServices::openUrl(QUrl::fromLocalFile(path))` | Opens with the OS default application for that file type. |
| `revealInFileManager(QString path)` | `QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()))` | Opens the parent directory in the OS file manager. |
| `copyToClipboard(QString text)` | `QGuiApplication::clipboard()->setText(text)` | Used for copy-path and copy-SHA. |

All four live in `ui/` (`RepoViewModel.hpp` / `repoviewmodel.cpp`). No `core/`
changes. No new signals needed — `operationFailed` already covers async errors.

---

## 3. New dialog: `DiscardChangesDialog.qml`

Follows the `DeleteBranchDialog` pattern: an `OverlayCard` with:

- Warning text: `"Discard changes to <filename>? This cannot be undone."`
- Buttons: `Cancel` (secondary) · `Discard` (primary, destructive text colour)
- Signal `accepted` → caller invokes `repoVm.discardFile(path)`

The dialog is instantiated once per view that hosts a `FileContextMenu` and opened
from the menu's `onDiscard` signal.

---

## 4. Per-entity menu components

Four new QML files in `ui/qml/`, each wrapping `AppMenu` + `AppMenuItem` +
`AppMenuSeparator`. Views instantiate one, set properties from the row model, call
`.popup()`.

### 4.1 `FileContextMenu.qml`

**Used in:** `ChangesPane.qml` (working-changes file list)

**Properties:** `filePath: string`, `fileName: string`, `statusKind: string`
(`"added"/"modified"/"deleted"/"untracked"`), `checkState: int` (Qt check-state
value: `0`=Unchecked, `1`=Partial, `2`=Checked)

**Signals:** `stage()`, `unstage()`, `discard()`, `openInEditor()`,
`revealInFileManager()`, `copyPath()`

| Item | Rule |
|------|------|
| **Stage** | Disabled when `checkState === 2` (fully staged) |
| **Unstage** | **Hidden** when `statusKind === "untracked"`; disabled when `checkState === 0` |
| — separator — | |
| **Open in editor** | Always enabled |
| **Reveal in file manager** | Always enabled |
| **Copy path** | Always enabled |
| — separator — | |
| **Discard changes** *(destructive)* | Always shown. For a tracked file it restores the committed content; for a new file (`untracked`/`added`) it deletes the file from the worktree (and drops any staged entry). |

Wiring in `ChangesPane.qml`:

```qml
FileContextMenu {
    id: fileMenu
    onStage:             repoVm.setFileChecked(fileMenu.rowIndex, true)
    onUnstage:           repoVm.setFileChecked(fileMenu.rowIndex, false)
    onDiscard:           discardDialog.open()
    onOpenInEditor:      repoVm.openInEditor(fileMenu.filePath)
    onRevealInFileManager: repoVm.revealInFileManager(fileMenu.filePath)
    onCopyPath:          repoVm.copyToClipboard(fileMenu.filePath)
}
DiscardChangesDialog {
    id: discardDialog
    fileName: fileMenu.fileName
    onAccepted: repoVm.discardFile(fileMenu.filePath)
}
```

The menu stores `rowIndex` so `setFileChecked` can address the model row.

### 4.2 `BranchContextMenu.qml`

**Used in:** `BranchDropdown.qml`

**Properties:** `branchName: string`, `isHead: bool`, `isRemote: bool`

**Signals:** `switchBranch()`, `newBranchFromHere()`, `rename()`, `deleteBranch()`,
`merge()`

| Item | Rule |
|------|------|
| **Switch to branch** | Disabled when `isHead` |
| **New branch from here** | Always enabled |
| — separator — | |
| **Rename** | **Hidden** when `isRemote` |
| **Delete** *(destructive)* | **Hidden** when `isRemote`; disabled when `isHead` |
| — separator — | |
| **Merge into current** | **Hidden** when `isHead` |
| **Rebase `<current>` onto `<name>`** | **Hidden** when `isHead` |

Wiring in `BranchDropdown.qml`: `onSwitchBranch → repoVm.switchBranch(name)`,
`onNewBranchFromHere → newBranchDialog.sourceBranch = branchMenu.branchName; newBranchDialog.open()`
(`NewBranchDialog` already supports a `sourceBranch` string that maps to `createBranch(name, from, checkout)`
via the ViewModel — if it currently only accepts an OID, extend it to also accept a branch name),
`onRename → renameBranchDialog.open()`,
`onDeleteBranch → deleteBranchDialog.open()`, `onMerge → repoVm.startMerge(name)`,
`onRebase → repoVm.startRebase(name)`.

The existing inline "Merge into current" button on branch rows is removed in favour
of this menu item to avoid duplication.

### 4.3 `CommitContextMenu.qml`

**Used in:** `HistoryPane.qml`

**Properties:** `oid: string`, `shortOid: string`, `localBranchName: string`,
`isHead: bool`

**Signals:** `copySha()`, `newBranchFromHere()`, `checkoutCommit()`, `merge()`,
`reword()`

| Item | Rule |
|------|------|
| **Copy SHA** | Always enabled |
| — separator — | |
| **New branch from here** | Always enabled |
| **Checkout commit** | Disabled when `isHead` |
| **Reword…** | **Hidden** unless `isHead` (reword of older commits is deferred to the rebase engine — see [history-editing](history-editing.md)) |
| **Undo last commit** | **Hidden** unless `isHead`; soft-resets to the parent, keeping changes staged (D37). |
| **Squash `<N>` commits…** | **Hidden** unless ≥2 rows are selected; seeds `RebaseTodoDialog` pre-filled (oldest `pick`, rest `squash`) via `repoVm.requestSquashTodo(rows)`. |
| **Edit history from here…** | **Hidden** when `isHead` (the clicked commit is the tip — nothing to rebase onto it); opens `RebaseTodoDialog` via `repoVm.startInteractiveRebase(oid)` |
| — separator — | |
| **Merge `<localBranchName>` into current** | **Hidden** when `localBranchName` is empty |

Replaces the existing inline `commitContextMenu` AppMenu in `HistoryPane.qml`.

Wiring: `onCopySha → repoVm.copyToClipboard(oid)`,
`onNewBranchFromHere → newBranchDialog.fromOid = oid; newBranchDialog.open()`,
`onCheckoutCommit → repoVm.checkoutCommit(oid)`,
`onMerge → repoVm.startMerge(localBranchName)`,
`onReword → rewordDialog.openFor(oid)` (the dialog lazy-fetches the full message
via `repoVm.requestCommitMessage`),
`onUndoLastCommit → repoVm.undoLastCommit()`,
`onSquashSelected → repoVm.requestSquashTodo(historyList.selectedRows)`.

**Multi-select & combined diff.** The History graph supports multi-selection
(Shift-click for a contiguous range, Ctrl-click to toggle); a contiguous
selection of ≥2 commits shows a **combined diff** in the detail pane. This is a
selection/detail behaviour, not a menu item — see
[history-editing](history-editing.md).

### 4.4 `RepoContextMenu.qml`

**Used in:** `Sidebar.qml` (repo tree rows)

**Properties:** `repoPath: string`

**Signals:** `revealInFileManager()`, `updateAllSubmodules()`, `removeFromProject()`

| Item | Rule |
|------|------|
| **Update all submodules** | Always enabled |
| — separator — | |
| **Reveal in file manager** | Always enabled |
| — separator — | |
| **Remove from project** *(destructive)* | Always enabled |

Replaces the existing inline `repoContextMenu` AppMenu in `Sidebar.qml`.

Wiring: `onRevealInFileManager → repoVm.revealInFileManager(repoPath)`,
`onUpdateAllSubmodules → projectController.updateAllSubmodules(repoPath)`,
`onRemoveFromProject → projectController.removeRepo(repoPath)`.

### 4.5 `SubmoduleContextMenu.qml`

**Used in:** `Sidebar.qml` (submodule tree rows)

**Properties:** `ownerRepoPath: string`, `submodulePath: string`, `status: int`
(`0`=Clean, `1`=Dirty, `2`=Uninitialized)

**Signals:** `initRequested()`, `updateAllRequested()`, `deinitRequested()`

| Item | Rule |
|------|------|
| **Initialize submodule** / **Update submodule** | Label adapts: *Initialize* when `status === 2`; *Update* otherwise. Always enabled. |
| **Update all submodules** | **Disabled** when `status === 2` (no children to update on an uninitialised node) |
| — separator — | |
| **Deinitialize submodule** *(destructive)* | **Disabled** when `status === 2` (already uninitialised) |

The right-click `TapHandler` in `Sidebar.qml` dispatches: submodule rows → this
menu; top-level repo rows → `RepoContextMenu`.

Wiring:
`onInitRequested → projectController.initSubmodule(ownerRepoPath, submodulePath)`,
`onUpdateAllRequested → projectController.updateAllSubmodules(submodulePath)`,
`onDeinitRequested → projectController.deinitSubmodule(ownerRepoPath, submodulePath)`.

> `updateAllRequested` passes the submodule's own path as the repo handle — that
> is the "drill one level deeper" behaviour: once a submodule is initialised it
> becomes a real repo whose direct submodules `updateAllSubmodules` can act on.

---

## 5. Right-click wiring pattern

All views use the same `TapHandler` pattern:

```qml
TapHandler {
    acceptedButtons: Qt.RightButton
    onTapped: {
        entityMenu.prop1 = model.prop1
        entityMenu.prop2 = model.prop2
        entityMenu.popup()
    }
}
```

Each menu is instantiated **once** per view (outside the delegate), properties set
at right-click time. This avoids per-row menu instances and is consistent with the
existing `repoContextMenu` and `commitContextMenu` patterns.

---

## 6. Scope exclusions (deferred)

| Item | Reason |
|------|--------|
| `ignoreFile` (add to .gitignore) | No core API yet |
| Copy remote URL | No per-repo ViewModel access from Sidebar |
| Fetch from repo context menu | Sidebar ViewModel is single-repo; cross-repo fetch is separate work |
| Open in terminal | Cross-platform terminal detection is non-trivial; deferred |
| Revert commit, tags | Not yet implemented in core |
| CommitDetail file list right-click | Low priority; commit files are read-only and don't need git ops |

---

## 7. Files changed

**New `ui/qml/`:**
- `AppMenuSeparator.qml`
- `FileContextMenu.qml`
- `BranchContextMenu.qml`
- `CommitContextMenu.qml`
- `RepoContextMenu.qml`
- `SubmoduleContextMenu.qml`
- `DiscardChangesDialog.qml`

**Modified `ui/qml/`:**
- `AppMenuItem.qml` — add `destructive: bool` property
- `ChangesPane.qml` — add right-click + FileContextMenu + DiscardChangesDialog
- `BranchDropdown.qml` — add right-click + BranchContextMenu, remove inline merge button
- `HistoryPane.qml` — replace inline commitContextMenu with CommitContextMenu
- `Sidebar.qml` — replace inline repoContextMenu with RepoContextMenu; add SubmoduleContextMenu + inline Init button

**Modified `ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp`:**
- Add `discardFile`, `openInEditor`, `revealInFileManager`, `copyToClipboard`

**Modified `ui/CMakeLists.txt`:**
- Register all new QML files

**Modified `docs/spec/design/design.md`:**
- Document `AppMenuItem.destructive` and use of `state.deleted` for destructive actions
