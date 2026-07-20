# Plan 33 — Repo tree entry redesign (branch + sync + dirty)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-20 |
| **Status** | `planned` |
| **Spec** | [`spec/product/2026-07-20-repo-tree-entry-redesign-design.md`](../spec/product/2026-07-20-repo-tree-entry-redesign-design.md) |
| **Depends on** | Plan 21 (live refresh / fleet poll), Plan 24 (submodule rows) |

**Goal:** Turn each top-level sidebar repo row into a taller, two-line entry
showing the **current branch**, **ahead/behind vs upstream**, and **dirty/clean**
state — instead of just the repo name.

**Architecture:** `RepoListModel` gains four per-repo fields (branch, detached,
dirtyCount, hasUpstream) + roles + two setters; the existing `shortOid`/`ahead`/
`behind` fields are reused. `RepoListModel::setRepos` seeds the new state
synchronously from `GitRepo::head()`/`status()`/`syncStatus()` (it already opens
each repo for `submoduleTree()`). `ProjectController::pollRepos` refreshes the
same state live via `AsyncRepo`. `Sidebar.qml`'s delegate is restructured into a
two-line layout. Submodule rows are unchanged.

**Tech stack:** C++23, Qt 6 (Quick/QML + QtTest), libgit2 (via core `GitRepo`/
`AsyncRepo`), QCoro coroutines.

## Global constraints

- **No Qt in `core/`** — this plan touches only `ui/`; do not add includes to
  core. Colours come from **theme tokens** (`theme.*`), never hex literals
  ([`spec/engineering/engineering.md`](../spec/engineering/engineering.md)).
- **TDD** — write the failing test first for every task.
- Paths via `generic_string()`, never `.string()`.
- No new source files are needed. New/changed tests live in already-registered
  files (`tests/ui/test_repo_list_model.cpp`, `tests/ui/test_project_controller.cpp`,
  `tests/ui/test_qml_sync.cpp`) — **no `tests/CMakeLists.txt` or `main.cpp` edits**.
- Keep passing: existing `TestRepoListModel`, `TestProjectController`,
  `TestQmlSync` slots; the `repoTree` / `fetchAllButton` object names.
- Build: `cmake --build build --parallel`. Test one class:
  `ctest --test-dir build -R gittide_ui_tests --output-on-failure` (the UI tests
  are one aggregated binary; filter by test-function name in the run output).

---

## Task 1: Model — new fields, roles, and setters

Add the per-repo state the delegate will bind to, plus the two setters the
producer (Task 2/3) will call. Pure model change — no git, no QML.

**Files:**
- Modify: `ui/include/gittide/ui/repolistmodel.hpp`
- Modify: `ui/src/repolistmodel.cpp`
- Test: `tests/ui/test_repo_list_model.cpp`

**Interfaces (produced):**
- Roles: `BranchRole`, `DetachedRole`, `DirtyCountRole`, `HasUpstreamRole`
  (QML names `"branch"`, `"detached"`, `"dirtyCount"`, `"hasUpstream"`).
- `void setRepoHead(int rootRow, const QString& branch, bool detached, const QString& shortOid, int dirtyCount);`
- `void setSyncCounts(int rootRow, int ahead, int behind, bool hasUpstream);` — **signature changed** (added `hasUpstream`).

- [ ] **Step 1: Write the failing test** — add two slots to `TestRepoListModel`
  in `tests/ui/test_repo_list_model.cpp`, and update the one existing
  `setSyncCounts` call (line ~147) to pass the new arg.

