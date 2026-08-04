# Plan 48 — Refresh coherence: one truth for "what changed" and "what is selected"

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-08-04 |
| **Status** | `done` |
| **Spec** | `spec/engineering/engineering.md §Live refresh (D35)`, `spec/product/product.md §Repository tree`, `decisions.md` D35 (amended) |
| **Depends on** | Plan 24 (submodule refresh reuses D35), Plan 33 (repo tree dirty badge), Plan 46 (single-flight/generation pattern) |

**Goal:** Every surface reflects disk truth within one debounce window of the
change, whoever made it — GitTide, a terminal, or an editor. And every list
highlights the row the ViewModel actually has selected, so a panel can never show
a diff without marking the file it belongs to.

**Architecture:** Two independent defects, fixed in two independent directions.

1. **Refresh propagation** (`RepoWatcher` → `RepoController` → `ProjectController`).
   Today three mechanisms refresh three surfaces at three latencies, with no
   connection between them: a debounced `QFileSystemWatcher` for the active repo's
   panes, explicit post-op cascades for in-app mutations, and a 5 s fleet poll for
   the sidebar rows. The active repo's own mutations never reach the sidebar, and
   the watcher silently drops events every time it re-arms. Fix: make the watcher
   lossless, make the mute a refcount, let the active repo push its state straight
   into its tree row, and give the fleet poll a single-flight guard.

2. **Selection ownership** (`RepoViewModel` → QML). Today the ViewModel owns
   *which file/commit is loaded* and each `ListView` independently owns *which row
   is highlighted*, via its own `currentIndex`. They diverge whenever the VM
   changes selection without a click — auto-select-first-file, selection preserved
   across a status refresh, repo switch — and a model reset silently snaps
   `currentIndex` to 0. Fix: the VM exposes the selected **row** as a property;
   QML binds `currentIndex` to it one-way and only ever calls a `select*` verb.

No new layers, no core changes. Everything lands in `ui/`.

**Tech stack:** `QFileSystemWatcher` set diffing, QCoro tasks, `QPointer`
generation guards, Qt property bindings from QML to `Q_PROPERTY`.

## Global constraints

- **No Qt in `core/`** — this plan touches no `core/` source.
- **One owner per `GitRepo`** — the fleet poll keeps opening its own per-repo
  handles; the single-flight guard must not turn into a shared handle.
- New `ui/` sources → `ui/CMakeLists.txt`; new tests → `gittide_ui_test_sources`
  in `tests/CMakeLists.txt`.
- Existing object names used by the QML tests must survive: `fileList`,
  `commitFilesList`, `historyList`, `graphList`, `changesTabBar`, `repoTree`.
- The whole suite keeps passing (`ctest --test-dir build --output-on-failure`).

---

## Task 1: `RepoWatcher` — refcounted mute

**Files:** Modify `ui/include/gittide/ui/repowatcher.hpp`, `ui/src/repowatcher.cpp`.
Test `tests/ui/test_repo_watcher.cpp`.

**Why:** `m_muted` is a plain `bool` and `unmute()` schedules `m_muted = false`
after one debounce window unconditionally. Nested `WatchMute` guards (the RAII
type in `repocontroller.cpp:26`) therefore break: an inner guard's release
un-mutes while the outer cascade is still writing to disk, so the outer's own
writes re-trigger the watcher; and the deferred release can land after a *later*
`mute()` has already begun, blinding a mutation that should be watched.

**Interfaces:** unchanged public API (`mute()` / `unmute()`); `m_muted` becomes
`int m_muteDepth` plus `quint64 m_unmuteGen` so a stale deferred release is
dropped.

- [x] **Step 1: Write the failing test** — `nested_mute_needs_matching_unmutes`:
      `mute(); mute(); unmute();` then touch a watched dir and `qWait` past the
      debounce → `worktreeChanged` must **not** fire. Then `unmute();` wait past
      the release window, touch again → it must fire. Fails today at the first
      assertion (the single `unmute()` clears the mute).
- [x] **Step 2: Make it pass** — `mute()` increments; `unmute()` decrements and,
      only on reaching 0, captures `++m_unmuteGen` into a `singleShot` lambda that
      clears the mute *only if* `m_unmuteGen` is unchanged. Guard against
      `unmute()` without `mute()` (clamp at 0).
