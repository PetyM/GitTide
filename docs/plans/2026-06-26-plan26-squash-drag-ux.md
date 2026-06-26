# Plan 26 — Squash / drag UX polish

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-06-26 |
| **Status** | `planned` |
| **Spec** | [spec/product/2026-06-26-squash-drag-ux-design.md](../spec/product/2026-06-26-squash-drag-ux-design.md) |
| **Depends on** | Plan 25 (history graph tab + drag fix), Plan 20 (interactive rebase) |

**Goal:** Make drag-to-squash feel direct: a floating chip follows the cursor
during a drag, and any interactive-rebase message pause auto-opens the
commit-message dialog (no Continue click).

**Architecture:** Two independent UI changes, no `core/` change. (1) `RepoViewModel`
emits a new one-shot `rebaseMessagePauseEntered()` on the rising edge into a
`RebasePause::Message`; `WorkingPane` opens the existing `rebaseMessageDialog`
automatically on that signal. (2) `HistoryPane` gains a pane-level floating chip
bound to drag state held on the `dropLogic` object.

**Tech stack:** Qt 6 Quick/QML (`TapHandler`/`DragHandler` already in place), C++23
ViewModel, QtTest headless runner.

## Global constraints

- **No `core/` / rebase-engine change** — the engine already pauses at
  `RebasePause::Message` with `messagePrefill` set. This plan only surfaces it.
- **Colour from theme tokens only** (`surfaceRaised`, `border`, `accent`,
  `textPrimary`/`textMuted`) — no hex literals.
- Don't change conflict-pause handling (banner + Continue stays).
- Don't change drag mechanics (hold-to-arm, three-band drop zones, `performDrop`).
- New `ui/` sources → none (edits only). New tests → add to the `gittide_ui_tests`
  source list + `tests/ui/main.cpp` (`#include` + `RUN`).
- **TDD:** failing test first per task.

### Build & test — REAL invocations (this repo)

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/michal/Qt/6.8.3/gcc_64 -DGITGUI_BUILD_QML=ON   # once
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests        # ONE QtTest binary, all classes
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure   # full suite
```

UI tests are **QtTest** `QObject` classes run by the shared `tests/ui/main.cpp`
(NOT Catch2). To add or extend one, mirror `tests/ui/test_repoviewmodel_rebase.cpp`
(real rebase via `gittide::test::TempRepo`) or `tests/ui/test_qml_history.cpp`
(QML load + `findChild`). A NEW ui test class needs three edits: add the source to
`gittide_ui_test_sources` in `tests/CMakeLists.txt`, `#include "test_<name>.cpp"` in
`tests/ui/main.cpp`, and `RUN(TestClass);` in its `main()`. Extending an existing
class needs none of those.

---

## Task 1: Auto-open the message dialog on a rebase message pause

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` (declare the signal)
- Modify: `ui/src/repoviewmodel.cpp:72` (rising-edge emit in the rebase lambda)
- Modify: `ui/qml/WorkingPane.qml` (refactor + auto-open `Connections`)
- Test: extend `tests/ui/test_repoviewmodel_rebase.cpp` (the `TestRepoViewModelRebase` class)

**Interfaces:**
- Produces: `void RepoViewModel::rebaseMessagePauseEntered()` — emitted once each
  time the rebase newly enters a `Message` pause (rising edge: pause becomes
  `Message`, or the step index advances while still `Message`).

- [ ] **Step 1: Write the failing test.** Add a slot to `TestRepoViewModelRebase`
  in `tests/ui/test_repoviewmodel_rebase.cpp`, mirroring the existing
  `squash_commit_into_folds_dragged_into_target` slot (~line 284) which builds a
  TempRepo with ≥2 commits, opens it in a `RepoViewModel`, and calls
  `squashCommitInto`. A real squash pauses at `RebasePause::Message`:

```cpp
    /// A drag-squash pauses for the combined message; the ViewModel must fire
    /// rebaseMessagePauseEntered exactly once on that rising edge.
    void squash_emits_message_pause_entered_once()
    {
        TempRepoFixture fx;                 // however the neighbouring slots build the repo
        RepoViewModel vm;
        openRepo(vm, fx);                   // mirror the existing slots' setup helpers
        QTRY_COMPARE(vm.reorderableRunLength(), 2);

        QSignalSpy pauseSpy(&vm, &RepoViewModel::rebaseMessagePauseEntered);
        QMetaObject::invokeMethod(&vm, "squashCommitInto", Q_ARG(int, 0), Q_ARG(int, 1));

        // The interactive rebase runs async on the pool; wait for the pause.
        QTRY_COMPARE(vm.rebasePauseReason(), QStringLiteral("message"));
        QCOMPARE(pauseSpy.count(), 1);

        // A subsequent rebaseStateChanged that is still the SAME message pause
        // (e.g. a refresh) must NOT re-fire the signal.
        emit /* or trigger */ ;            // if easy to provoke; otherwise omit this block
    }