```cpp
    void repo_head_and_dirty_roles_roundtrip()
    {
        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = "/home/u/api"}});
        const QModelIndex i0 = m.index(0, 0);

        // Defaults.
        QCOMPARE(m.data(i0, RepoListModel::BranchRole).toString(), QString());
        QCOMPARE(m.data(i0, RepoListModel::DetachedRole).toBool(), false);
        QCOMPARE(m.data(i0, RepoListModel::DirtyCountRole).toInt(), 0);
        QCOMPARE(m.data(i0, RepoListModel::HasUpstreamRole).toBool(), false);

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        m.setRepoHead(0, QStringLiteral("main"), false, QStringLiteral("abc1234"), 3);
        QCOMPARE(m.data(i0, RepoListModel::BranchRole).toString(), QStringLiteral("main"));
        QCOMPARE(m.data(i0, RepoListModel::DirtyCountRole).toInt(), 3);
        QVERIFY(spy.count() >= 1);

        // Detached: branch empty, detached true, short oid carried in ShortOidRole.
        m.setRepoHead(0, QString(), true, QStringLiteral("deadbee"), 0);
        QCOMPARE(m.data(i0, RepoListModel::DetachedRole).toBool(), true);
        QCOMPARE(m.data(i0, RepoListModel::ShortOidRole).toString(), QStringLiteral("deadbee"));

        // hasUpstream flows through setSyncCounts.
        m.setSyncCounts(0, 2, 1, true);
        QCOMPARE(m.data(i0, RepoListModel::AheadRole).toInt(), 2);
        QCOMPARE(m.data(i0, RepoListModel::HasUpstreamRole).toBool(), true);
    }

    void setRepoHead_out_of_range_is_noop()
    {
        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = "/home/u/api"}});
        m.setRepoHead(9, QStringLiteral("x"), false, QString(), 0); // must not crash
        QCOMPARE(m.topLevelCount(), 1);
    }
```

  And change the existing call in `fetchState_roundtrips_and_resets`:
  ```cpp
        m.setSyncCounts(0, 1, 3, false);   // was setSyncCounts(0, 1, 3)
  ```

- [ ] **Step 2: Run to verify it fails** — the file does not compile:
  `BranchRole`/`setRepoHead` undefined and `setSyncCounts` takes 3 args.
  Run: `cmake --build build --parallel` → Expected: compile error
  `'BranchRole' is not a member of RepoListModel` (or similar).

- [ ] **Step 3: Add the roles** — in `repolistmodel.hpp`, extend the `Roles`
  enum (after `BehindRole`):

```cpp
        AheadRole,
        BehindRole,
        BranchRole,
        DetachedRole,
        DirtyCountRole,
        HasUpstreamRole,
        BusyRole,
        OwnerRepoPathRole,
```

- [ ] **Step 4: Add the Node fields** — in the `Node` struct (after `behind`):

```cpp
        int                                ahead  = 0;
        int                                behind = 0;
        QString                            branch;
        bool                               detached    = false;
        int                                dirtyCount   = 0;
        bool                               hasUpstream  = false;
```

- [ ] **Step 5: Declare the setters** — in `repolistmodel.hpp`, replace the
  `setSyncCounts` declaration and add `setRepoHead`:

```cpp
    void setSyncCounts(int rootRow, int ahead, int behind, bool hasUpstream);
    /// Set the current-branch / dirty state of the top-level repo at `rootRow`.
    /// `shortOid` is used only for the detached-HEAD fallback (reuses ShortOidRole).
    void setRepoHead(int rootRow, const QString& branch, bool detached,
                     const QString& shortOid, int dirtyCount);
```

- [ ] **Step 6: Emit the roles from `data()`** — in `repolistmodel.cpp`, add
  cases (after `BehindRole`):

```cpp
    case BranchRole:
        return node->branch;
    case DetachedRole:
        return node->detached;
    case DirtyCountRole:
        return node->dirtyCount;
    case HasUpstreamRole:
        return node->hasUpstream;
```

- [ ] **Step 7: Register the QML role names** — in `roleNames()` (after
  `BehindRole`):

```cpp
    roles[BranchRole]         = "branch";
    roles[DetachedRole]       = "detached";
    roles[DirtyCountRole]     = "dirtyCount";
    roles[HasUpstreamRole]    = "hasUpstream";
```

- [ ] **Step 8: Update `setSyncCounts` and add `setRepoHead`** — in
  `repolistmodel.cpp`, change `setSyncCounts` to store `hasUpstream` and add the
  new setter beneath it:

```cpp
void RepoListModel::setSyncCounts(int rootRow, int ahead, int behind, bool hasUpstream)
{
    if (rootRow < 0 || rootRow >= static_cast<int>(m_roots.size()))
        return;
    Node& n        = *m_roots[rootRow];
    n.ahead        = ahead;
    n.behind       = behind;
    n.hasUpstream  = hasUpstream;
    const QModelIndex idx = createIndex(rootRow, 0, &n);
    emit dataChanged(idx, idx, {AheadRole, BehindRole, HasUpstreamRole});
}

void RepoListModel::setRepoHead(int rootRow, const QString& branch, bool detached,
                                const QString& shortOid, int dirtyCount)
{
    if (rootRow < 0 || rootRow >= static_cast<int>(m_roots.size()))
        return;
    Node& n       = *m_roots[rootRow];
    n.branch      = branch;
    n.detached    = detached;
    n.shortOid    = shortOid;
    n.dirtyCount  = dirtyCount;
    const QModelIndex idx = createIndex(rootRow, 0, &n);
    emit dataChanged(idx, idx, {BranchRole, DetachedRole, ShortOidRole, DirtyCountRole});
}
```

  The changed `setSyncCounts` signature breaks its two existing callers in
  `ui/src/projectcontroller.cpp` — update **both now** so the tree compiles
  (Task 3 later adds the head/status refresh to `pollRepos`):
  - `fetchOne` (~line 420):
    `m_repoModel->setSyncCounts(row, ahead, behind, st->hasUpstream);`
  - `pollRepos` (~line 67):
    `m_repoModel->setSyncCounts(row, st->ahead, st->behind, st->hasUpstream);`

- [ ] **Step 9: Run the tests — verify pass**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: `repo_head_and_dirty_roles_roundtrip` and
  `setRepoHead_out_of_range_is_noop` PASS; existing slots still PASS.

- [ ] **Step 10: Commit**

```bash
git add ui/include/gittide/ui/repolistmodel.hpp ui/src/repolistmodel.cpp \
        ui/src/projectcontroller.cpp tests/ui/test_repo_list_model.cpp
git commit -m "feat(ui): RepoListModel exposes branch/detached/dirty/upstream roles"
```

---

## Task 2: Seed repo state from disk in `setRepos`

`setRepos` already opens each present repo to build its submodule subtree. Reuse
that handle to seed branch / detached / shortOid / dirtyCount / ahead / behind /
hasUpstream, so a row shows real state on the very first render (before any poll).

**Files:**
- Modify: `ui/src/repolistmodel.cpp` (`setRepos`, ~lines 89–97)
- Test: `tests/ui/test_repo_list_model.cpp`

**Interfaces (consumed):** `GitRepo::head()` → `HeadState{branch, oid, detached}`,
`GitRepo::status()` → `std::vector<FileStatus>`, `GitRepo::syncStatus()` →
`SyncStatus{ahead, behind, hasUpstream}`.

- [ ] **Step 1: Write the failing test** — add a slot to `TestRepoListModel`
  (this file already includes `support/temprepo.hpp`):

```cpp
    void setRepos_seeds_branch_dirty_and_upstream_from_disk()
    {
        using namespace gittide::test;
        TempRepo repo;
        repo.setIdentity("Test", "test@example.com");
        repo.writeFile("a.txt", "one\n");
        repo.commitAll("c1");
        repo.writeFile("a.txt", "two\n"); // uncommitted change → dirty

        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = repo.path().generic_string(), .alias = "r"}});
        const QModelIndex i0 = m.index(0, 0);

        QVERIFY(!m.data(i0, RepoListModel::BranchRole).toString().isEmpty()); // "master"/default
        QCOMPARE(m.data(i0, RepoListModel::DetachedRole).toBool(), false);
        QVERIFY(m.data(i0, RepoListModel::DirtyCountRole).toInt() >= 1);      // 1 modified file
        QCOMPARE(m.data(i0, RepoListModel::HasUpstreamRole).toBool(), false); // no remote
    }
```