- [x] **Step 3: Refactor / verify** — Doxygen on the header states the refcount
      contract; run the UI suite.

## Task 2: `RepoWatcher` — lossless re-arm

**Files:** Modify `ui/include/gittide/ui/repowatcher.hpp`, `ui/src/repowatcher.cpp`.
Test `tests/ui/test_repo_watcher.cpp`.

**Why:** `RepoController::rearmWatch()` runs at the end of **every** refresh
cascade and calls `watch()`, which starts with `clear()`:
`m_fsw->removePaths(all)`, `m_timer->stop()`, and `m_pendingWork = m_pendingGit =
false`. So (a) any filesystem event that arrived while the refresh was running is
thrown away, and (b) there is a window with zero watches installed. This is the
"it refreshes only sometimes" symptom. Removing and re-adding every path on each
cascade is also needless inotify churn on a large repo.

**Interfaces:**

```cpp
/// Replace the watch set with the directories in @p targets. Incremental: only
/// paths that left the set are removed and only new ones are added, so no event
/// is lost across a re-arm, and a debounce batch already in flight survives.
void watch(const gittide::WatchTargets& targets);

/// Stop watching everything and drop any pending batch. Repo close only.
void clear();
```

- [x] **Step 1: Write the failing test** — `rearm_preserves_pending_batch`: watch
      targets, touch a watched dir, and *before* the debounce elapses call
      `watch(sameTargets)` again; assert `worktreeChanged` still fires. Fails
      today (`clear()` wipes `m_pendingWork` and stops the timer).
- [x] **Step 2: Write the second failing test** — `rearm_keeps_watches_installed`:
      after a re-arm, `m_fsw->directories()` still contains every target dir and a
      change fired immediately after the re-arm is reported. Assert via the signal,
      not the internals.
- [x] **Step 3: Make it pass** — `watch()` computes the desired path set (target
      dirs + `m_activeFile`), diffs it against `m_fsw->directories() +
      m_fsw->files()`, calls `removePaths` on the difference and `addPaths` on the
      addition, and touches neither `m_timer` nor the pending flags. `clear()`
      keeps its current full-reset behaviour and stays the repo-close path.
- [x] **Step 4: Refactor / verify** — `setActiveFile()` reuses the same diffing so
      it cannot drop a directory watch; run the UI suite.

## Task 3: `RepoController` — single-flight cascade + graph in the refresh set

**Files:** Modify `ui/include/gittide/ui/repocontroller.hpp`,
`ui/src/repocontroller.cpp`. Test `tests/ui/test_repo_controller.cpp`.

**Why (a):** `refreshAll()` is fired by the git-dir watcher, by window-focus
resync, and by the tail of most mutations. Nothing stops three of them running
concurrently; each is five sequential `co_await`s of real git work on the shared
`QtConcurrent` pool. Stacked cascades are why refreshes feel randomly slow.

**Why (b):** `refreshAll()` refreshes status + branches + history + sync + stash,
never graph. `refreshGraph()` is called from exactly one place —
`WorkingPane.qml:98`, on `tabs.currentIndexChanged === 2`. So the Graph tab never
updates from a watcher event, and switching repos *while already on the Graph tab*
leaves the previous repo's graph on screen because the tab index never changes.

**Interfaces:**

```cpp
/// Full refresh cascade: status + branches + history + sync + stash, plus graph
/// when a graph view is subscribed (see setGraphSubscribed). Single-flight: a
/// call made while one is running sets a pending flag and the running cascade
/// re-runs once at the end, so N triggers collapse into at most 2 passes.
QCoro::Task<void> refreshAll();

/// Tell the controller whether a graph view is on screen. While true, refreshAll
/// also re-emits graphReady/refTipsReady; while false the (expensive, all-refs)
/// graph walk is skipped.
void setGraphSubscribed(bool subscribed);
```

- [x] **Step 1: Write the failing test** — `refresh_all_is_single_flight`: fire
      `refreshAll()` three times in one event-loop turn on a temp repo; count
      `statusChanged` emissions; assert ≤ 2. Fails today (3).
- [x] **Step 2: Write the second failing test** — `refresh_all_emits_graph_when_subscribed`:
      `setGraphSubscribed(true)` then `refreshAll()` → a `graphReady` arrives;
      with `false` → none. Fails today (never emitted from `refreshAll`).
