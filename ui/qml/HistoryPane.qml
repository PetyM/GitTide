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
                height: 48
                color: (ListView.isCurrentItem
                        || historyList.selectedRows.indexOf(index) >= 0)
                       ? theme.surfaceOverlay : "transparent"

                // Accent left border on the selected row (over the graph cell, x=0).
                Rectangle {
                    visible: parent.ListView.isCurrentItem
                             || historyList.selectedRows.indexOf(index) >= 0
                    width: 2
                    height: parent.height
                    color: theme.accent
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        historyList.forceActiveFocus()   // arrows work right after a click
                        if (mouse.button === Qt.RightButton) {
                            // Right-clicking inside an existing multi-selection keeps
                            // it (so "Squash N commits…" applies to the whole range);
                            // otherwise collapse to the clicked row.
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
                        } else {
                            if (mouse.modifiers & Qt.ShiftModifier) {
                                var anchor = historyList.currentIndex
                                var lo = Math.max(0, Math.min(anchor, index))
                                var hi = Math.max(anchor, index)
                                var range = []
                                for (var r = lo; r <= hi; ++r) range.push(r)
                                historyList.selectedRows = range
                                historyList.currentIndex = index
                            } else if (mouse.modifiers & Qt.ControlModifier) {
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
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 12
                    spacing: 8

                    GraphColumn {
                        Layout.fillHeight: true
                        Layout.preferredWidth: implicitWidth
                        graphRow: model.graphRow
                        laneColors: theme.laneColors
                        headColor: theme.head
                        laneCount: repoVm && repoVm.history ? repoVm.history.laneCount : 1
                        head: model.isHead
                    }

                    Avatar {
                        name: model.author
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: model.summary
                            color: theme.textPrimary
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

                    // Drag-to-reorder grip — only for commits in the reorderable run
                    // (linear single-parent span from HEAD). Dragging onto another run
                    // row opens a confirmation, then rewrites history via rebase.
                    Label {
                        id: reorderGrip
                        objectName: "reorderGrip"
                        visible: !!repoVm && index < repoVm.reorderableRunLength
                        text: "⠿"
                        color: reorderDrag.active ? theme.accent : theme.textMuted
                        font.pixelSize: 15
                        Layout.preferredWidth: 16
                        Layout.alignment: Qt.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                        ToolTip.text: "Drag to reorder"
                        ToolTip.visible: gripHover.hovered
                        HoverHandler { id: gripHover }
                        DragHandler {
                            id: reorderDrag
                            target: null
                            xAxis.enabled: false
                            onActiveChanged: {
                                if (!active && repoVm) {
                                    var p = reorderGrip.mapToItem(historyList.contentItem,
                                                                  reorderDrag.centroid.position.x,
                                                                  reorderDrag.centroid.position.y)
                                    var to = historyList.indexAt(p.x, p.y)
                                    if (to >= 0 && to < repoVm.reorderableRunLength && to !== index)
                                        reorderConfirm.openFor(index, to)
                                }
                            }
                        }
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