- [ ] **Step 2: Run to verify it fails**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: FAIL — `DirtyCountRole` is 0 and `BranchRole` is empty (setRepos
  doesn't seed them yet).

- [ ] **Step 3: Seed in `setRepos`** — in the `if (present)` block of `setRepos`
  (`repolistmodel.cpp`), after the existing `submoduleTree()` seeding, add:

```cpp
        if (present)
        {
            auto repo = gittide::GitRepo::open(p);
            if (repo)
            {
                if (auto tree = repo->submoduleTree())
                    appendSubmodules(*root, *tree);

                if (auto hs = repo->head())
                {
                    root->branch   = QString::fromStdString(hs->branch);
                    root->detached = hs->detached;
                    root->shortOid = QString::fromStdString(
                        hs->oid.substr(0, std::min<std::size_t>(7, hs->oid.size())));
                }
                if (auto st = repo->status())
                    root->dirtyCount = static_cast<int>(st->size());
                if (auto sy = repo->syncStatus())
                {
                    root->ahead       = sy->ahead;
                    root->behind      = sy->behind;
                    root->hasUpstream = sy->hasUpstream;
                }
            }
        }
```

  Add `#include <algorithm>` at the top of `repolistmodel.cpp` (for `std::min`).

- [ ] **Step 4: Run the test — verify pass**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: `setRepos_seeds_branch_dirty_and_upstream_from_disk` PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/src/repolistmodel.cpp tests/ui/test_repo_list_model.cpp
git commit -m "feat(ui): seed repo branch/dirty/sync state in RepoListModel::setRepos"
```

---

## Task 3: Refresh repo state live in `ProjectController::pollRepos`

The 5-second fleet poll already refreshes ahead/behind. Extend it to also refresh
branch/detached/shortOid/dirtyCount so a non-active repo's row stays current when
its working tree or HEAD changes on disk.

**Files:**
- Modify: `ui/src/projectcontroller.cpp` (`pollRepos`, ~lines 63–72)
- Test: `tests/ui/test_project_controller.cpp`

**Interfaces (consumed):** `AsyncRepo::head()`, `AsyncRepo::status()`,
`AsyncRepo::syncStatus()` (all `QCoro::Task<Expected<...>>`); `RepoListModel::setRepoHead`,
`RepoListModel::setSyncCounts` (Task 1).

- [ ] **Step 1: Write the failing test** — add a slot to the test class in
  `tests/ui/test_project_controller.cpp`. Ensure these includes are present at
  the top of the file (add any that are missing): `<QtTest/QtTest>`,
  `"gittide/ui/projectcontroller.hpp"`, `"gittide/ui/repolistmodel.hpp"`,
  `"gittide/projectstore.hpp"`, `"support/temprepo.hpp"`.

```cpp
    void pollRepos_refreshes_branch_and_dirty()
    {
        using namespace gittide::test;
        using gittide::ui::ProjectController;
        using gittide::ui::RepoListModel;

        TempRepo repo;
        repo.setIdentity("Test", "test@example.com");
        repo.writeFile("a.txt", "one\n");
        repo.commitAll("c1");

        gittide::ProjectStore store;
        auto& p = store.createProject("P");
        store.addRepo(p.id, gittide::RepoRef{.path = repo.path().generic_string()});

        // Short poll interval so the timer fires quickly under QTRY.
        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/100);
        controller.activate(QString::fromStdString(p.id));

        RepoListModel*    model = controller.repos();
        const QModelIndex i0    = model->index(0, 0);
        QCOMPARE(model->data(i0, RepoListModel::DirtyCountRole).toInt(), 0); // seeded clean

        // Dirty the tree on disk, then let the poll pick it up.
        repo.writeFile("a.txt", "two\n");
        controller.setWindowActive(true);
        QTRY_COMPARE_WITH_TIMEOUT(model->data(i0, RepoListModel::DirtyCountRole).toInt(), 1, 5000);
        QVERIFY(!model->data(i0, RepoListModel::BranchRole).toString().isEmpty());
    }