- [x] **Step 3: Make it pass** — `m_refreshAllActive` / `m_refreshAllPending`
      around the body; a trailing `if (m_refreshAllPending) { clear; co_await
      refreshAll(); }`. Append `if (m_graphSubscribed) co_await refreshGraph();`
      to the cascade. Keep the existing `QPointer self` guards after each await.
- [x] **Step 4: Refactor / verify** — run the UI suite.

## Task 4: `RepoViewModel` — graph subscription + graph on open

**Files:** Modify `ui/include/gittide/ui/repoviewmodel.hpp`,
`ui/src/repoviewmodel.cpp`, `ui/qml/WorkingPane.qml`. Test
`tests/ui/test_repo_view_model.cpp`, `tests/ui/test_qml_graph.cpp`.

**Why:** `open()` (repoviewmodel.cpp:191-198) refreshes status/branches/history/
sync/stash but not graph, and `open()` is used for an in-place repo switch (the
sidebar never calls `close()` first), so `m_graph` keeps the previous repo's rows.

**Interfaces:**

```cpp
Q_PROPERTY(bool graphVisible READ graphVisible WRITE setGraphVisible NOTIFY graphVisibleChanged)
/// Set by the QML tab strip. Turning it true refreshes the graph immediately;
/// it also gates the controller's graph refresh inside refreshAll.
void setGraphVisible(bool visible);
```

- [x] **Step 1: Write the failing test** — `open_refreshes_graph_when_visible`:
      `setGraphVisible(true)`, `open(repoA)`, wait, `open(repoB)`, wait; assert
      `graph()->rowCount()` matches repo B's commit count. Fails today (still A's).
- [x] **Step 2: Make it pass** — add the property; `setGraphVisible(true)` calls
      `refreshGraph()` when a repo is open; `open()` calls `refreshGraph()` when
      `m_graphVisible`; forward the flag to `m_controller->setGraphSubscribed()`.
- [x] **Step 3: QML** — replace `WorkingPane.qml`'s
      `onCurrentIndexChanged: if (currentIndex === 2 …) repoVm.refreshGraph()` with
      a declarative `Binding { target: repoVm; property: "graphVisible"; value:
      tabs.currentIndex === 2 }`, so the flag is right on every path (tab switch,
      repo switch, startup) rather than only on an index *change*.
- [x] **Step 4: Refactor / verify** — run the UI suite.

## Task 5: `RepoListModel` — address rows by path

**Files:** Modify `ui/include/gittide/ui/repolistmodel.hpp`,
`ui/src/repolistmodel.cpp`. Test `tests/ui/test_repo_list_model.cpp`.

**Why:** Task 6 needs to update the tree row for *the repo currently open*, which
may be a submodule nested at any depth. Today `setRepoHead`/`setSyncCounts` take a
top-level row index only, so a submodule opened as a repo can never be updated.

**Interfaces:**

```cpp
/// Set branch / detached / shortOid / dirtyCount on the node with this exact
/// path, at any depth. No-op (returns false) when the path is not in the tree.
bool setRepoHeadByPath(const QString& path, const QString& branch, bool detached,
                       const QString& shortOid, int dirtyCount);
/// Set ahead / behind / hasUpstream on the node with this exact path, any depth.
bool setSyncCountsByPath(const QString& path, int ahead, int behind, bool hasUpstream);
```

- [x] **Step 1: Write the failing test** — build a tree with one repo and one
      submodule; `setRepoHeadByPath(submodulePath, …)` → `DirtyCountRole` on the
      child index reflects it and exactly one `dataChanged` fires for that index.
      Fails today (no such method).
- [x] **Step 2: Make it pass** — implement both on top of the existing
      `findByPath` + `rowOf`; emit `dataChanged` with the same role list the
      row-indexed setters use. Refactor `setRepoHead`/`setSyncCounts` to delegate
      so there is one implementation.
- [x] **Step 3: Refactor / verify** — run the UI suite.

## Task 6: The active repo pushes its state into its sidebar row

**Files:** Modify `ui/include/gittide/ui/repoviewmodel.hpp`,
`ui/src/repoviewmodel.cpp`, `ui/include/gittide/ui/projectcontroller.hpp`,
`ui/src/projectcontroller.cpp`, `app/qml_main.cpp`. Test
`tests/ui/test_project_controller.cpp`.

