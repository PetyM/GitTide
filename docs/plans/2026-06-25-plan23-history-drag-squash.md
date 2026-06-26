# Plan 23 — Whole-row long-press drag + drag-to-squash in history

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: use superpowers:subagent-driven-development or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-06-25 |
| **Status** | `done` |
| **Spec** | [spec/product/2026-06-25-history-drag-squash-design.md](../spec/product/2026-06-25-history-drag-squash-design.md); extends [spec/product/rebase-interactive.md](../spec/product/rebase-interactive.md) §3.2, **D36** |
| **Depends on** | Plan 20 (interactive-rebase engine), Plan 22 (history-editing UX: `reorderCommits`, `ReorderConfirmDialog`, `reorderableRunLength`) |

**Goal:** Make the whole commit row in the history view a drag source behind a
250 ms press-and-hold (a quick click still selects), and add drag-to-squash via a
three-band drop zone — top/bottom thirds reorder, the middle third squashes the
dragged commit into the target through the existing interactive-rebase engine.

**Architecture:** No `core/` change — the manual rebase engine (D34) already
squashes and pauses for the combined message. One new ViewModel invokable,
`RepoViewModel::squashCommitInto(int fromRow, int toRow)`, a sibling of
`reorderCommits` that builds a `pick…/squash` plan folding the dragged commit into
the target. The `HistoryPane.qml` delegate gains a whole-row hold-to-drag gesture,
a pure JS band-resolver (`dropZoneAt`), a release router (`performDrop`), and live
reorder/squash drop indicators. Squash routes to `squashCommitInto`; reorder keeps
the existing `ReorderConfirmDialog` → `reorderCommits` path.

**Tech stack:** Qt 6 Quick/QML (`DragHandler`, `TapHandler`, `Timer`), C++23
ViewModel on `QCoro`, libgit2 (untouched here), Catch2 + QtTest headless runner.

## Global constraints

- **No Qt in `core/`** — this plan touches no `core/` file at all.
- **Colour from theme tokens only** — every new visual state (lifted row, insertion
  line, squash fill/badge) uses `theme.*` tokens, never a hex literal
  ([engineering.md](../spec/engineering/engineering.md)).
- **Affordance, not colour-only (D19)** — the squash vs. reorder drop states differ
  by shape (insertion line vs. filled row + "◆ squash" badge), not colour alone.
- **TDD** — failing test first for each task. New `ui/` sources are already
  registered; no new files in `ui/CMakeLists.txt` (only `HistoryPane.qml` /
  `repoviewmodel.*` are modified). Tests go in the existing
  `tests/ui/test_repoviewmodel_rebase.cpp` and `tests/ui/test_qml_history.cpp`
  (already in `tests/CMakeLists.txt`).
- **Must keep passing:** `reorder_commits_rewrites_history_order`,
  `reorderable_run_counts_linear_run` (Plan 22), and the `reorderGrip` objectName
  (the grip stays as a discoverability hint).
- **Gating unchanged:** only rows with `index < reorderableRunLength` are draggable;
  drop targets must also satisfy `toRow < reorderableRunLength` and `toRow != from`.

---