```

  Use the EXACT repo-setup idiom of the neighbouring slots (don't invent
  `TempRepoFixture`/`openRepo` if they use inline setup — copy what they do). The
  essential assertions are the two `QCOMPARE`s: pause reason becomes `"message"`
  and `pauseSpy.count() == 1`.

- [ ] **Step 2: Run it, verify it fails** (signal undeclared → compile error, or
  `pauseSpy.count()` is 0):

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```
Expected: FAIL.

- [ ] **Step 3: Declare the signal** in `ui/include/gittide/ui/repoviewmodel.hpp`
  next to `void rebaseStateChanged();` (~line 271):

```cpp
    /// Emitted once when the rebase newly enters a Message pause (squash/reword
    /// step). Drives WorkingPane to auto-open the commit-message dialog.
    void rebaseMessagePauseEntered();
```

- [ ] **Step 4: Emit on the rising edge** in `ui/src/repoviewmodel.cpp`. Replace
  the existing rebase-state lambda (currently lines 72–73):

```cpp
    connect(m_controller, &RepoController::rebaseStateChanged, this,
            [this](const gittide::RebaseState& s) { m_rebase = s; emit rebaseStateChanged(); });
```
  with one that compares the previous `m_rebase` to the incoming `s` BEFORE
  overwriting it (no new members needed — `m_rebase` already holds the prior state):

```cpp
    connect(m_controller, &RepoController::rebaseStateChanged, this,
            [this](const gittide::RebaseState& s)
            {
                const bool wasMessage = (m_rebase.pause == gittide::RebasePause::Message);
                const int  prevStep   = m_rebase.current;
                m_rebase = s;
                emit rebaseStateChanged();
                const bool isMessage = (s.pause == gittide::RebasePause::Message);
                // Rising edge: just became a message pause, or advanced to a new
                // message step. Re-emits of the same step (refreshes) do nothing.
                if (isMessage && (!wasMessage || s.current != prevStep))
                    emit rebaseMessagePauseEntered();
            });
```
  (`gittide::RebasePause` is already visible via the `rebase.hpp` include used by
  the `rebasePauseReason()` accessor.)

- [ ] **Step 5: Auto-open in `ui/qml/WorkingPane.qml`.** Refactor the
  `RebaseBanner.onRequestMessageEdit` body (currently ~lines 128–144) into a
  reusable function on the `workingPane` root, then call it from both the banner
  and a new `Connections`. Add this function near the other `workingPane`
  functions (e.g. after `takeFocusLast`):

```qml
    // Open the interactive-rebase message editor, prefilled from the engine's
    // combined/reword text. Called automatically on a message pause and manually
    // from the rebase banner's Continue button.
    function openRebaseMessageDialog() {
        var prefill = repoVm ? repoVm.rebaseMessagePrefill : ""
        var nl = prefill.indexOf("\n")
        if (nl < 0) {
            rebaseMessageDialog.summary = prefill
            rebaseMessageDialog.body = ""
        } else {
            rebaseMessageDialog.summary = prefill.substring(0, nl)
            rebaseMessageDialog.body =
                prefill.substring(prefill.charAt(nl + 1) === "\n" ? nl + 2 : nl + 1)
        }
        rebaseMessageDialog.open()
    }
```
  Replace the `RebaseBanner.onRequestMessageEdit` block body with a single call:

```qml
            onRequestMessageEdit: workingPane.openRebaseMessageDialog()
```
  And add, near the other top-level `Connections` in `workingPane`:

```qml
    // Auto-open the message editor the moment an interactive rebase pauses for a
    // message (squash/reword) — no Continue click needed. The banner's Continue
    // remains a manual fallback if the dialog is dismissed.
    Connections {
        target: repoVm
        enabled: repoVm !== null
        function onRebaseMessagePauseEntered() { workingPane.openRebaseMessageDialog() }
    }
```

