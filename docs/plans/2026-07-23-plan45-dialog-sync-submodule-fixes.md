# Plan 45 — Dialog layout, sync affordances, unpushed cue & submodule discard

| | |
|--|--|
| **Date** | 2026-07-23 |
| **Status** | `done` |
| **Spec** | `spec/design/design.md §Components`, `spec/product/product.md §Syncing`, `§Changes tab`, `§History tab` |
| **Depends on** | Plan 39 (local-only cue), Plan 40 (tabbed Options dialog) |

**Goal:** Fix a batch of reported bugs: dialogs sizing too short / buttons flush
to the edge (worst on New-branch, whose base-branch combo escaped the card),
dialogs centring over a sub-pane instead of the window, an unpushed cue too faint,
Push/Pull always shown, no background fetch, and submodule discard silently doing
nothing.

**Architecture:** All fixes land in `ui/` (QML + ViewModel) except the submodule
discard, which is a `core/` git-engine fix. Dialog layout is centralised in
`AppDialog` + two new primitives (`DialogColumn`, `DialogButtons`) so all ~18
dialogs are fixed at once.

**Tech stack:** Qt Quick Controls (Basic), libgit2 submodule API, QCoro.

## Global constraints

- No Qt in `core/`; errors as `Expected<T>`; paths via `generic_u8string()`.
- New `ui/` QML files registered in `ui/qml/qml.qrc`.
- TDD for the core change; the full suite (`ctest`) must stay green.

---

## Task 1: Dialog sizing, padding & centring

**Files:** `ui/qml/AppDialog.qml`, `ui/qml/DialogColumn.qml` (new),
`ui/qml/DialogButtons.qml` (new), `ui/qml/qml.qrc`, all `*Dialog.qml` +
`Main.qml`/`ChangesPane.qml` inline dialogs.

- [x] `AppDialog` derives `implicitHeight` from header + content + footer and sets
      `height: implicitHeight` (QtQuick's `Dialog` drops content height once a
      `footer` is present → card too short, content overflowed, footer overlapped).
- [x] `AppDialog` parents to `Overlay.overlay` so it centres in the window, not the
      item it is declared in (the discard dialog was centring over the diff pane).
- [x] `DialogColumn` wraps the body `ColumnLayout` in a plain Item (a Layout used
      directly as a Popup `contentItem` reports implicit height 0).
- [x] `DialogButtons` wraps the footer button row, inset from the card edges (a bare
      `RowLayout` footer sits flush; `Layout.margins` there is a no-op).
- [x] Migrate every `contentItem: ColumnLayout` → `DialogColumn` and every
      `footer: RowLayout {…}` → `DialogButtons`; promote the two raw `Dialog`
      instances in `Main.qml`/`ChangesPane.qml` to `AppDialog`.

## Task 2: Show Push/Pull only when meaningful + background auto-fetch

**Files:** `ui/qml/BranchBar.qml`, `ui/include/gittide/ui/repoviewmodel.hpp`,
`ui/src/repoviewmodel.cpp`.

- [x] Branch bar: **Pull** visible only when `behindCount > 0`, **Push** only when
      `aheadCount > 0` (Fetch always; Publish only when no upstream).
- [x] `Timer` (3 min) calls `repoVm.autoFetch()` for a repo whose branch tracks an
      upstream and isn't already syncing.
- [x] `RepoViewModel::autoFetch()` runs a **silent** fetch: an `m_silentSync` flag
      suppresses `authRequired` and `operationFailed` so a timer never raises the
      credential dialog or an error toast.

## Task 3: Make the unpushed cue prominent, in History and Graph

**Files:** `ui/qml/HistoryPane.qml`, `ui/qml/GraphPane.qml`.

- [x] Replace the faint `text.secondary` `↑` with a bold **accent** `↑`.
- [x] Tint the whole unpushed row with a faint `accent` wash (under selection), in
      both History and Graph tabs.

## Task 4: Discard a modified submodule resets it to the pin

**Files:** `core/src/gitrepo.cpp`, `tests/test_git_repo_discard.cpp`.

- [x] **Failing test** — move a submodule off its pin, `discard("sub")`, assert the
      submodule HEAD returns to the pinned commit and the superproject is clean.
- [x] `GitRepo::discard` detects a submodule via `git_submodule_lookup`, resets the
      superproject index to HEAD, then `git_submodule_update` (force) so the
      submodule checks out its pinned commit. Non-submodule paths unchanged.

---

## Outcome

All six issues shipped. Dialog layout is now owned by `AppDialog` + `DialogColumn`
+ `DialogButtons` (design §Components, D57): dialogs size to their content, centre
in the window, and their footer buttons are inset — verified visually across
New-branch, Options, Discard, and the fleet-fetch error dialog. The branch bar
shows Fetch always and Pull/Push/Publish only when they apply, backed by a silent
3-minute auto-fetch (`RepoViewModel::autoFetch`, D55, product §Syncing). Unpushed
commits read at a glance in both History and Graph via an accent `↑` plus a faint
accent row tint (design + product). Submodule discard now resets to the pin via
`git_submodule_update` (D56, product §Changes tab), covered by a new discard test.
Full suite: 209/209 green.