**Why:** This is the reported bug. `RepoListModel::dirtyCount` — the sidebar's
"● N" badge — is written **only** by `ProjectController::pollRepos()`, a 5 s timer
gated on window focus. `RepoController` and `ProjectController` are never
connected (`app/qml_main.cpp` wires each to QML separately). So reverting a change
in GitTide empties the Changes pane instantly and leaves the badge reading 1 for
up to 5 s — indefinitely if the window is not focused.

**Interfaces:**

```cpp
// RepoViewModel — emitted whenever the open repo's own refresh produces a new
// head/status/sync picture. Lets the sidebar row for this repo track in-app
// mutations without waiting for the fleet poll.
signals:
    void activeRepoStateChanged(QString path, QString branch, bool detached,
                                QString shortOid, int dirtyCount);
    void activeRepoSyncChanged(QString path, int ahead, int behind, bool hasUpstream);

// ProjectController — apply that state to the tree row for `path` (any depth).
public slots:
    void applyActiveRepoState(const QString& path, const QString& branch, bool detached,
                              const QString& shortOid, int dirtyCount);
    void applyActiveRepoSync(const QString& path, int ahead, int behind, bool hasUpstream);
```

- [x] **Step 1: Write the failing test** — with a `ProjectController` holding one
      repo, call `applyActiveRepoState(path, "master", false, "abc1234", 0)` and
      assert `DirtyCountRole` on that row is 0 without the poll timer having run.
      Fails today (no such slot).
- [x] **Step 2: Write the second failing test** (the reported bug, at VM level) —
      in `test_repo_view_model.cpp`: open a temp repo with one modified file, spy
      on `activeRepoStateChanged`, discard the file, and assert the last emission
      carries `dirtyCount == 0`. Fails today (no signal).
- [x] **Step 3: Make it pass** — emit `activeRepoStateChanged` from
      `RepoViewModel::onStatus` and `onHead` (dirty count = `m_files->rowCount()`,
      branch/oid from the last head), and `activeRepoSyncChanged` from the
      `syncStatusChanged` lambda. Implement both `ProjectController` slots on
      Task 5's by-path setters. Connect them in `app/qml_main.cpp`.
- [x] **Step 4: Refactor / verify** — run the UI suite; confirm no double-update
      fight with the poll (both write the same values, poll skips the active repo
      after Task 7).

## Task 7: Fleet poll — single-flight, skips the active repo

**Files:** Modify `ui/include/gittide/ui/projectcontroller.hpp`,
`ui/src/projectcontroller.cpp`. Test `tests/ui/test_project_controller.cpp`.

**Why:** `pollRepos()` has no re-entrancy guard. It opens every repo in the
project and awaits four git operations on each, sequentially, on the shared
`QtConcurrent` pool — and the timer re-fires every 5 s regardless. With a handful
of repos the polls overlap and stack, saturating the same pool the active repo's
refreshes use. That is the diffuse "everything is randomly slow" symptom. After
Task 6 the active repo is live-updated, so the poll re-reading it is pure waste.

**Interfaces:** `bool m_polling`; `QString m_activeRepoPath` (set by the existing
`setActiveRepo`, which QML already calls on every repo open — `Main.qml:512`).

- [x] **Step 1: Write the failing test** — `poll_does_not_overlap`: a controller
      with a short interval over a repo; drive the event loop and assert the poll
      body never runs re-entrantly (instrument via a counter incremented on entry
      and asserted ≤ 1 at any suspension point, or assert total open-calls over N
      ticks ≤ N). Fails today.
- [x] **Step 2: Make it pass** — early-return when `m_polling`; RAII-clear it on
      every exit path including the `co_return`s. `continue` past the row whose
      path equals `m_activeRepoPath`.
- [x] **Step 3: Refactor / verify** — set `m_activeRepoPath` in `setActiveRepo`
      (it already receives it) and clear it when the project changes; run the UI
      suite.

## Task 8: `RepoListModel::setRepos` stops blocking the UI thread

**Files:** Modify `ui/src/repolistmodel.cpp`, `ui/src/projectcontroller.cpp`.
Test `tests/ui/test_repo_list_model.cpp`, `tests/ui/test_project_controller.cpp`.

**Why:** `setRepos` (repolistmodel.cpp:104-127) runs `GitRepo::open` +
`submoduleTree` + `head` + `status` + `syncStatus` **synchronously, on the UI
thread, for every repo** — and `activate()` calls it on every project switch.
That is the stall when switching projects. The identical data is already produced
asynchronously by `pollRepos()`, so this is duplicated logic as well as a freeze.