## Task 1: `RepoViewModel::squashCommitInto` — fold dragged commit into target

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` (declare the invokable, next to `reorderCommits`)
- Modify: `ui/src/repoviewmodel.cpp` (implement, after `reorderCommits` ~line 434)
- Test: `tests/ui/test_repoviewmodel_rebase.cpp` (new slot + register in the run list)

**Interfaces:**
- Produces: `Q_INVOKABLE void squashCommitInto(int fromRow, int toRow);`
  — folds the commit at `fromRow` (newest-first run index) into the commit at
  `toRow`; the combined commit keeps the target's slot. Drives the engine via
  `m_controller->startInteractiveRebase(base, actions, oids)`; the engine pauses on
  the combined message (surfaced through the existing `rebasePauseReason ==
  "message"` / `RewordDialog` flow). No-op if either row is outside
  `[0, reorderableRunLength)`, if `fromRow == toRow`, or if `reorderableRunLength < 2`.
- Consumes: existing `m_reorderableRunLength`, `m_lastLayout.rows`,
  `m_controller->startInteractiveRebase(...)` (same as `reorderCommits`),
  `continueRebase(QString)` (existing, for the test to finish the message pause).

- [x] **Step 1: Write the failing test.** Add this slot to the `private slots:`
  section of `tests/ui/test_repoviewmodel_rebase.cpp`, and add a forward call/registration
  the same way `reorder_commits_rewrites_history_order` is declared (it is an
  auto-run QtTest slot — no extra registration needed beyond being a `private slot`):

```cpp
/// squashCommitInto folds the dragged commit into the target: squashing the
/// newest commit (row 0) into the second-newest (row 1) yields one combined
/// commit at the target's slot, carrying both commits' changes.
void squash_commit_into_folds_dragged_into_target()
{
    const auto dir = repo_view_model_rebase_test::makeLinearRepo(3); // newest-first: c2, c1, c0
    QCOMPARE(repo_view_model_rebase_test::headMessage(dir), std::string("c2\n"));

    RepoViewModel vm;
    vm.open(QString::fromStdString(dir.generic_string()));
    QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

    // Drag c2 (row 0) onto c1 (row 1) → squash c2 into c1.
    QMetaObject::invokeMethod(&vm, "squashCommitInto", Q_ARG(int, 0), Q_ARG(int, 1));

    // The engine pauses for the combined message (squash), prefilled target-then-dragged.
    QTRY_COMPARE_WITH_TIMEOUT(vm.property("rebasePauseReason").toString(), QStringLiteral("message"), 5000);
    QVERIFY(vm.property("rebaseMessagePrefill").toString().contains(QStringLiteral("c1")));
    QVERIFY(vm.property("rebaseMessagePrefill").toString().contains(QStringLiteral("c2")));

    // Confirm the combined message → rebase completes.
    QMetaObject::invokeMethod(&vm, "continueRebase", Q_ARG(QString, QStringLiteral("c1+c2\n")));
    QTRY_VERIFY_WITH_TIMEOUT(!vm.rebaseInProgress(), 5000);

    // HEAD is the combined commit (target's identity, new message), and it carries
    // the dragged commit's file (f2.txt from c2) folded in.
    QTRY_COMPARE_WITH_TIMEOUT(repo_view_model_rebase_test::headMessage(dir), std::string("c1+c2\n"), 3000);
    QVERIFY(std::filesystem::exists(dir / "f2.txt"));
    QVERIFY(std::filesystem::exists(dir / "f1.txt"));

    std::filesystem::remove_all(dir);
}
```

- [x] **Step 2: Run it to verify it fails.**

Run: `ctest --test-dir build -R repoviewmodel_rebase --output-on-failure`
Expected: FAIL — `squashCommitInto` is not a registered invokable
(`QMetaObject::invokeMethod` returns false / no such method), so the pause never
arrives and the `QTRY_COMPARE` on `rebasePauseReason` times out.

- [x] **Step 3: Declare the invokable.** In `ui/include/gittide/ui/repoviewmodel.hpp`,
  directly below the existing `reorderCommits` declaration, add:

```cpp
    /// Squash the commit at @p fromRow into the commit at @p toRow (both
    /// newest-first indices into the reorderable run). The dragged commit folds
    /// into the target; the combined commit keeps the target's slot. Drives the
    /// interactive engine, which pauses on the combined message (RewordDialog).
    /// No-op unless both rows are in [0, reorderableRunLength) and differ.
    Q_INVOKABLE void squashCommitInto(int fromRow, int toRow);