```

  Also declare the slot in the class's `private slots:` list.

- [ ] **Step 2: Run to verify it fails**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: FAIL — `DirtyCountRole` stays 0 (pollRepos never refreshes it), so
  `QTRY_COMPARE_WITH_TIMEOUT` times out.

- [ ] **Step 3: Refresh head + dirty in `pollRepos`** — in the per-row loop of
  `pollRepos` (`projectcontroller.cpp`), after the `syncStatus` block and before
  the `submoduleTree` block, add:

```cpp
        auto st = co_await repo.syncStatus();
        if (!self)
            co_return;
        if (st)
            m_repoModel->setSyncCounts(row, st->ahead, st->behind, st->hasUpstream);

        QString branch, shortOid;
        bool    detached = false;
        int     dirty    = 0;
        if (auto hs = co_await repo.head(); hs)
        {
            branch   = QString::fromStdString(hs->branch);
            detached = hs->detached;
            shortOid = QString::fromStdString(
                hs->oid.substr(0, std::min<std::size_t>(7, hs->oid.size())));
        }
        if (!self)
            co_return;
        if (auto ds = co_await repo.status(); ds)
            dirty = static_cast<int>(ds->size());
        if (!self)
            co_return;
        m_repoModel->setRepoHead(row, branch, detached, shortOid, dirty);
```

  Add `#include <algorithm>` at the top of `projectcontroller.cpp` if not present.

- [ ] **Step 4: Run the test — verify pass**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: `pollRepos_refreshes_branch_and_dirty` PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): pollRepos refreshes repo branch/dirty state live"
```

---

## Task 4: Sidebar delegate — two-line repo entry

Restructure the `TreeViewDelegate` `contentItem` into a two-line layout for
top-level repos: line 1 = name (larger, `textPrimary`) + dirty badge; line 2 =
branch (smaller, `textSecondary`) + ahead/behind. Submodule rows keep their
current single-line rendering. All colours via `theme.*`.

**Files:**
- Modify: `ui/qml/Sidebar.qml` (delegate `implicitHeight` ~line 224; `contentItem`
  ~lines 257–366)
- Test: `tests/ui/test_qml_sync.cpp`

- [ ] **Step 1: Write the failing test** — add a slot to `TestQmlSync`
  (declare it in the class's `private slots:` and define it before the trailing
  `#include "test_qml_sync.moc"`):

```cpp
void TestQmlSync::sidebar_repo_row_exposes_branch_and_dirty()
{
    using namespace gittide::test;
    ThemeManager mgr;
    mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;

    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one\n");
    repo.commitAll("c1");
    repo.writeFile("a.txt", "two\n"); // dirty
    repoModel.setRepos({gittide::RepoRef{.path = repo.path().generic_string(), .alias = "r"}});

    gittide::ProjectStore store;
    auto& p = store.createProject("P");
    ProjectController controller(&store);
    controller.activate(QString::fromStdString(p.id));

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QVERIFY(!engine.rootObjects().isEmpty()); // Main.qml + redesigned delegate load, no fatal QML error

    // The data the two-line delegate binds is present on the row.
    const QModelIndex i0 = repoModel.index(0, 0);
    QVERIFY(!repoModel.data(i0, RepoListModel::BranchRole).toString().isEmpty());
    QVERIFY(repoModel.data(i0, RepoListModel::DirtyCountRole).toInt() >= 1);
}
```

  Declaration line to add under `private slots:`:
  ```cpp
    void sidebar_repo_row_exposes_branch_and_dirty();
  ```

- [ ] **Step 2: Run to verify it fails / regresses** — before the QML change the
  test compiles but is a weak guard; run it to confirm it currently PASSES on the
  *model* assertions (they rely on Task 2). This slot's real job is to lock that
  `Main.qml` keeps loading after the delegate rewrite.
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: PASS (model data present). Proceed to the delegate rewrite; re-run in
  Step 4 to prove no QML load regression.