- [x] **Step 1: Write the failing test** — `set_repos_does_no_git_io`: point a
      `RepoRef` at a valid repo, call `setRepos`, and assert the row is present
      with its display name but `branch` empty and `dirtyCount == 0` (i.e. not yet
      hydrated). Fails today (populated synchronously).
- [x] **Step 2: Write the second failing test** — `activate_hydrates_rows`: after
      `activate()`, spin the event loop and assert branch/dirtyCount are filled in.
- [x] **Step 3: Make it pass** — reduce `setRepos` to display name + path +
      `missing` (a `std::filesystem::exists` check only, no libgit2). Have
      `activate()` / `refreshRepoModel()` kick `pollRepos()` immediately after so
      hydration happens off-thread through the existing path.
- [x] **Step 4: Refactor / verify** — update any existing `test_repo_list_model`
      case that asserted synchronous hydration; run the whole suite.

## Task 9: Selection becomes ViewModel-owned — changed files

**Files:** Modify `ui/include/gittide/ui/repoviewmodel.hpp`,
`ui/src/repoviewmodel.cpp`, `ui/qml/ChangesPane.qml`. Test
`tests/ui/test_repo_view_model.cpp`, `tests/ui/test_qml_shell.cpp`.

**Why:** the second reported symptom. `ChangesPane.qml:123` paints the highlight
from `ListView.isCurrentItem`, i.e. the view's own `currentIndex`, which is only
ever assigned in the click handler (`:143`) and the arrow keys. The VM changes the
selection without a click in two places — `onStatus` re-selects the preserved
`m_activeFile` after `setFiles` has reset the model (`repoviewmodel.cpp:647-656`),
and `open()` clears it — and a model reset snaps `currentIndex` to 0 regardless.
So the diff pane shows file X while row 0 is highlighted.

**Interfaces:**

```cpp
/// Row of activeFile in changedFiles, or -1 when nothing is selected. The single
/// source of truth for the changed-file list's highlight: QML binds currentIndex
/// to it and never assigns currentIndex itself.
Q_PROPERTY(int activeFileRow READ activeFileRow NOTIFY activeFileChanged)
int activeFileRow() const;
```

- [x] **Step 1: Write the failing test** — open a repo with files `a`, `b`, `c`;
      `selectFile("c")`; trigger a status refresh that drops `a` (stage it);
      assert `activeFile == "c"` **and** `activeFileRow` equals
      `changedFiles()->rowForPath("c")` (now shifted). The row property does not
      exist yet → fails.
- [x] **Step 2: Make it pass** — implement `activeFileRow()` over
      `m_files->rowForPath(m_activeFile)`; make sure `activeFileChanged` is emitted
      after `setFiles` in `onStatus` so the binding re-evaluates against the new
      rows (currently `selectFile` is called before the emission ordering matters).
- [x] **Step 3: QML** — `fileList.currentIndex: repoVm ? repoVm.activeFileRow : -1`
      as a **binding**; delete the `fileList.currentIndex = index` assignment from
      the click handler (it now flows through `repoVm.selectFile`); rewrite
      `Keys.onUp/DownPressed` to call `repoVm.selectFileAtRow(activeFileRow ± 1)`
      instead of mutating `currentIndex`. Same treatment for the stash-preview
      branch of the same list (`repoVm.selectCommitFile`).
- [x] **Step 4: Refactor / verify** — add a QML test asserting `fileList.currentIndex`
      tracks `repoVm.activeFile` after a VM-driven selection change; run the suite.

## Task 10: Selection becomes ViewModel-owned — commit files, history, graph

**Files:** Modify `ui/include/gittide/ui/repoviewmodel.hpp`,
`ui/src/repoviewmodel.cpp`, `ui/include/gittide/ui/historylistmodel.hpp`,
`ui/src/historylistmodel.cpp`, `ui/qml/CommitDetail.qml`, `ui/qml/HistoryPane.qml`,
`ui/qml/GraphPane.qml`. Test `tests/ui/test_repo_view_model.cpp`,
`tests/ui/test_qml_history.cpp`, `tests/ui/test_qml_graph.cpp`.