```

- [x] **Step 4: Implement.** In `ui/src/repoviewmodel.cpp`, immediately after
  `reorderCommits` (after ~line 434), add:

```cpp
void RepoViewModel::squashCommitInto(int fromRow, int toRow)
{
    const int n = m_reorderableRunLength;
    if (n < 2 || fromRow == toRow)
        return;
    if (fromRow < 0 || fromRow >= n || toRow < 0 || toRow >= n)
        return;

    // Build the run newest-first (index 0 == HEAD), same as reorderCommits.
    QList<QString> run;
    run.reserve(n);
    for (int i = 0; i < n; ++i)
        run << QString::fromStdString(m_lastLayout.rows[i].commit.oid);

    // Detach onto the parent of the run's (original) oldest commit.
    const auto& deepest = m_lastLayout.rows[n - 1].commit;
    if (deepest.parents.empty())
        return; // defensive: run members always have one parent
    const QString base = QString::fromStdString(deepest.parents[0]);

    const QString dragged = run[fromRow];
    const QString target  = run[toRow];

    // Place the dragged commit immediately *newer-adjacent* to the target, then
    // mark it squash. The engine reads the plan oldest-first and folds a squash
    // entry into the preceding kept commit; placing dragged directly after target
    // in oldest-first order folds dragged into target. In this newest-first list,
    // "directly after target oldest-first" == "directly before target here", i.e.
    // insert at the target's index so dragged sits one slot newer than target.
    run.removeAt(fromRow);
    const int t = run.indexOf(target);
    run.insert(t, dragged);

    // Hand the engine the plan oldest-first: all picks, except the dragged commit
    // is squash.
    QStringList oids, actions;
    for (auto it = run.rbegin(); it != run.rend(); ++it)
    {
        oids << *it;
        actions << (*it == dragged ? QStringLiteral("squash") : QStringLiteral("pick"));
    }
    QCoro::connect(m_controller->startInteractiveRebase(base, actions, oids), this, [] {});
}
```

- [x] **Step 5: Run the test to verify it passes.**

Run: `ctest --test-dir build -R repoviewmodel_rebase --output-on-failure`
Expected: PASS — `squash_commit_into_folds_dragged_into_target` green, plus the
existing `reorder_commits_*` and `reorderable_run_*` slots still pass.

- [x] **Step 6: Commit.**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp tests/ui/test_repoviewmodel_rebase.cpp
git commit -m "feat(ui): squashCommitInto folds a dragged commit into a target via the rebase engine"
```

---

## Task 2: `dropZoneAt` — pure band resolver on the history pane

**Files:**
- Modify: `ui/qml/HistoryPane.qml` (add a JS function on the pane root, `historyPane`)
- Test: `tests/ui/test_qml_history.cpp` (new slot)

**Interfaces:**
- Produces: a JS function on the `historyPane` root item:
  `function dropZoneAt(localY, rowHeight)` returning one of the strings
  `"above"`, `"squash"`, `"below"`. Top third → `"above"`, middle third →
  `"squash"`, bottom third → `"below"`. Clamps out-of-range `localY` (≤0 →
  `"above"`, ≥`rowHeight` → `"below"`).

- [x] **Step 1: Write the failing test.** Add this slot to `TestQmlHistory` in
  `tests/ui/test_qml_history.cpp` (it follows the `history_list_binds_to_history_model`
  pattern: load `Main.qml`, `findChild` by objectName, then `invokeMethod`):

```cpp
void drop_zone_resolves_three_bands()
{
    ThemeManager mgr;
    mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;
    RepoViewModel vm;

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QCOMPARE(engine.rootObjects().size(), 1);

    QObject* pane = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyPane"));
    QVERIFY(pane != nullptr);

    QString zone;
    const qreal h = 48.0;
    // Top third → "above".
    QMetaObject::invokeMethod(pane, "dropZoneAt", Q_RETURN_ARG(QString, zone),
                              Q_ARG(QVariant, 4.0), Q_ARG(QVariant, h));
    QCOMPARE(zone, QStringLiteral("above"));
    // Middle third → "squash".
    QMetaObject::invokeMethod(pane, "dropZoneAt", Q_RETURN_ARG(QString, zone),
                              Q_ARG(QVariant, 24.0), Q_ARG(QVariant, h));
    QCOMPARE(zone, QStringLiteral("squash"));
    // Bottom third → "below".
    QMetaObject::invokeMethod(pane, "dropZoneAt", Q_RETURN_ARG(QString, zone),
                              Q_ARG(QVariant, 44.0), Q_ARG(QVariant, h));
    QCOMPARE(zone, QStringLiteral("below"));
}
```