- [ ] **Step 6: Run the suite, verify GREEN.**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```
Expected: the new slot passes; no regressions.

- [ ] **Step 7: Commit.**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp \
        ui/qml/WorkingPane.qml tests/ui/test_repoviewmodel_rebase.cpp
git commit -m "feat(ui): auto-open message dialog on interactive-rebase message pause"
```

---

## Task 2: Floating drag chip in the history list

**Files:**
- Modify: `ui/qml/HistoryPane.qml`
- Test: extend `tests/ui/test_qml_history.cpp` (the `TestQmlHistory` class)

**Interfaces:**
- Consumes: existing `dropLogic` (objectName `"historyPane"`), `dragArmed`
  per-delegate, `rowDrag` DragHandler, `holdTimer`.
- Produces: pane-level chip `Item { objectName: "dragChip" }` whose `visible`
  binds to `dropLogic.dragActive`; `dropLogic` gains `dragActive` (bool),
  `draggedSummary` (string), `draggedShortOid` (string), `dragPos` (point).

- [ ] **Step 1: Write the failing test.** Add a slot to `TestQmlHistory` in
  `tests/ui/test_qml_history.cpp`, mirroring how that file loads a QML component
  and uses `findChild`. The chip is a pane-level child (not inside the ListView
  delegate), so it IS reachable headlessly:

```cpp
    /// The floating drag chip exists, is hidden until a drag arms, and shows the
    /// dragged commit summary driven by dropLogic state.
    void drag_chip_tracks_drag_state()
    {
        // Load HistoryPane (mirror this file's existing QML-load idiom).
        QObject* pane = /* loaded HistoryPane root */;
        QObject* chip = pane->findChild<QObject*>("dragChip");
        QVERIFY(chip != nullptr);
        QCOMPARE(chip->property("visible").toBool(), false);

        QObject* dropLogic = pane->findChild<QObject*>("historyPane"); // the QtObject
        QVERIFY(dropLogic != nullptr);
        dropLogic->setProperty("draggedSummary", QStringLiteral("fix: thing"));
        dropLogic->setProperty("dragActive", true);
        QCOMPARE(chip->property("visible").toBool(), true);
    }
```
  Match the file's actual QML-load helper (engine + component, or Main.qml +
  findChild) rather than inventing one.

- [ ] **Step 2: Run it, verify it fails** (`dragChip` not found / `dragActive`
  property missing):

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```
Expected: FAIL.

- [ ] **Step 3: Extend `dropLogic`** in `ui/qml/HistoryPane.qml` (the
  `QtObject { id: dropLogic; objectName: "historyPane" ... }`). Add these
  properties alongside the existing `dropTargetIndex`/`dropTargetZone`:

```qml
        // Floating-chip drag state (set when a row arms, cleared on release).
        property bool   dragActive: false
        property string draggedSummary: ""
        property string draggedShortOid: ""
        property point  dragPos: Qt.point(0, 0)
