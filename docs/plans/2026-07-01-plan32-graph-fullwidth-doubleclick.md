# Plan 32 — Graph tab full-width + double-click hand-off to History

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-01 |
| **Status** | `planned` |
| **Spec** | [`spec/product/product.md`](../spec/product/product.md#graph-tab), decision D47 in [`decisions.md`](../decisions.md) |
| **Depends on** | Plan 25 (History redesign + full git-graph tab) |

**Goal:** Fix mouse-wheel scrolling everywhere (regression test for an
already-applied fix), then remove the Graph tab's inline commit-detail panel so
the graph fills the pane, and make double-clicking a graph row switch to the
History tab showing that commit's diff.

**Architecture:** `WheelScroller.qml`'s `view` property now resolves through
`parent.parent` (Flickable's default-property redirection sends declared
children into `contentItem`, not the Flickable itself) — this is already fixed
in the working tree; this plan adds the regression test. `GraphPane.qml` drops
its `CommitDetail` + divider and widens its `ListView` to `Layout.fillWidth`.
A new `commitActivated()` signal on `GraphPane`, fired from a
`TapHandler { gesture: TapHandler.DoubleTap }` per row, lets `WorkingPane.qml`
switch `tabs.currentIndex` to History — no oid look-up needed since
`selectedCommit`/`commitDiff` are already shared global state on `RepoViewModel`.

**Tech stack:** Qt Quick/QML (Pointer Handlers), QtTest headless UI tests
(`QT_QPA_PLATFORM=offscreen`).

## Global constraints

- No Qt in `core/` — not touched by this plan (QML/UI only).
- New UI tests need **two edits**: add to `gittide_ui_test_sources` in
  `tests/CMakeLists.txt`, and add both an `#include` and a `QTest::qExec` call in
  `tests/ui/main.cpp` — miss either and it silently registers zero tests.
- Zero new compiler warnings and zero Qt runtime warnings (a warning is a bug).
- Colour/tokens, `m_` members, lowercase file names, Allman braces — not
  relevant here (no new C++, no colour changes).

---

## Task 1: Regression test for the WheelScroller fix

The mouse-wheel-doesn't-scroll bug is already fixed in the working tree
(`ui/qml/WheelScroller.qml`) — root cause: `Flickable`'s default property
redirects QML children declared inside it (e.g. `ListView { WheelScroller {} }`)
into its internal `contentItem`, so a `WheelHandler` declared that way has
`parent` pointing at `contentItem` (a plain `Item`, no `contentY`), not the
`Flickable`/`ListView` itself. The current file already reads
`property Flickable view: parent ? parent.parent : null`. This task adds the
missing regression test so a future "simplification" back to `property
Flickable view: parent` fails loudly instead of silently breaking every
scrollable list in the app.

**Files:**
- Create: `tests/ui/test_qml_wheelscroller.cpp`
- Modify: `tests/CMakeLists.txt` (add to `gittide_ui_test_sources`)
- Modify: `tests/ui/main.cpp` (add `#include` + `QTest::qExec` call)

**Interfaces:**
- Consumes: `qrc:/qml/WheelScroller.qml` (`view` property, type `Flickable`).
- Produces: nothing new — this is a leaf regression test.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_qml_wheelscroller.cpp`:

```cpp
#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

class TestQmlWheelScroller : public QObject
{
    Q_OBJECT
private slots:

    // WheelScroller is always declared as a direct child of a ListView/Flickable.
    // Flickable's default property redirects declared children into its internal
    // contentItem, so WheelHandler.parent is that contentItem, not the Flickable —
    // `view` must skip past it via parent.parent, not bind to `parent` directly.
    void view_resolves_to_the_enclosing_flickable_not_its_content_item()
    {
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        comp.setData(R"QML(
            import QtQuick
            ListView {
                id: lv
                objectName: "lv"
                width: 200
                height: 200
                model: 20
                delegate: Item { height: 20 }
                WheelScroller { id: scroller; objectName: "scroller" }
            }
        )QML", QUrl(QStringLiteral("qrc:/qml/_test_wheelscroller_host.qml")));

        QObject* root = comp.create();
        QVERIFY2(root, qPrintable(comp.errorString()));

        QObject* scroller = root->findChild<QObject*>(QStringLiteral("scroller"));
        QVERIFY(scroller != nullptr);

        // `view` must be the ListView itself (root), not its contentItem.
        const QVariant viewVar = scroller->property("view");
        QCOMPARE(viewVar.value<QObject*>(), root);

        delete root;
    }
};

#include "test_qml_wheelscroller.moc"
```

- [ ] **Step 2: Wire the test into the build**

In `tests/CMakeLists.txt`, find the `gittide_ui_test_sources` list (it already
contains `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_graph.cpp` per
`ui/CMakeLists.txt` conventions) and add:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_wheelscroller.cpp
```

In `tests/ui/main.cpp`, add near the other `#include "test_qml_*.cpp"` lines:

```cpp
#include "test_qml_wheelscroller.cpp"
```

and add a `QTest::qExec` call in `main()` alongside the others:

```cpp
    { TestQmlWheelScroller t; status |= QTest::qExec(&t, argc, argv); }
```

- [ ] **Step 3: Build and run — verify the test currently PASSES**

The fix is already applied in the working tree, so this test should pass
immediately, proving the regression guard is correctly wired:

```bash
cmake --build build --target gittide_ui_tests --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests -select TestQmlWheelScroller
```

Expected: `PASS` for `view_resolves_to_the_enclosing_flickable_not_its_content_item`.

- [ ] **Step 4: Verify the test actually catches the regression**

Temporarily revert `ui/qml/WheelScroller.qml`'s `view` property to the buggy
form (`property Flickable view: parent`), rebuild, and re-run the same test —
confirm it now **FAILS** (either a QML type-coercion warning + `view` reads as
a null `QObject*`, or `QCOMPARE` mismatches `root`). Then restore the fixed
form (`property Flickable view: parent ? parent.parent : null`) and rebuild —
confirm `PASS` again.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/test_qml_wheelscroller.cpp tests/CMakeLists.txt tests/ui/main.cpp ui/qml/WheelScroller.qml
git commit -m "fix(ui): mouse-wheel scroll was dead everywhere — WheelHandler.parent is Flickable's contentItem, not the Flickable"
```

---

## Task 2: Graph tab — drop the inline commit-detail panel, widen the graph

**Files:**
- Modify: `ui/qml/GraphPane.qml`
- Modify: `tests/ui/test_qml_graph.cpp` (add structural assertion)

**Interfaces:**
- Consumes: nothing new.
- Produces: `GraphPane` no longer exposes an object named `graphCommitDetail`;
  its commit-list `Item` becomes `Layout.fillWidth: true` (was
  `Layout.preferredWidth: 460`).

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_qml_graph.cpp`, inside `class TestQmlGraph`, a new slot
(place it after `graph_tab_exists_and_selection_drives_commit_detail`):

```cpp
    void graph_tab_has_no_inline_commit_detail_panel()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        {
            QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historySpy.wait(3000));
        }

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        {
            QSignalSpy historyReady2(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historyReady2.wait(3000));
        }

        QObject* root = engine.rootObjects().first();

        // Switch to Graph tab (index 2).
        QSignalSpy graphSpy(vm.graph(), &QAbstractItemModel::modelReset);
        QObject* tabBar = root->findChild<QObject*>(QStringLiteral("changesTabBar"));
        QVERIFY(tabBar != nullptr);
        tabBar->setProperty("currentIndex", 2);
        QVERIFY(graphSpy.wait(3000));

        // No inline commit-detail panel in the Graph tab anymore.
        QVERIFY(root->findChild<QObject*>(QStringLiteral("graphCommitDetail")) == nullptr);

        // The commit list fills the whole pane width — find its ListView and
        // compare its width against the tab body's width (allow a 2px margin
        // for the pane's own layout spacing/borders).
        QObject* graphList = root->findChild<QObject*>(QStringLiteral("graphList"));
        QVERIFY(graphList != nullptr);
        QObject* graphTabBody = root->findChild<QObject*>(QStringLiteral("graphTabBody"));
        QVERIFY(graphTabBody != nullptr);
        const qreal listWidth = graphList->property("width").toReal();
        const qreal paneWidth = graphTabBody->property("width").toReal();
        QVERIFY2(listWidth > paneWidth - 2.0,
                 qPrintable(QStringLiteral("expected graphList to fill the pane: list=%1 pane=%2")
                                .arg(listWidth).arg(paneWidth)));

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build build --target gittide_ui_tests --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests -select TestQmlGraph
```

Expected: `FAIL` — `graphCommitDetail` is still found (it exists today).

- [ ] **Step 3: Remove the detail panel, widen the list**

In `ui/qml/GraphPane.qml`:

Change the commit-list wrapper `Item` (currently lines 34-36) from a fixed
width to fill-width:

```qml
    // ---- Commit list (graph column + ref chips + avatar + summary/author/date) ----
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
```

Delete the hairline divider block and the `CommitDetail` block and their
trailing `Connections` block (currently lines 196-215):

```qml
    // Hairline divider
    Rectangle {
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        color: theme.border
    }

    // ---- Selected-commit detail (files + read-only diff) ----
    CommitDetail {
        id: graphDetail
        objectName: "graphCommitDetail"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    Connections {
        target: graphDetail
        function onTabBackward() { graphList.forceActiveFocus() }
        function onTabForward() { graphPane.tabNext() }
    }
```

Update `takeFocusLast()` (currently line 15) — there is no longer an inner
detail pane to hand off to, so it becomes the same as `takeFocus()`:

```qml
    function takeFocus() { graphList.forceActiveFocus() }
    // No inner detail pane anymore (Graph tab is graph-only); Tab chain entry
    // from either direction lands on the same, only focusable element.
    function takeFocusLast() { graphList.forceActiveFocus() }
```

Update `graphList`'s `Keys.onTabPressed` (currently lines 61-64) — Tab now
leaves the pane entirely instead of moving to the (now-removed) detail pane:

```qml
            Keys.onTabPressed: {
                graphPane.tabNext()
                event.accepted = true
            }
```

- [ ] **Step 4: Run it to verify it passes**

```bash
cmake --build build --target gittide_ui_tests --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests -select TestQmlGraph
```

Expected: `PASS` for all `TestQmlGraph` slots, including the new one.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/GraphPane.qml tests/ui/test_qml_graph.cpp
git commit -m "feat(ui): Graph tab drops the inline commit-detail panel, graph fills the pane"
```

---

## Task 3: Double-click a graph row switches to History with that commit's diff

**Files:**
- Modify: `ui/qml/GraphPane.qml`
- Modify: `ui/qml/WorkingPane.qml`
- Modify: `tests/ui/test_qml_graph.cpp` (add end-to-end assertion)

**Interfaces:**
- Consumes: `RepoViewModel::selectGraphCommitAtRow(int)` (already exists, sets
  the shared `selectedCommit`/`commitFiles`/`commitDiff` state also read by
  `HistoryPane`'s `CommitDetail`).
- Produces: `GraphPane` gains `signal commitActivated()` and
  `function activateRow(index)` (calls `graphList.selectRow(index)` then emits
  `commitActivated()`) — both are `QMetaObject::invokeMethod`/signal-spy
  reachable from tests without simulating a physical double-click gesture.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_qml_graph.cpp`, after the test added in Task 2:

```cpp
    void double_click_activates_row_and_switches_to_history_tab()
    {
        const auto dir = qml_graph_test::make_branched_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        {
            QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historySpy.wait(3000));
        }

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        {
            QSignalSpy historyReady2(vm.history(), &QAbstractItemModel::modelReset);
            vm.open(QString::fromStdString(dir.generic_string()));
            QVERIFY(historyReady2.wait(3000));
        }

        QObject* root = engine.rootObjects().first();

        QSignalSpy graphSpy(vm.graph(), &QAbstractItemModel::modelReset);
        QObject* tabBar = root->findChild<QObject*>(QStringLiteral("changesTabBar"));
        QVERIFY(tabBar != nullptr);
        tabBar->setProperty("currentIndex", 2);
        QVERIFY(graphSpy.wait(3000));
        QVERIFY(vm.graph()->rowCount(QModelIndex()) >= 1);

        QObject* graphBody = root->findChild<QObject*>(QStringLiteral("graphTabBody"));
        QVERIFY(graphBody != nullptr);

        // Simulate the double-click's effect: activateRow(0) selects the row
        // (shared repoVm selection state) and emits commitActivated(), which
        // WorkingPane wires to a tab switch.
        bool invoked = QMetaObject::invokeMethod(graphBody, "activateRow", Q_ARG(QVariant, 0));
        QVERIFY(invoked);

        QVERIFY(!vm.selectedCommit().isEmpty());
        QCOMPARE(tabBar->property("currentIndex").toInt(), 1);

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build build --target gittide_ui_tests --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests -select TestQmlGraph
```

Expected: `FAIL` — `activateRow` doesn't exist yet on `GraphPane`
(`QMetaObject::invokeMethod` returns `false`), and/or `currentIndex` stays `2`.

- [ ] **Step 3: Add the signal + function + double-tap handler to GraphPane.qml**

Add the signal next to the existing `tabNext()`/`tabPrev()` signals (currently
lines 17-18):

```qml
    signal tabNext()
    signal tabPrev()
    // Fired when a graph row is double-clicked; WorkingPane switches to History.
    signal commitActivated()

    // Select the row (shared repoVm selection state, same as a single click)
    // then signal a hand-off to History. Exposed as a root-level function so
    // it's reachable both from the delegate's DoubleTap TapHandler and from
    // tests via QMetaObject::invokeMethod, without simulating a click gesture.
    function activateRow(index) {
        graphList.selectRow(index)
        commitActivated()
    }
```

Add a third `TapHandler` to the row delegate, alongside the existing
left-click/right-click ones (currently lines 87-107), for the double-click
gesture:

```qml
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    gesture: TapHandler.DoubleTap
                    onTapped: {
                        graphList.currentIndex = index
                        graphPane.activateRow(index)
                    }
                }
```

- [ ] **Step 4: Wire the tab switch in WorkingPane.qml**

Add a `Connections` block near the existing `onRebaseMessagePauseEntered`
one (currently around line 245-249):

```qml
    // Double-click on a Graph row hands off to History with that commit's
    // diff already selected (selectedCommit/commitDiff are shared repoVm
    // state, so no oid look-up is needed — just switch the tab).
    Connections {
        target: graphTabBody
        function onCommitActivated() {
            tabs.currentIndex = 1
            historyTabBody.takeFocus()
        }
    }
```

- [ ] **Step 5: Run it to verify it passes**

```bash
cmake --build build --target gittide_ui_tests --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests -select TestQmlGraph
```

Expected: `PASS` for all `TestQmlGraph` slots, including the new one.

- [ ] **Step 6: Full suite + manual smoke check**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass (the pre-existing unrelated `ECMPoQmToolsTest`
failure, if present, is not caused by this plan and can be ignored).

Then manually launch `./build/app/gittide_app`, open a repo with at least two
branches, go to the Graph tab, confirm: (a) the graph fills the full pane
width with no right-hand panel, (b) mouse-wheel scrolling works in the graph
list, (c) double-clicking a commit row switches to the History tab and shows
that commit's diff in the right-hand panel.

- [ ] **Step 7: Commit**

```bash
git add ui/qml/GraphPane.qml ui/qml/WorkingPane.qml tests/ui/test_qml_graph.cpp
git commit -m "feat(ui): double-click a Graph row switches to History showing that commit's diff"
```

---

## Outcome

> Fill in when the plan reaches `done`. The durable result — what shipped and
> **where it now lives** in the code and the living spec. This is the bridge a
> future reader follows from "this plan" to "the current truth":
>
> - Shipped: <summary>.
> - Spec updated: <which `spec/` sections now describe this>.
> - Code: <the main files/types that resulted>.