> Note: the pane root must carry `objectName: "historyPane"`. If it does not yet,
> add it in Step 3 (the root item of `HistoryPane.qml`).

- [x] **Step 2: Run it to verify it fails.**

Run: `ctest --test-dir build -R qml_history --output-on-failure`
Expected: FAIL — either `findChild("historyPane")` returns null, or
`invokeMethod("dropZoneAt", …)` fails (no such method), so the first `QCOMPARE`
fails.

- [x] **Step 3: Implement.** In `ui/qml/HistoryPane.qml`, ensure the root item has
  `objectName: "historyPane"`, then add this function to the root item's body
  (alongside its other properties/functions):

```qml
// Resolve a drop position within a target row into one of three bands:
// top third inserts above, bottom third below (reorder), middle third squashes
// into the target. Pure — unit-tested in test_qml_history.cpp.
function dropZoneAt(localY, rowHeight) {
    if (localY <= rowHeight / 3)
        return "above"
    if (localY >= rowHeight * 2 / 3)
        return "below"
    return "squash"
}
```

- [x] **Step 4: Run the test to verify it passes.**

Run: `ctest --test-dir build -R qml_history --output-on-failure`
Expected: PASS — `drop_zone_resolves_three_bands` green.

- [x] **Step 5: Commit.**

```bash
git add ui/qml/HistoryPane.qml tests/ui/test_qml_history.cpp
git commit -m "feat(ui): dropZoneAt resolves history drop into reorder/squash bands"
```

---

## Task 3: `performDrop` router — wire bands to reorder vs. squash

**Files:**
- Modify: `ui/qml/HistoryPane.qml` (add `performDrop` on the `historyPane` root)
- Test: `tests/ui/test_qml_history.cpp` (two new slots)

**Interfaces:**
- Consumes: `dropZoneAt` (Task 2), `RepoViewModel::squashCommitInto` (Task 1),
  the existing `reorderConfirm` dialog (objectName `reorderConfirmDialog`, with
  `openFor(from, to)`), and `repoVm.reorderableRunLength`.
- Produces: a JS function on `historyPane`:
  `function performDrop(fromIndex, toIndex, zone)` —
  - `zone === "squash"` → `repoVm.squashCommitInto(fromIndex, toIndex)`;
  - `zone === "above"` or `"below"` → `reorderConfirm.openFor(fromIndex, toIndex)`;
  - any drop where `toIndex` is out of `[0, reorderableRunLength)`, equal to
    `fromIndex`, or `< 0` → no-op.

- [x] **Step 1: Write the failing tests.** Add these two slots to `TestQmlHistory`
  in `tests/ui/test_qml_history.cpp`. They use a real linear repo (so the run has
  ≥2 commits) loaded into the `RepoViewModel` behind `Main.qml`:

```cpp
void perform_drop_squash_routes_to_squash_commit_into()
{
    const auto dir = repo_view_model_rebase_test::makeLinearRepo(3); // run length 2

    ThemeManager mgr; mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;
    RepoViewModel vm;
    QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
    vm.open(QString::fromStdString(dir.generic_string()));
    QVERIFY(historySpy.wait(3000));
    QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QObject* pane = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyPane"));
    QVERIFY(pane != nullptr);

    // Squash row 0 into row 1 → engine pauses for the combined message.
    QMetaObject::invokeMethod(pane, "performDrop",
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("squash")));
    QTRY_COMPARE_WITH_TIMEOUT(vm.property("rebasePauseReason").toString(), QStringLiteral("message"), 5000);

    std::filesystem::remove_all(dir);
}

void perform_drop_reorder_opens_confirm_dialog()
{
    const auto dir = repo_view_model_rebase_test::makeLinearRepo(3);

    ThemeManager mgr; mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;
    RepoViewModel vm;
    QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
    vm.open(QString::fromStdString(dir.generic_string()));
    QVERIFY(historySpy.wait(3000));
    QTRY_COMPARE_WITH_TIMEOUT(vm.property("reorderableRunLength").toInt(), 2, 3000);

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QObject* root = engine.rootObjects().first();
    QObject* pane = root->findChild<QObject*>(QStringLiteral("historyPane"));
    QVERIFY(pane != nullptr);

    // Reorder (below) row 0 onto row 1 → the confirm dialog opens (no engine drive yet).
    QMetaObject::invokeMethod(pane, "performDrop",
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, 1), Q_ARG(QVariant, QStringLiteral("below")));
    QObject* dlg = root->findChild<QObject*>(QStringLiteral("reorderConfirmDialog"));
    QVERIFY(dlg != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(dlg->property("visible").toBool(), 3000);
    // Reorder must NOT have driven the engine directly.
    QCOMPARE(vm.property("rebaseInProgress").toBool(), false);

    std::filesystem::remove_all(dir);
}
```