**Why:** the same defect in three more lists, and the worst instance of it:
`onCommitFiles` (`repoviewmodel.cpp:855`) auto-selects file row 0 so its diff
loads without a click — but `commitFilesList.currentIndex` is never told, so the
commit's diff appears with **no file marked**. Exactly the reported "I see changes
but I don't see which file they belong to". History and Graph have the same gap on
every VM-driven `selectedCommit` change (undo toast, post-rebase re-anchor at
`repoviewmodel.cpp:938`, graph→history hand-off).

**Interfaces:**

```cpp
// RepoViewModel
Q_PROPERTY(int activeCommitFileRow READ activeCommitFileRow NOTIFY activeCommitFileChanged)
Q_PROPERTY(int selectedCommitRow   READ selectedCommitRow   NOTIFY selectedCommitChanged)
Q_PROPERTY(int selectedGraphRow    READ selectedGraphRow    NOTIFY selectedCommitChanged)

// HistoryListModel — row of `oid`, or -1. Needed by the two row properties above.
int rowForOid(const QString& oid) const;
```

- [x] **Step 1: Write the failing test** — `commit_file_autoselect_reports_row`:
      `selectCommit(oid)` on a commit touching two files; after the async load,
      assert `activeCommitFile` is file 0 **and** `activeCommitFileRow == 0`.
      Fails today (no property).
- [x] **Step 2: Write the second failing test** — `selected_commit_row_follows_oid`:
      `selectCommit(secondOid)` → `selectedCommitRow == 1`; and after `open()` of a
      different repo → `-1`.
- [x] **Step 3: Make it pass** — `HistoryListModel::rowForOid` (linear scan over
      `m_layout.rows`; the list is bounded by the 1000-commit limit); the three VM
      properties on top of it and `m_commitFiles->rowForPath`.
