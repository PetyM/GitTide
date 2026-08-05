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

    // Emitted with the text to put on the clipboard; the owning view routes it to
    // repoVm.copyToClipboard so clipboard access stays in one place.
    signal copyRequested(string text)

    property bool dragging: false
    property real lastX: 0
    property real lastY: 0

    acceptedButtons: Qt.LeftButton | Qt.RightButton
    cursorShape: Qt.IBeamCursor

    // --- hit testing -------------------------------------------------------

    function findCode(item) {
        if (!item)
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

    // --- selection driving -------------------------------------------------

    // Returns true when the press started a selection, false when it belongs to
    // whatever sits underneath (checkbox, gutter, conflict buttons).
    function pressAt(x, y, modifiers) {
        if (!selection)
            return false
        const row = rowAt(y)
        const col = columnAt(row, x, y)
        if (row < 0 || col < 0)
            return false

        lastX = x
        lastY = y
        if (modifiers & Qt.ShiftModifier)
            selection.extendTo(row, col)
        else
            selection.begin(row, col)
        dragging = true
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
    // whole diff.
    function handleKey(key, modifiers) {
        if (!selection || !(modifiers & Qt.ControlModifier))
            return false
        if (key === Qt.Key_A) {
            selection.selectAll()
            return true
        }
        if (key === Qt.Key_C) {
            overlay.copyRequested(selection.copyText((modifiers & Qt.ShiftModifier) !== 0))
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
        if (moved) {
            // A drag is its own gesture: the click that follows it starts a
            // fresh count, not a double-click on whatever came before the drag.
            moved = false
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

    // Right-button dispatch lives here rather than in handlePress(): only
    // onPressed sees mouse.button, and handlePress's (x, y, modifiers)
    // signature is what the tests drive directly.
    onPressed: function(mouse) {
        if (mouse.button === Qt.RightButton) {
            // Right-click never changes the selection — it acts on it.
            contextMenu.popup()
            mouse.accepted = true
            return
        }
        mouse.accepted = handlePress(mouse.x, mouse.y, mouse.modifiers)
    }
    onPositionChanged: function(mouse) { moveTo(mouse.x, mouse.y) }
    onReleased: function(mouse) { handleRelease(mouse.x, mouse.y) }

    Keys.onPressed: function(event) {
        event.accepted = handleKey(event.key, event.modifiers)
    }

    DiffContextMenu {
        id: contextMenu
        hasSelection: overlay.selection ? overlay.selection.hasSelection : false
        onCopy: overlay.copyRequested(overlay.selection.copyText(false))
        onCopyWithMarkers: overlay.copyRequested(overlay.selection.copyText(true))
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