> These reference `repo_view_model_rebase_test::makeLinearRepo` — add
> `#include` for that helper, or duplicate the small helper into the history test's
> anonymous namespace if cross-including is awkward (mirror the existing
> `qml_history_test` helpers already in the file).

- [x] **Step 2: Run them to verify they fail.**

Run: `ctest --test-dir build -R qml_history --output-on-failure`
Expected: FAIL — `performDrop` does not exist, so both `invokeMethod` calls fail
and the subsequent `QTRY_*` assertions time out / fail.

- [x] **Step 3: Implement.** In `ui/qml/HistoryPane.qml`, add to the `historyPane`
  root body, next to `dropZoneAt`:

```qml
// Route a released drag: squash folds the dragged commit into the target;
// reorder (above/below) goes through the existing confirmation dialog. Both
// source and target must lie in the reorderable run and differ.
function performDrop(fromIndex, toIndex, zone) {
    if (!repoVm)
        return
    if (toIndex < 0 || toIndex >= repoVm.reorderableRunLength || toIndex === fromIndex)
        return
    if (zone === "squash")
        repoVm.squashCommitInto(fromIndex, toIndex)
    else
        reorderConfirm.openFor(fromIndex, toIndex)
}
```

- [x] **Step 4: Run the tests to verify they pass.**

Run: `ctest --test-dir build -R qml_history --output-on-failure`
Expected: PASS — both new slots green; existing history slots still pass.

- [x] **Step 5: Commit.**

```bash
git add ui/qml/HistoryPane.qml tests/ui/test_qml_history.cpp
git commit -m "feat(ui): performDrop routes history drops to squash or reorder-confirm"
```

---

## Task 4: Whole-row hold-to-drag gesture + lifted state

**Files:**
- Modify: `ui/qml/HistoryPane.qml` (delegate: add a row-wide drag gesture armed by a
  hold timer; lifted visual; keep the `MouseArea` click-select intact)

**Interfaces:**
- Consumes: `dropZoneAt` + `performDrop` (Tasks 2–3), `historyList.indexAt`,
  `historyList.contentItem`, `repoVm.reorderableRunLength`.
- Produces: a delegate that arms a vertical drag after a 250 ms press-and-hold
  anywhere on a run row, lifts the row (opacity + accent border via theme tokens)
  while dragging, and on release resolves the target row + band and calls
  `historyPane.performDrop(index, targetIndex, zone)`. A press shorter than the
  hold threshold falls through to the existing click-select.

This task is gesture wiring; its behavioural guarantees (squash route, reorder
route) are already covered by Task 3's `performDrop` tests. There is no reliable
headless input-simulation path for the 250 ms hold in the QML test harness (the
engine loads no visible window), so this task is verified by (a) the Task 3 routing
tests still passing through the new release handler, and (b) a manual smoke check.
Do **not** invent a flaky input-simulation test; keep the seam (`performDrop`)
tested instead.

- [x] **Step 1: Add the hold-armed drag to the delegate.** In the delegate
  `Rectangle` of `HistoryPane.qml`, add (inside the delegate, not replacing the
  existing `MouseArea` — the `MouseArea` keeps handling click/right-click select):