```

- [ ] **Step 4: Give the list-column `Item` an id** so the chip can parent to it
  and coordinates can map into it. The wrapping `Item` currently is
  `Item { Layout.preferredWidth: 420; Layout.fillHeight: true; ListView { id: historyList ... } }`.
  Add `id: listColumn`:

```qml
    Item {
        id: listColumn
        Layout.preferredWidth: 420
        Layout.fillHeight: true
        // ... existing ListView { id: historyList ... } and focus-border Rectangle ...
```

- [ ] **Step 5: Set chip state when the drag arms / moves / releases.** In the
  delegate:
  - In `holdTimer.onTriggered` (currently `onTriggered: dragArmed = true`), also
    set the chip source + active flag:

```qml
                Timer {
                    id: holdTimer
                    interval: 250
                    repeat: false
                    onTriggered: {
                        dragArmed = true
                        dropLogic.dragActive = true
                        dropLogic.draggedSummary = model.summary
                        dropLogic.draggedShortOid = model.shortOid
                    }
                }
```
  - In `rowDrag.onCentroidChanged`, after the existing `updateDropTarget` call,
    update `dragPos` mapped into `listColumn`:

```qml
                    onCentroidChanged: {
                        if (active && dragArmed) {
                            var p = mapToItem(historyList.contentItem,
                                              rowDrag.centroid.position.x,
                                              rowDrag.centroid.position.y)
                            dropLogic.updateDropTarget(p)
                            dropLogic.dragPos = mapToItem(listColumn,
                                              rowDrag.centroid.position.x,
                                              rowDrag.centroid.position.y)
                        }
                    }
```
  - In `rowDrag.onActiveChanged`, in the release branch where `dragArmed = false`
    and the drop indicators are cleared, also clear `dragActive`:

```qml
                            dragArmed = false
                            dropLogic.dragActive = false
                            dropLogic.dropTargetIndex = -1
                            dropLogic.dropTargetZone = ""
```

- [ ] **Step 6: Add the chip** as the last child of `listColumn` (after the
  focus-border Rectangle), so it floats above the list:

```qml
        // Floating drag chip — follows the cursor during an armed drag, showing
        // the dragged commit and whether the hovered drop is a move or a squash.
        Item {
            id: dragChip
            objectName: "dragChip"
            visible: dropLogic.dragActive
            z: 100
            width: chipBg.implicitWidth
            height: chipBg.implicitHeight
            x: Math.max(0, Math.min(listColumn.width - width - 4, dropLogic.dragPos.x + 12))
            y: Math.max(0, Math.min(listColumn.height - height - 4, dropLogic.dragPos.y + 12))

            Rectangle {
                id: chipBg
                implicitWidth: chipRow.implicitWidth + 16
                implicitHeight: chipRow.implicitHeight + 10
                radius: 4
                color: theme.surfaceRaised
                border.width: 1
                border.color: dropLogic.dropTargetZone === "squash" ? theme.accent : theme.border

                RowLayout {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 8
                    Label {
                        text: dropLogic.draggedShortOid
                        color: theme.textMuted
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                    Label {
                        text: dropLogic.draggedSummary
                        color: theme.textPrimary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.maximumWidth: 220
                    }
                    Label {
                        text: dropLogic.dropTargetZone === "squash" ? "◆ Squash"
                              : (dropLogic.dropTargetIndex >= 0 ? "Move" : "")
                        color: theme.accent
                        font.pixelSize: 11
                        visible: text.length > 0
                    }
                }
            }
        }
```

- [ ] **Step 7: Run the suite, verify GREEN.**

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```
Expected: the new slot passes; existing `TestQmlHistory` (drag/grip/dropZone) and
all other classes still pass.

- [ ] **Step 8: Manual-smoke note for the report.** Headless cannot synthesise the
  live drag; state in the report: "open repo → History → press-hold-drag a row →
  a chip follows the cursor; the hint reads '◆ Squash' over a squash target and
  'Move' over a reorder target."

- [ ] **Step 9: Commit.**

```bash
git add ui/qml/HistoryPane.qml tests/ui/test_qml_history.cpp
git commit -m "feat(ui): floating drag chip follows the cursor in history drag"
```

---

## Task 3: Docs close-out

**Files:**
- Modify: `docs/spec/product/2026-06-26-squash-drag-ux-design.md` (Status → shipped)
- Modify: `docs/spec/product/history-editing.md` and/or `rebase-interactive.md`
  (note the auto-open message flow + drag chip)
- Modify: `docs/plans/index.md` (add the Plan 26 row)
- Modify: this plan's **Status** → `done`, fill **Outcome**
- Modify: `docs/decisions.md` (optional one-liner)

- [ ] **Step 1: Update the living spec.** In the history-editing / interactive-rebase
  product spec, record: (a) an interactive-rebase **message pause auto-opens** the
  message-edit dialog (squash and reword), with the banner Continue as a fallback;
  (b) the history drag shows a **floating chip** under the cursor with a move/squash
  hint. Flip the design doc **Status** to `shipped`.

- [ ] **Step 2: Add the Plan 26 row** to `docs/plans/index.md` (mirror the Plan 25
  row format, link this file).

- [ ] **Step 3: Run the full suite** to confirm nothing regressed:

```bash
cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```
Expected: all PASS.

- [ ] **Step 4: Fill this plan's Outcome, set Status `done`, commit.**

```bash
git add docs/
git commit -m "docs: close out Plan 26 (squash/drag UX polish)"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: <which `spec/` sections now describe this>.
> - Code: `RepoViewModel::rebaseMessagePauseEntered`, `WorkingPane.openRebaseMessageDialog`,
>   the `dropLogic` drag-chip state + `dragChip` in `HistoryPane.qml`.