- [x] **Step 4: QML** — bind `commitFilesList.currentIndex`,
      `historyList.currentIndex` and `graphList.currentIndex` to the matching
      property; drop the direct `currentIndex = index` assignments; route arrow
      keys through the `select*AtRow` verbs. In `HistoryPane`, keep `selectedRows`
      for multi-select but re-seed it to `[selectedCommitRow]` whenever
      `selectedCommitRow` changes from outside a click (a `Connections` on
      `onSelectedCommitChanged` that resets the range unless the change came from
      the pane's own multi-select gesture).
- [x] **Step 5: Refactor / verify** — QML tests assert each list's `currentIndex`
      after a VM-driven selection; run the whole suite.

## Task 11: Close the in-place-edit gap for the active repo

**Files:** Modify `ui/include/gittide/ui/repocontroller.hpp`,
`ui/src/repocontroller.cpp`. Test `tests/ui/test_repo_controller.cpp`. Update
`docs/decisions.md` (D35) and `docs/spec/engineering/engineering.md`.

**Why:** `QFileSystemWatcher`'s **directory** watches report create/delete/rename/
attribute changes to entries, not content writes to an existing file. Only the one
file in `setActiveFile` is watched individually. So an editor that saves in place
(no atomic rename) makes a tracked file dirty and GitTide shows nothing until the
window regains focus. D35 named this gap and closed it with focus-resync alone,
which is not enough when GitTide and the editor are visible side by side.

**Decision to record:** amend D35 with a **status-only** safety net for the active
repo — not the "poll the active repo" approach D35 rejected. It runs `status()`
and nothing else (no history, no graph, no branches), only while the window is
active, only when the watcher has been quiet for longer than the interval, and it
is skipped entirely while a mutation or sync is in flight. Cost is one `git status`
every `kActiveStatusInterval` (default 8 s) against an already-open handle.

- [x] **Step 1: Write the failing test** — open a repo, let it settle, modify an
      existing tracked file **in place** (open/write/close, no rename), and assert
      `statusChanged` arrives within ~2 × the (test-injected, short) interval
      without any focus event. Fails today.
- [x] **Step 2: Make it pass** — `QTimer m_activeStatusTimer`, started by `open()`
      and stopped when no repo is open; its slot returns early when a refresh is
      in flight (Task 3's flag) or the watcher fired within the last interval;
      otherwise `co_await refreshStatus()`. Injectable interval, matching the
      existing `watchDebounceMs` ctor-parameter convention.
- [x] **Step 3: Wire the focus gate** — reuse `ProjectController::setWindowActive`'s
      trigger: add `RepoViewModel::setWindowActive(bool)` forwarding to the
      controller, called from `Main.qml`'s existing `onActiveChanged`, so the timer
      does not tick in the background.
- [x] **Step 4: Docs** — amend the D35 entry in `docs/decisions.md` (state the
      residual gap and this bounded exception, keeping the original rejection of a
      *full* active-repo poll intact) and update the live-refresh section of
      `spec/engineering/engineering.md`.
- [x] **Step 5: Refactor / verify** — run the whole suite.

## Task 12: Close-out

- [x] Update `spec/engineering/engineering.md` §Live refresh with the finished
      model: watcher (lossless, refcounted mute) → controller (single-flight
      cascade, graph subscription, status safety net) → sidebar (pushed from the
      active repo, polled for the rest); and the selection-ownership rule —
      **the ViewModel owns selection, QML binds to it and never assigns
      `currentIndex`**.
- [x] Symbol-level facts stay in Doxygen next to each new member.
- [x] Tick every checkbox above and fill in **Outcome**.
- [x] `ctest --test-dir build --output-on-failure` green.

---

## Outcome

**Shipped.** Refresh is coherent end to end, and list selection has a single
owner. Two user-visible bugs are fixed with regression tests named after them:
the sidebar's dirty badge no longer lags an in-app revert
(`discardEmitsActiveRepoStateWithZeroDirtyCount`,
`applyActiveRepoState_updates_the_row_without_polling`), and a commit's diff can
no longer appear with no file marked (`commitFileAutoSelectReportsItsRow`,
`file_list_highlight_follows_the_view_model`).

Where each defect landed:

| Was | Now |
|--|--|
| Re-arm wiped the pending batch and every watch | Incremental set diff; timer and batch untouched |
| `mute()` a bool with an unconditional timed release | Reference-counted with a generation-stamped release |
| `refreshAll` stacked, and never covered the graph | Single-flight; graph in the cascade while subscribed |
| Graph refreshed only on a tab-index *change* | `graphVisible` binding — correct across repo switches |
| Sidebar row written only by the 5 s fleet poll | Active repo pushes head/status/sync by path |
| Poll re-entrant, and re-read the active repo | Single-flight, skips the open repo |
| `setRepos` ran libgit2 on the UI thread | No git I/O; `activate()` kicks async hydration |
| Four lists each owned a `currentIndex` | ViewModel exposes the row; QML binds one-way |
| In-place saves invisible until refocus | Status-only safety net, focus-gated |

**Spec updated:** `spec/engineering/engineering.md` §Live refresh (rewritten:
incremental re-arm, refcounted mute, single-flight cascade, graph subscription,
status safety net, the poll's new role, the active repo's push) and a new
§Selection ownership. `decisions.md` gains **D35a** (amends D35 — the safety net
and the refresh plumbing) and **D35b** (selection ownership).

**Code:**
- `ui/{include/gittide/ui,src}/repowatcher.*` — `applyWatchSet`, `m_muteDepth`,
  `m_unmuteGen`
- `ui/{include/gittide/ui,src}/repocontroller.*` — single-flight `refreshAll`,
  `setGraphSubscribed`, `setWindowActive`, `pollActiveStatus`
- `ui/{include/gittide/ui,src}/repoviewmodel.*` — `graphVisible`,
  `activeRepoStateChanged`/`activeRepoSyncChanged`, `activeFileRow`,
  `activeCommitFileRow`, `selectedCommitRow`, `selectedGraphRow`
- `ui/{include/gittide/ui,src}/repolistmodel.*` — `setRepoHeadByPath`,
  `setSyncCountsByPath`, `setRepos` stripped of git I/O
- `ui/{include/gittide/ui,src}/projectcontroller.*` — `applyActiveRepoState`,
  `applyActiveRepoSync`, poll guard, `hydrateRepoModel`
- `ui/{include/gittide/ui,src}/historylistmodel.*` — `rowForOid`
- `ui/qml/` — `ChangesPane`, `CommitDetail`, `HistoryPane`, `GraphPane`,
  `WorkingPane`, `Main`
- `app/qml_main.cpp` — the ViewModel → ProjectController connections

**Follow-up not taken:** `rowForOid` is a linear scan over the layout (bounded by
the 1000-commit history limit, run once per selection change). If the limit ever
rises, it wants an oid → row hash maintained by `setLayout`.