```qml
// Whole-row drag, armed by a press-and-hold so a quick click still selects.
// Only rows in the reorderable run participate.
property bool dragArmed: false

DragHandler {
    id: rowDrag
    enabled: !!repoVm && index < repoVm.reorderableRunLength
    target: null                 // we move nothing; we read the centroid on release
    xAxis.enabled: false         // vertical only, matching the grip drag
    // Arm only after the hold timer fires (set by holdTimer below). Until armed,
    // a drag does nothing, so a quick press-drag-release won't reorder.
    onActiveChanged: {
        if (active) {
            holdTimer.restart()
        } else {
            holdTimer.stop()
            if (dragArmed && repoVm) {
                var p = mapToItem(historyList.contentItem,
                                  rowDrag.centroid.position.x,
                                  rowDrag.centroid.position.y)
                var to = historyList.indexAt(p.x, p.y)
                if (to >= 0) {
                    // Local Y within the target row resolves the band.
                    var rowTop = to * 48              // fixed 48 px row height
                    var localY = p.y - rowTop
                    var zone = historyPane.dropZoneAt(localY, 48)
                    historyPane.performDrop(index, to, zone)
                }
            }
            dragArmed = false
        }
    }
}

Timer {
    id: holdTimer
    interval: 250
    repeat: false
    onTriggered: dragArmed = true   // row "lifts" via the binding below
}
```

> If `48` (row height) is already a named constant/property in the file, use it
> instead of the literal in both places; otherwise the delegate's `height: 48`
> is the single source — keep the two `48`s consistent with it.

- [x] **Step 2: Add the lifted visual.** Bind the delegate root's appearance to
  `dragArmed` using theme tokens (extend the existing delegate `Rectangle`):

```qml
opacity: dragArmed ? 0.7 : 1.0
border.width: dragArmed ? 1 : 0
border.color: theme.accent
```

- [x] **Step 3: Update the grip tooltip.** Change the existing `reorderGrip`
  `ToolTip.text` from `"Drag to reorder"` to `"Drag to reorder or squash"` (the
  grip stays as the discoverability hint; the whole row is now draggable).

- [x] **Step 4: Build and run the full UI suite.**

Run: `cmake --build build --parallel && ctest --test-dir build -R qml_history --output-on-failure`
Expected: PASS — Task 3's `performDrop` routing tests still green (the release
handler now calls into them); no regressions in the history slots.

- [x] **Step 5: Manual smoke check (record the result).** Run the app, open a repo
  with a linear history. Verify: (a) a quick click selects a commit; (b) pressing
  and holding ~¼ s on a row lifts it and lets you drag vertically; (c) dropping on
  the top/bottom third of another run row opens the reorder confirmation; (d)
  dropping on the middle third opens the combined-message editor (squash). Note the
  outcome in the PR / plan Outcome.

- [x] **Step 6: Commit.**

```bash
git add ui/qml/HistoryPane.qml
git commit -m "feat(ui): whole-row hold-to-drag in history with reorder/squash drop bands"
```

---

## Task 5: Live drop indicators — reorder line vs. squash highlight

**Files:**
- Modify: `ui/qml/HistoryPane.qml` (delegate: a hovered-band overlay driven during
  the active drag)

**Interfaces:**
- Consumes: the active `rowDrag` (Task 4), `dropZoneAt`, `historyList.indexAt`.
- Produces: per-row visual feedback while a drag is active — a 2 px `accent`
  insertion line at the row's top (band `"above"`) or bottom (band `"below"`) edge,
  or a `surfaceOverlay` fill + "◆ squash" badge (band `"squash"`) on the row under
  the cursor. Purely visual; no new routing.

This task is visual polish with no headless-testable assertion (it depends on live
pointer position during an active `DragHandler`). Verify by the manual smoke check
in Task 4 Step 5 — confirm the indicator distinguishes reorder from squash *before*
release. Keep all colours as `theme.*` tokens (D19: shape differs, not just colour).

- [x] **Step 1: Track the live hovered target + band.** On the `historyPane` root,
  add observable state updated as the active drag moves. Add a property and a small
  updater invoked from `rowDrag`'s `centroid` change (extend Task 4's `DragHandler`
  with an `onCentroidChanged` that, while `active && dragArmed`, computes the target
  index + zone exactly as the release handler does and stores them):