- [ ] **Step 3: Rewrite the delegate** — in `ui/qml/Sidebar.qml`:

  (a) Make repo rows taller (submodules unchanged) — replace line ~224:
  ```qml
                implicitHeight: row.isSub ? 30 : 46
  ```

  (b) Replace the whole `contentItem: RowLayout { … }` block (lines ~257–366)
  with the two-line version below. The chevron `Item` is unchanged; the name and
  all trailing state move into a stacked `ColumnLayout`.

```qml
                contentItem: RowLayout {
                    spacing: 8

                    // Expand/collapse chevron (unchanged).
                    Item {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        Layout.alignment: Qt.AlignVCenter
                        Label {
                            anchors.centerIn: parent
                            visible: row.hasChildren
                            text: row.expanded ? "▾" : "▸"
                            color: theme.textSecondary
                            font.pixelSize: 10
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: row.hasChildren
                            onClicked: repoTree.toggleExpanded(row.row)
                        }
                    }

                    // Name (line 1) + branch/sync (line 2, repos only).
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        // LINE 1 — name + trailing state.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: model.display
                                color: (model.missing || row.uninit) ? theme.textMuted : theme.textPrimary
                                font.pixelSize: row.isSub ? 13 : 14
                                font.weight: row.activeRepo ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            // Submodule: pinned short OID (mono) — hidden when uninitialised.
                            Label {
                                visible: row.isSub && !row.uninit
                                text: model.shortOid
                                color: theme.textMuted
                                font.family: "monospace"
                                font.pixelSize: 11
                            }
                            // Submodule: status dot (dirty amber / clean green @0.55).
                            Rectangle {
                                visible: row.isSub && !row.uninit
                                implicitWidth: 7
                                implicitHeight: 7
                                radius: 3.5
                                color: model.status === 1 ? theme.stateModified : theme.stateAdded
                                opacity: model.status === 1 ? 1.0 : 0.55
                            }
                            // Submodule: inline initialise affordance (uninitialised only).
                            AppButton {
                                variant: "primary"
                                compact: true
                                Layout.alignment: Qt.AlignVCenter
                                visible: row.uninit && !model.submoduleBusy
                                text: "Init"
                                onClicked: { if (projectController) projectController.initSubmodule(model.ownerRepoPath, model.repoPath) }
                            }
                            // Spinner while an op runs on this row.
                            BusyIndicator {
                                running: model.submoduleBusy === true
                                visible: running
                                implicitWidth: 14
                                implicitHeight: 14
                            }

                            // Repository: missing-on-disk warning.
                            Label {
                                visible: !row.isSub && model.missing === true
                                text: "⚠"
                                color: theme.stateModified
                            }

                            // Repository: dirty/clean badge (amber dot + count, or a subtle check).
                            RowLayout {
                                visible: !row.isSub && !model.missing
                                spacing: 4
                                Rectangle {
                                    visible: model.dirtyCount > 0
                                    implicitWidth: 7
                                    implicitHeight: 7
                                    radius: 3.5
                                    color: theme.stateModified
                                }
                                Label {
                                    visible: model.dirtyCount > 0
                                    text: model.dirtyCount
                                    color: theme.textSecondary
                                    font.pixelSize: 11
                                }
                                Label {
                                    visible: model.dirtyCount === 0
                                    text: "✓"
                                    color: theme.stateAdded
                                    opacity: 0.6
                                    font.pixelSize: 11
                                }
                            }

                            // Fetch status: spinner while running, then a result glyph.
                            // fetchState: 0 Idle, 1 Running, 2 UpToDate, 3 Updated, 4 Failed.
                            BusyIndicator {
                                running: model.fetchState === 1
                                visible: running
                                implicitWidth: 14
                                implicitHeight: 14
                            }
                            Label {
                                visible: model.fetchState === 3 && model.behind > 0
                                text: "↓" + model.behind
                                color: theme.accent
                                font.pixelSize: 11
                            }
                            Label {
                                visible: model.fetchState === 2
                                text: "✓"
                                color: theme.textMuted
                                font.pixelSize: 11
                            }
                            Label {
                                id: fetchFailedLabel
                                visible: model.fetchState === 4
                                text: "!"
                                color: theme.stateDeleted
                                font.pixelSize: 11
                                ToolTip.text: model.fetchError
                                ToolTip.visible: fetchFailHover.hovered
                                HoverHandler { id: fetchFailHover }
                            }
                        }

                        // LINE 2 — branch + ahead/behind (repositories only).
                        RowLayout {
                            visible: !row.isSub && !model.missing
                            spacing: 6

                            // Branch glyph (hidden when detached).
                            Label {
                                visible: !model.detached
                                text: "⎇"   // ⎇
                                color: theme.textSecondary
                                font.pixelSize: 11
                            }
                            Label {
                                text: model.detached ? "detached" : model.branch
                                color: theme.textSecondary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.maximumWidth: 180
                            }
                            // Detached: the short commit id after the label.
                            Label {
                                visible: model.detached
                                text: model.shortOid
                                color: theme.textMuted
                                font.family: "monospace"
                                font.pixelSize: 11
                            }
                            // Ahead / behind (only with an upstream, non-detached).
                            Label {
                                visible: !model.detached && model.hasUpstream && model.ahead > 0
                                text: "↑" + model.ahead   // ↑N
                                color: theme.stateAdded
                                font.pixelSize: 11
                            }
                            Label {
                                visible: !model.detached && model.hasUpstream && model.behind > 0
                                text: "↓" + model.behind   // ↓N
                                color: theme.stateIncoming
                                font.pixelSize: 11
                            }
                            // No upstream → dash (non-detached).
                            Label {
                                visible: !model.detached && !model.hasUpstream
                                text: "—"   // —
                                color: theme.textMuted
                                font.pixelSize: 11
                            }
                            Item { Layout.fillWidth: true }   // left-align the status row
                        }
                    }
                }
```

