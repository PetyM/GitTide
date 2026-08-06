import QtQuick

// Pointer and keyboard selection surface for a diff ListView. Declared as a
// SIBLING of the list, never inside a delegate: an autoscrolling drag scrolls the
// row it started on out of view, the ListView destroys that delegate, and a
// delegate-owned MouseArea would lose its mouse grab mid-drag.
//
// Talks to the list only through indexAt / itemAtIndex / contentY /
// contentHeight / height, so a stub object drives it in tests — where real
// ListView delegates are never instantiated.
MouseArea {
    id: overlay
    objectName: "diffSelectionOverlay"

    // The diff ListView this overlay selects in.
    property var list: null
    // The DiffSelection to drive.
    property var selection: null
    // The list's vertical AppScrollBar, if any. A press over it must reach the
    // scrollbar, not start a text selection — see inScrollBar().
    property var scrollBar: null

    // Emitted with the text to put on the clipboard; the owning view routes it to
    // repoVm.copyToClipboard so clipboard access stays in one place.
    signal copyRequested(string text)

    property bool dragging: false
    property real lastX: 0
    property real lastY: 0
    // Last hovered position, tracked whether or not a button is down — drives
    // the cursor shape (see cursorShape below). (-1, -1) means "not hovering".
    property point hoverPos: Qt.point(-1, -1)

    acceptedButtons: Qt.LeftButton | Qt.RightButton
    // I-beam only where a press would actually start a selection; everywhere
    // else (checkboxes, gutter, sign column, conflict buttons, the scrollbar)
    // keeps the ordinary arrow, matching what's actually clickable there.
    cursorShape: canSelectAt(hoverPos.x, hoverPos.y) ? Qt.IBeamCursor : Qt.ArrowCursor

    // Tracks hoverPos for the cursorShape binding above, without hoverEnabled:
    // a MouseArea with hoverEnabled true accepts hover events and stops their
    // delivery to items beneath it, so every checkbox, Accept button and
    // scrollbar handle under this full-viewport overlay would lose its hover
    // tint. HoverHandler does not consume hover (its `blocking` property
    // defaults to false), so it observes the same pointer without blocking it.
    HoverHandler {
        id: hoverTracker
        onPointChanged: overlay.hoverPos = hovered ? point.position : Qt.point(-1, -1)
    }

    // --- hit testing -------------------------------------------------------

    function findCode(item) {
        if (!item || !item.visible)
            return null
        if (item.objectName === "diffCodeText")
            return item
        for (var i = 0; i < item.children.length; ++i) {
            const found = findCode(item.children[i])
            if (found)
                return found
        }
        return null
    }

    function codeItemAtRow(row) {
        if (!list || row < 0)
            return null
        return findCode(list.itemAtIndex(row))
    }

    // Model row under overlay-local y. y outside the viewport is clamped to the
    // nearest edge row, which is what an autoscrolling drag wants.
    function rowAt(y) {
        if (!list)
            return -1
        const clamped = Math.max(0, Math.min(list.height - 1, y))
        return list.indexAt(1, clamped + list.contentY)
    }

    // Character offset under (x, y) in the row's code item, or -1 when the point
    // is left of the code column or the row has no code item (a conflict header).
    function columnAt(row, x, y) {
        const code = codeItemAtRow(row)
        if (!code)
            return -1
        const local = code.mapFromItem(overlay, x, y)
        if (local.x < 0)
            return -1
        return code.positionAt(local.x, local.y)
    }

    // True when (x, y) falls inside the scrollbar's current on-screen rect,
    // and the bar is actually interactive right now. `visible` alone is not
    // enough: AppScrollBar's policy is ScrollBar.AsNeeded, and the Basic
    // style's ScrollBar is `visible: control.policy !== ScrollBar.AlwaysOff`
    // — true whenever the policy isn't AlwaysOff, regardless of whether the
    // content actually overflows. AsNeeded hides only the *handle*, which
    // AppScrollBar ties to `size < 1.0` (see its opacity binding) — so that's
    // the check that tells a real, clickable bar from an always-`visible`,
    // nothing-to-drag one on a diff short enough to need no scrolling.
    function inScrollBar(x, y) {
        if (!scrollBar || !scrollBar.visible || scrollBar.size >= 1.0)
            return false
        const topLeft = scrollBar.mapToItem(overlay, 0, 0)
        return x >= topLeft.x && x <= topLeft.x + scrollBar.width &&
               y >= topLeft.y && y <= topLeft.y + scrollBar.height
    }

    // True when a press/hover at (x, y) would land in a row's code column —
    // shared by pressAt() (should this press start a selection?) and the
    // hover cursor (should it show an I-beam?).
    function canSelectAt(x, y) {
        if (!selection || inScrollBar(x, y))
            return false
        const row = rowAt(y)
        return row >= 0 && columnAt(row, x, y) >= 0
    }

    // --- selection driving -------------------------------------------------

    // Returns true when the press started a selection, false when it belongs to
    // whatever sits underneath (checkbox, gutter, conflict buttons, scrollbar).
    function pressAt(x, y, modifiers) {
        if (!canSelectAt(x, y))
            return false
        const row = rowAt(y)
        const col = columnAt(row, x, y)

        lastX = x
        lastY = y
        const shiftExtend = (modifiers & Qt.ShiftModifier) !== 0
        if (shiftExtend)
            selection.extendTo(row, col)
        else
            selection.begin(row, col)
        dragging = true
        // A shift-click extend is its own gesture, like a drag: the paired
        // release (handleRelease) must not run the click-count path, which
        // would otherwise call clickAt(..., 1) and clear what this press
        // just extended.
        shiftExtendPress = shiftExtend
        return true
    }

    function moveTo(x, y) {
        if (!dragging || !selection)
            return
        lastX = x
        lastY = y
        moved = true
        const row = rowAt(y)
        if (row < 0)
            return
        var col = columnAt(row, x, y)
        if (col < 0)
            // No code item on this row: extend through it — to the row end when
            // dragging downwards, to its start when dragging back up.
            col = row > selection.anchorRow ? selection.rowLength(row) : 0
        selection.extendTo(row, col)
        autoScroll.running = y < 0 || y > overlay.height
    }

    function endDrag() {
        dragging = false
        autoScroll.running = false
    }

    // Ends the drag the same way endDrag() does, but also clears the gesture
    // flags endDrag()'s own callers (handleRelease()) clear themselves once
    // they're done reading them. onCanceled has no such follow-up: a grab
    // cancelled mid-drag (window deactivation, another handler stealing it)
    // would otherwise leave `moved`/`shiftExtendPress` at their pre-cancel
    // values, and the next plain click's release would misread them as "this
    // click is its own gesture" (see handleRelease) and silently skip
    // clearing the selection.
    function cancelDrag() {
        endDrag()
        moved = false
        shiftExtendPress = false
    }

    // count: 1 = plain click (clears), 2 = word, 3 = whole row.
    function clickAt(x, y, count) {
        if (!selection)
            return
        const row = rowAt(y)
        const col = columnAt(row, x, y)
        if (row < 0 || col < 0)
            return
        if (count >= 3)
            selection.selectLine(row)
        else if (count === 2)
            selection.selectWord(row, col)
        else
            selection.clear()
    }

    // Returns true when the key was consumed. Ctrl+C copies code text,
    // Ctrl+Shift+C the same selection with diff markers, Ctrl+A selects the
    // whole diff. Modifiers are matched exactly (not just "Control is among
    // the bits set") — `modifiers & Qt.ControlModifier` alone would also fire
    // for Ctrl+Alt+A, stealing a shortcut that belongs to something else.
    function handleKey(key, modifiers) {
        if (!selection)
            return false
        if (key === Qt.Key_A && modifiers === Qt.ControlModifier) {
            selection.selectAll()
            return true
        }
        if (key === Qt.Key_C && modifiers === Qt.ControlModifier) {
            overlay.copyRequested(selection.copyText(false))
            return true
        }
        if (key === Qt.Key_C && modifiers === (Qt.ControlModifier | Qt.ShiftModifier)) {
            overlay.copyRequested(selection.copyText(true))
            return true
        }
        return false
    }

    // Qt has no triple-click signal — count presses that land close together on
    // the same row, the way editors do.
    property int clickCount: 0
    property int clickRow: -1
    property double lastClickAt: 0
    property bool moved: false
    // Set by pressAt() when the press just handled was a Shift+click extend —
    // consumed (and reset) by the very next handleRelease(), see pressAt().
    property bool shiftExtendPress: false

    // Starts a selection via pressAt and, only when it did, takes keyboard
    // focus — a rejected press (checkbox, gutter, sign column, conflict Accept
    // buttons) must leave focus with whatever is underneath. Factored out of
    // onPressed so it can be driven directly (a QML "pressed" signal handler
    // isn't itself invokable from C++, headless tests need a way in).
    function handlePress(x, y, modifiers) {
        const handled = pressAt(x, y, modifiers)
        if (handled)
            overlay.forceActiveFocus()
        return handled
    }

    // Ends the drag (if any) and does the click-count bookkeeping, or calls
    // clickAt() for a plain click. Factored out of onReleased for the same
    // reason as handlePress: a QML "released" signal handler isn't itself
    // invokable from C++, headless tests need a way in.
    function handleRelease(x, y) {
        endDrag()
        if (moved || shiftExtendPress) {
            // A drag, or a Shift+click extend, is its own gesture: the click
            // that follows starts a fresh count, not a double-click on
            // whatever came before — and, for the shift case, this release
            // must not itself run clickAt(..., 1), which would clear the
            // selection the paired press just extended.
            moved = false
            shiftExtendPress = false
            clickCount = 0
            clickRow = -1
            return
        }
        const row = rowAt(y)
        const now = Date.now()
        clickCount = (now - lastClickAt < 400 && row === clickRow) ? clickCount + 1 : 1
        lastClickAt = now
        clickRow = row
        clickAt(x, y, clickCount)
    }

    // Routes a press by button. Left starts/extends the selection via
    // handlePress(); right never touches the selection — it opens the
    // context menu instead. Factored out of onPressed, like handlePress
    // itself, so button dispatch is invokable from a headless test rather
    // than only reachable through a real QML "pressed" event.
    function dispatchPress(button, x, y, modifiers) {
        if (button === Qt.RightButton) {
            // Right-click never changes the selection — it acts on it.
            contextMenu.popup()
            return true
        }
        return handlePress(x, y, modifiers)
    }

    // Routes a release by button. A right-button release must never reach
    // handleRelease(): its paired press already left the selection and the
    // click counter untouched, and running the click-counting/clickAt()
    // path here would clear or mutate the selection out from under an open
    // context menu, and would also plant a bogus click in the counter that
    // misreads the next left click as a double.
    function dispatchRelease(button, x, y) {
        if (button === Qt.RightButton)
            return
        handleRelease(x, y)
    }

    onPressed: function(mouse) { mouse.accepted = dispatchPress(mouse.button, mouse.x, mouse.y, mouse.modifiers) }
    onPositionChanged: function(mouse) {
        hoverPos = Qt.point(mouse.x, mouse.y)
        moveTo(mouse.x, mouse.y)
    }
    onReleased: function(mouse) { dispatchRelease(mouse.button, mouse.x, mouse.y) }
    onExited: hoverPos = Qt.point(-1, -1)
    // The grab can be cancelled out from under a drag (window deactivation,
    // another handler stealing it) without ever delivering a release. Without
    // this, "dragging" stays true forever and the autoscroll timer keeps
    // stepping contentY and extending the selection on its own.
    onCanceled: cancelDrag()

    Keys.onPressed: function(event) {
        event.accepted = handleKey(event.key, event.modifiers)
    }

    DiffContextMenu {
        id: contextMenu
        hasSelection: overlay.selection ? overlay.selection.hasSelection : false
        onCopy: if (overlay.selection) overlay.copyRequested(overlay.selection.copyText(false))
        onCopyWithMarkers: if (overlay.selection) overlay.copyRequested(overlay.selection.copyText(true))
        onSelectAll: if (overlay.selection) overlay.selection.selectAll()
    }

    Timer {
        id: autoScroll
        interval: 16
        repeat: true
        onTriggered: {
            if (!overlay.list)
                return
            const over = overlay.lastY < 0 ? overlay.lastY : (overlay.lastY - overlay.height)
            const step = Math.max(-60, Math.min(60, over))
            const max = Math.max(0, overlay.list.contentHeight - overlay.list.height)
            overlay.list.contentY = Math.max(0, Math.min(max, overlay.list.contentY + step))
            overlay.moveTo(overlay.lastX, overlay.lastY)
        }
    }
}