```qml
// On historyPane root:
property int dropTargetIndex: -1
property string dropTargetZone: ""

function updateDropTarget(globalPt) {
    var to = historyList.indexAt(globalPt.x, globalPt.y)
    if (to < 0 || !repoVm || to >= repoVm.reorderableRunLength) {
        dropTargetIndex = -1; dropTargetZone = ""; return
    }
    var localY = globalPt.y - to * 48
    dropTargetIndex = to
    dropTargetZone = dropZoneAt(localY, 48)
}
```

In `rowDrag` (Task 4) add, and clear on release:

```qml
onCentroidChanged: {
    if (active && dragArmed) {
        var p = mapToItem(historyList.contentItem, rowDrag.centroid.position.x, rowDrag.centroid.position.y)
        historyPane.updateDropTarget(p)
    }
}
// in onActiveChanged, when !active, after performDrop:
historyPane.dropTargetIndex = -1
historyPane.dropTargetZone = ""
```

- [x] **Step 2: Draw the indicators.** In the delegate, add overlay items keyed to
  whether this row is the current drop target:

```qml
// Reorder insertion line (above/below) on the hovered target row.
Rectangle {
    visible: historyPane.dropTargetIndex === index
             && (historyPane.dropTargetZone === "above" || historyPane.dropTargetZone === "below")
    width: parent.width
    height: 2
    color: theme.accent
    y: historyPane.dropTargetZone === "above" ? 0 : parent.height - height
}
// Squash highlight + badge on the hovered target row.
Rectangle {
    visible: historyPane.dropTargetIndex === index && historyPane.dropTargetZone === "squash"
    anchors.fill: parent
    color: theme.surfaceOverlay
    Label {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: "◆ squash"
        color: theme.accent
        font.pixelSize: 11
    }
}
```

- [x] **Step 3: Build + run the suite (no regressions).**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: PASS — full suite green; the indicators are visual-only and break no test.

- [x] **Step 4: Manual smoke check.** Repeat Task 4 Step 5 and confirm the insertion
  line shows for top/bottom bands and the squash fill + "◆ squash" badge shows for
  the middle band, updating live as you move before release.

- [x] **Step 5: Commit.**

```bash
git add ui/qml/HistoryPane.qml
git commit -m "feat(ui): live reorder-line / squash-highlight drop indicators in history"
```

---

## Task 6: Documentation — spec, decision, plan index

**Files:**
- Modify: `docs/spec/product/rebase-interactive.md` (§3.2)
- Modify: `docs/decisions.md` (new **D38**)
- Modify: `docs/plans/index.md` (add the Plan 23 row)
- Modify: this plan's **Outcome** section

- [x] **Step 1: Update `rebase-interactive.md` §3.2.** Replace the "Reorder directly
  in the history view" paragraph's grip-only description with the whole-row
  hold-to-drag model: the entire row in the reorderable run is a drag source behind
  a 250 ms press-and-hold (a quick click still selects); a three-band drop zone on
  the target row routes top/bottom thirds to reorder (existing `ReorderConfirmDialog`
  → `reorderCommits`) and the middle third to **squash** via the new
  `RepoViewModel::squashCommitInto(fromRow, toRow)`, which folds the dragged commit
  into the target through the engine and pauses on the combined-message
  `RewordDialog`. Note the `⠿` grip remains as a discoverability affordance.

- [x] **Step 2: Add decision D38** to `docs/decisions.md`:

```markdown
### D38 — Whole-row long-press drag + drop-zone disambiguation in history

History drag was grip-only (a 16 px `⠿` target) and easy to miss; squash had no
direct-manipulation path. The whole commit row in the reorderable run is now a drag
source armed by a 250 ms press-and-hold (a quick click still selects — no
accidental reorder). A three-band drop zone on the target row disambiguates: top/
bottom thirds reorder (insert above/below, existing confirmation), the middle third
squashes the dragged commit into the target.

- **Hold-to-drag over grip-only:** the grip was undiscoverable; a whole-row hold is
  the platform-standard reorder gesture and keeps click-select intact. The grip
  stays as a hint.
- **Drop-zone thirds over a modifier key:** Shift/Ctrl-to-squash is invisible; live
  band indicators (insertion line vs. squash fill + "◆ squash" badge) show the
  outcome before release (affordance, not colour-only — D19).
- **Drag-squash opens the combined-message editor (no fixup via drag):** the
  `RewordDialog` pause is the confirmation gate, mirroring menu-driven squash;
  message-discarding fixup stays in the todo editor. Direction is fixed — the
  dragged commit folds into the target, which keeps its slot.

No core change: the manual engine (D34) already squashes and pauses for the message.
Extends D36 (history drag-to-reorder).
```