- [ ] **Step 4: Run the test — verify pass (no QML load regression)**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: `sidebar_repo_row_exposes_branch_and_dirty` PASS and no QML warnings
  about unknown roles in the run output.

- [ ] **Step 5: Visual check in the running app** — the delegate's layout can't be
  fully asserted headless. Launch the app (`/run` or the project's launch skill)
  and confirm against the spec mockup: a repo row shows name on line 1 with a
  dirty `● N` badge (or `✓` when clean), and line 2 shows `⎇ <branch>` with
  `↑N ↓N` (or `—` when no upstream, `detached <oid>` when detached).

- [ ] **Step 6: Commit**

```bash
git add ui/qml/Sidebar.qml tests/ui/test_qml_sync.cpp
git commit -m "feat(ui): two-line sidebar repo entry (branch + sync + dirty)"
```

---

## Task 5: Close-out — docs true, wish closed

- [ ] **Step 1: Flip the design doc status** — in
  `docs/spec/product/2026-07-20-repo-tree-entry-redesign-design.md`, set
  `**Status:** shipped` and add a `**Shipped:** 2026-07-20` line.

- [ ] **Step 2: Fill this plan's Outcome** (below) and set the header
  `**Status**` to `done`.

- [ ] **Step 3: Add the index row** — append to `docs/plans/index.md`:
  ```
  | [Plan 33 — Repo tree entry redesign (branch + sync + dirty)](2026-07-20-plan33-repo-tree-entry.md) | 2026-07-20 | done | product · ui |
  ```

- [ ] **Step 4: Record the decision** (if not already) in `docs/decisions.md`:
  the sidebar shows the current branch (not a hash) for repos, dirty = count, no
  upstream = dash, detached = `detached <oid>`; state refreshes on the existing
  fleet-poll path.

- [ ] **Step 5: Commit**

```bash
git add docs/
git commit -m "docs: close out Plan 33 — repo tree entry redesign"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: <which `spec/` sections now describe this>.
> - Code: <the main files/types that resulted>.
