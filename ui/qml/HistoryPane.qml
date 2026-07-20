import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GitTide 1.0

RowLayout {
    id: historyPane
    spacing: 0

    function takeFocus() { historyList.forceActiveFocus() }
    // Entry from the Tab chain in reverse — land on the last element (file list).
    function takeFocusLast() { commitDetail.takeFocus() }

    // Tab handoff to the surrounding chain (sidebar repo tree).
    signal tabNext()
    signal tabPrev()

    // Stable anchor for the drag-to-reorder/squash logic. The root's objectName is
    // overridden to "historyTabBody" when instantiated inside WorkingPane, so pure
    // helper functions live here where the name is always reachable via findChild
    // (tests) and the `id` is reachable from the delegate (dropLogic.dropZoneAt(…)).
    QtObject {
        id: dropLogic
        objectName: "historyPane"

        // Resolve a drop position within a target row into one of three bands:
        // top third inserts above, bottom third below (reorder), middle third squashes
        // into the target. Pure — unit-tested in test_qml_history.cpp.
        // Parameter types are `var` (QVariant) so Q_ARG(QVariant,…) matches;
        // return type `string` (QString) so Q_RETURN_ARG(QString,…) matches.
        function dropZoneAt(localY: var, rowHeight: var): string {
            if (localY <= rowHeight / 3)
                return "above"
            if (localY >= rowHeight * 2 / 3)
                return "below"
            return "squash"
        }

        // Live drop-target tracking — updated on every centroid move during an
        // active armed drag; cleared on release so indicators disappear.
        property int dropTargetIndex: -1
        property string dropTargetZone: ""
        readonly property int rowHeight: 48

        // Floating-chip drag state (set when a row arms, cleared on release).
        property bool   dragActive: false
        property string draggedSummary: ""
        property string draggedShortOid: ""
        property point  dragPos: Qt.point(0, 0)

        // @p contentPt is in historyList.contentItem coordinates (the caller maps
        // the drag centroid through mapToItem before calling).
        function updateDropTarget(contentPt) {
            var to = historyList.indexAt(contentPt.x, contentPt.y)
            if (to < 0 || !repoVm || to >= repoVm.reorderableRunLength) {
                dropTargetIndex = -1; dropTargetZone = ""; return
            }
            var localY = contentPt.y - to * rowHeight
            dropTargetIndex = to
            dropTargetZone = dropZoneAt(localY, rowHeight)
        }

        // Route a released drag: squash folds the dragged commit into the target;
        // reorder (above/below) goes through the existing confirmation dialog. Both
        // source and target must lie in the reorderable run and differ.
        function performDrop(fromIndex: var, toIndex: var, zone: var) {
            if (!repoVm)
                return
            if (toIndex < 0 || toIndex >= repoVm.reorderableRunLength || toIndex === fromIndex)
                return
            if (zone === "squash")
                repoVm.squashCommitInto(fromIndex, toIndex)
            else
                reorderConfirm.openFor(fromIndex, toIndex, zone)  // zone is "above"/"below"
        }
    }

    // ---- Commit context menu (right-click on a history row) ----
    CommitContextMenu {
        id: commitMenu
        onCopySha:           if (repoVm) repoVm.copyToClipboard(commitMenu.oid)
        onNewBranchFromHere: commitNewBranchDialog.openFromCommit(commitMenu.oid)
        onCheckoutCommit:    if (repoVm) repoVm.checkoutCommit(commitMenu.oid)
        onReword:            rewordDialog.openFor(commitMenu.oid)
        onUndoLastCommit:    if (repoVm) repoVm.undoLastCommit()
        onSquashSelected:    if (repoVm) repoVm.requestSquashTodo(historyList.selectedRows)
        onEditHistory:       if (repoVm) repoVm.requestRebaseTodo(commitMenu.oid)
        onMerge:             if (repoVm) repoVm.startMerge(commitMenu.localBranchName)
    }

    NewBranchDialog {
        id: commitNewBranchDialog
    }

    RewordDialog {
        id: rewordDialog
        onReworded: function(message) { if (repoVm) repoVm.rewordHead(message) }
    }

    RebaseTodoDialog {
        id: rebaseTodoDialog
    }

    ReorderConfirmDialog {
        id: reorderConfirm
    }

    // ---- Commit list (graph + avatar + summary/author/date) ----
    Item {
        id: listColumn
        Layout.preferredWidth: 420
        Layout.fillHeight: true

        ListView {
            id: historyList
            objectName: "historyList"
            anchors.fill: parent
            clip: true
            model: repoVm ? repoVm.history : null

            ScrollBar.vertical: AppScrollBar {}
            WheelScroller {}

            // Selected row indices. Always includes currentIndex.
            property var selectedRows: []

            function applySelection() {
                if (repoVm) repoVm.selectCommitRows(selectedRows)
            }

            activeFocusOnTab: true
            Keys.onUpPressed: {
                if (currentIndex > 0) {
                    currentIndex--
                    selectedRows = [currentIndex]
                    if (repoVm) repoVm.selectCommitAtRow(currentIndex)
                }
            }
            Keys.onDownPressed: {
                if (currentIndex < count - 1) {
                    currentIndex++
                    selectedRows = [currentIndex]
                    if (repoVm) repoVm.selectCommitAtRow(currentIndex)
                }
            }
            Keys.onTabPressed: {
                commitDetail.takeFocus()
                event.accepted = true
            }
            Keys.onBacktabPressed: {
                historyPane.tabPrev()
                event.accepted = true
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: dropLogic.rowHeight  // single source of truth (dropLogic.rowHeight)
                color: (ListView.isCurrentItem
                        || historyList.selectedRows.indexOf(index) >= 0)
                       ? theme.surfaceOverlay : "transparent"

                // Whole-row hold-to-drag gesture state.
                property bool dragArmed: false
                opacity: dragArmed ? 0.7 : 1.0
                border.width: dragArmed ? 1 : 0
                border.color: theme.accent

                // Accent left border on the selected row (over the graph cell, x=0).
                Rectangle {
                    visible: parent.ListView.isCurrentItem
                             || historyList.selectedRows.indexOf(index) >= 0
                    width: 2
                    height: parent.height
                    color: theme.accent
                }

                // TapHandler for left-click selection. Cooperates with the sibling
                // DragHandler (rowDrag) via shared pointer grabs — unlike a MouseArea
                // it does NOT take an exclusive grab on press, so DragHandler can win
                // the grab after the hold timer fires.
                TapHandler {
                    id: leftTap
                    objectName: "leftTapHandler"
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        historyList.forceActiveFocus()
                        const mods = point.modifiers
                        if (mods & Qt.ShiftModifier) {
                            var anchor = historyList.currentIndex
                            var lo = Math.max(0, Math.min(anchor, index))
                            var hi = Math.max(anchor, index)
                            var range = []
                            for (var r = lo; r <= hi; ++r) range.push(r)
                            historyList.selectedRows = range
                            historyList.currentIndex = index
                        } else if (mods & Qt.ControlModifier) {
                            var set = historyList.selectedRows.slice()
                            var at = set.indexOf(index)
                            if (at >= 0) set.splice(at, 1); else set.push(index)
                            if (set.indexOf(historyList.currentIndex) < 0)
                                set.push(historyList.currentIndex)
                            historyList.selectedRows = set
                            historyList.currentIndex = index
                        } else {
                            historyList.selectedRows = [index]
                            historyList.currentIndex = index
                        }
                        if (repoVm) historyList.applySelection()
                    }
                }

                // TapHandler for right-click context menu. Keeps an existing
                // multi-selection so "Squash N commits…" applies to the whole range.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        historyList.forceActiveFocus()
                        var inMulti = historyList.selectedRows.length >= 2
                                      && historyList.selectedRows.indexOf(index) >= 0
                        if (!inMulti) {
                            historyList.currentIndex = index
                            historyList.selectedRows = [index]
                        }
                        commitMenu.oid             = model.oid
                        commitMenu.shortOid        = model.shortOid
                        commitMenu.localBranchName = model.localBranchName ?? ""
                        commitMenu.isHead          = model.isHead
                        commitMenu.selectionCount  = historyList.selectedRows.length
                        commitMenu.popup()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 12
                    spacing: 8

                    Avatar {
                        name: model.author
                        email: model.authorEmail
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: model.summary
                            // Local-only (unpushed) commits stay full strength;
                            // already-pushed ones dim, emphasising what isn't shared.
                            color: model.isLocalOnly ? theme.textPrimary : theme.textSecondary
                            font.pixelSize: 13
                        }
                        RowLayout {
                            spacing: 8
                            Label {
                                text: model.author
                                color: theme.textMuted
                                font.pixelSize: 11
                            }
                            Label {
                                text: model.shortOid
                                color: theme.textMuted
                                font.family: "monospace"
                                font.pixelSize: 11
                            }
                            Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignRight
                                text: model.date
                                color: theme.textMuted
                                font.pixelSize: 11
                            }
                        }
                    }

                    // Unpushed badge: a shape/glyph cue (not colour alone) marking
                    // a commit that is not yet on any remote. Paired with the dim
                    // treatment above and the hollow graph dot in the Graph tab.
                    Label {
                        objectName: "localOnlyBadge"
                        visible: model.isLocalOnly
                        text: "↑" // upwards arrow — "needs pushing"
                        color: theme.textSecondary
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        Layout.alignment: Qt.AlignVCenter
                        ToolTip.visible: badgeHover.hovered
                        ToolTip.text: qsTr("Local only — not yet pushed")
                        HoverHandler { id: badgeHover }
                    }

                }

                // Reorder insertion line on the hovered target row (above / below bands).
                Rectangle {
                    visible: dropLogic.dropTargetIndex === index
                             && (dropLogic.dropTargetZone === "above" || dropLogic.dropTargetZone === "below")
                    width: parent.width
                    height: 2
                    color: theme.accent
                    y: dropLogic.dropTargetZone === "above" ? 0 : parent.height - height
                    z: 1
                }

                // Squash highlight + badge on the hovered target row (squash band).
                Rectangle {
                    visible: dropLogic.dropTargetIndex === index && dropLogic.dropTargetZone === "squash"
                    anchors.fill: parent
                    color: theme.surfaceOverlay
                    z: 1
                    Label {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: "◆ squash"
                        color: theme.accent
                        font.pixelSize: 11
                    }
                }

                // Whole-row drag, armed by a press-and-hold so a quick click still selects.
                // Only rows in the reorderable run participate.
                DragHandler {
                    id: rowDrag
                    enabled: !!repoVm && index < repoVm.reorderableRunLength
                    target: null                 // we move nothing; we read the centroid on release
                    xAxis.enabled: false         // vertical only, matching the grip drag
                    // Arm only after the hold timer fires. Until armed, a drag does nothing,
                    // so a quick press-drag-release won't reorder.
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
                                    var rowTop = to * dropLogic.rowHeight
                                    var localY = p.y - rowTop
                                    var zone = dropLogic.dropZoneAt(localY, dropLogic.rowHeight)
                                    dropLogic.performDrop(index, to, zone)
                                }
                            }
                            dragArmed = false
                            dropLogic.dragActive = false
                            dropLogic.dropTargetIndex = -1
                            dropLogic.dropTargetZone = ""
                        }
                    }
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
                }

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
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: historyList.activeFocus ? theme.focusBorder : "transparent"
            border.width: 1
            enabled: false
        }

        // Floating drag chip — follows the cursor during an armed drag, showing
        // the dragged commit and whether the hovered drop is a move or a squash.
        Item {
            id: dragChip
            objectName: "dragChip"
            visible: dropLogic.dragActive
            // Expose drag-active state for headless tests: QQuickItem.visible
            // returns effective visibility (inherits from parents) so it reads as
            // false when the history tab is not the active StackLayout child.
            // chipVisible is a plain QML bool property, unaffected by that chain.
            readonly property bool chipVisible: dropLogic.dragActive
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
    }

    // Hairline divider
    Rectangle {
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        color: theme.border
    }

    // ---- Selected-commit detail (files + read-only diff) ----
    CommitDetail {
        id: commitDetail
        objectName: "commitDetail"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    Connections {
        target: commitDetail
        function onTabBackward() { historyList.forceActiveFocus() }
        function onTabForward() { historyPane.tabNext() }
    }
}