- [x] **Step 3: Add the Plan 23 row** to `docs/plans/index.md` (mirror the Plan 22
  row's format; link to this file; Status `done`).

- [x] **Step 4: Fill this plan's Outcome** (below) and flip **Status** to `done`.

- [x] **Step 5: Commit.**

```bash
git add docs/spec/product/rebase-interactive.md docs/decisions.md docs/plans/index.md docs/plans/2026-06-25-plan23-history-drag-squash.md
git commit -m "docs: record whole-row drag + drag-to-squash (D38, Plan 23, spec §3.2)"
```

---

## Outcome

- **Shipped:** whole-row hold-to-drag (250 ms press-and-hold arms a `DragHandler`;
  quick click still selects) + drag-to-squash via three-band drop zone in the history
  view. Top/bottom third of the target row reorders (existing `ReorderConfirmDialog`
  → `reorderCommits`); middle third squashes the dragged commit into the target
  (`squashCommitInto` → combined-message `RewordDialog`). The `⠿` grip remains as a
  discoverability hint. Live drop indicators: 2 px accent insertion line for
  reorder, `surfaceOverlay` fill + "◆ squash" badge for squash.
- **Spec updated:** `rebase-interactive.md` §3.2 (whole-row drag + three-band drop
  zone); decision D38.
- **Code:**
  - `RepoViewModel::squashCommitInto(int fromRow, int toRow)` — new invokable,
    sibling of `reorderCommits`; builds a `pick…/squash` plan and drives the engine.
  - `HistoryPane.qml` — `QtObject { id: dropLogic; objectName: "historyPane" }`
    anchor holding `dropZoneAt`, `performDrop`, `updateDropTarget`, and live
    `dropTargetIndex`/`dropTargetZone` state; delegate-level `DragHandler#rowDrag`
    armed by `Timer#holdTimer`; lifted-row visual (opacity 0.7 + accent border);
    reorder-line and squash-highlight overlay items; updated grip tooltip.
  - Crash hardening: `open()` and `close()` in `repoviewmodel.cpp` now call
    `updateReorderableRun()` after clearing `m_lastLayout = {}`, preventing a stale
    `reorderableRunLength` from indexing an empty rows vector (SIGSEGV).
- **Verification:** `gittide_ui_tests` 100% green after each task (incl. new slots
  `squash_commit_into_folds_dragged_into_target`, `drop_zone_resolves_three_bands`,
  `perform_drop_*`, `opening_new_repo_resets_reorderable_run`). The drag/drop logic
  seam (`performDrop` → squash/reorder routing) is covered headlessly; the live
  hold-to-drag gesture and indicator rendering have **no headless test** (no reliable
  input simulation in the QML runner) and were verified by code review, not by an
  interactive run — a manual visual smoke check in the running app is still
  recommended before release.
- **Follow-ups (all resolved 2026-06-26):** (1) ✅ reorder now honours the band —
  `reorderCommits(fromRow, toRow, band)` inserts the dragged commit on the target's
  newer ("above") or older ("below") side; the band threads QML →
  `ReorderConfirmDialog.openFor(from, to, band)` → VM. Covered by
  `reorder_commits_above_band_inserts_newer_side` /
  `reorder_commits_below_band_inserts_older_side`. Design-doc §2 (distinct
  above/below) is now matched by code. (2) ✅ squash test asserts prefill order
  (`c1` before `c2`). (3) ✅ `rowHeight` is one source (`dropLogic.rowHeight`); the
  delegate references it. (4) ✅ `updateDropTarget` param renamed `contentPt` with a
  coordinate-space comment.
