import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GitTide 1.0

// Full all-refs git graph with branch/tag chips and read-only commit detail.
// Mirrors HistoryPane in structure but uses repoVm.graph instead of repoVm.history
// and has no drag-to-reorder or history-editing operations.
RowLayout {
    id: graphPane
    spacing: 0

    function takeFocus() { graphList.forceActiveFocus() }
    // No inner detail pane anymore (Graph tab is graph-only); Tab chain entry
    // from either direction lands on the same, only focusable element.
    function takeFocusLast() { graphList.forceActiveFocus() }

    signal tabNext()
    signal tabPrev()
    // Fired when a graph row is double-clicked; WorkingPane switches to History.
    signal commitActivated()

    // Select the row (shared repoVm selection state, same as a single click)
    // then signal a hand-off to History. Exposed as a root-level function so
    // it's reachable both from the delegate's double-tap TapHandler and from
    // tests via QMetaObject::invokeMethod, without simulating a click gesture.
    function activateRow(index) {
        graphList.selectRow(index)
        commitActivated()
    }

    CommitContextMenu {
        id: graphMenu
        // History-editing operations are structurally inapplicable on the all-refs
        // graph (cross-branch, not a single linear run from HEAD).
        allowHistoryEditing: false
        onCopySha:           if (repoVm) repoVm.copyToClipboard(graphMenu.oid)
        onNewBranchFromHere: graphNewBranchDialog.openFromCommit(graphMenu.oid)
        onCheckoutCommit:    if (repoVm) repoVm.checkoutCommit(graphMenu.oid)
        onMerge:             if (repoVm) repoVm.startMerge(graphMenu.localBranchName)
    }

    NewBranchDialog { id: graphNewBranchDialog }

    // ---- Commit list (graph column + ref chips + avatar + summary/author/date) ----
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ListView {
            id: graphList
            objectName: "graphList"
            anchors.fill: parent
            clip: true
            model: repoVm ? repoVm.graph : null

            ScrollBar.vertical: AppScrollBar {}
            WheelScroller {}
            activeFocusOnTab: true

            Keys.onUpPressed: {
                if (currentIndex > 0) {
                    currentIndex--
                    selectRow(currentIndex)
                }
            }
            Keys.onDownPressed: {
                if (currentIndex < count - 1) {
                    currentIndex++
                    selectRow(currentIndex)
                }
            }
            Keys.onTabPressed: {
                graphPane.tabNext()
                event.accepted = true
            }
            Keys.onBacktabPressed: {
                graphPane.tabPrev()
                event.accepted = true
            }

            function selectRow(i) {
                if (repoVm) repoVm.selectGraphCommitAtRow(i)
            }

            delegate: Rectangle {
                width: ListView.view.width
                readonly property int refCount:
                    (typeof refLabels !== "undefined" && refLabels) ? refLabels.length : 0
                readonly property int kRefColW: 120
                readonly property int kRowH: 48
                height: Math.max(kRowH, 8 + refCount * 18) // 8 = top+bottom pad, 18 = chip+gap
                color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

                // Accent left border on the selected row.
                Rectangle {
                    visible: parent.ListView.isCurrentItem
                    width: 2
                    height: parent.height
                    color: theme.accent
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        graphList.forceActiveFocus()
                        graphList.currentIndex = index
                        graphList.selectRow(index)
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        graphList.currentIndex = index
                        graphList.selectRow(index)
                        graphMenu.oid             = model.oid
                        graphMenu.shortOid        = model.shortOid
                        graphMenu.localBranchName = model.localBranchName ?? ""
                        graphMenu.isHead          = model.isHead
                        graphMenu.selectionCount  = 1
                        graphMenu.popup()
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    // This Qt build exposes double-tap detection via the dedicated
                    // doubleTapped signal rather than a `gesture` property (that
                    // TapHandler.gesture/DoubleTap API landed in a later Qt minor).
                    onDoubleTapped: {
                        graphList.currentIndex = index
                        graphPane.activateRow(index)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 0
                    anchors.leftMargin: 8
                    anchors.rightMargin: 12
                    spacing: 8

                    GraphColumn {
                        Layout.fillHeight: true
                        Layout.preferredWidth: implicitWidth
                        graphRow: model.graphRow
                        laneColors: theme.laneColors
                        headColor: theme.head
                        laneCount: repoVm && repoVm.graph ? repoVm.graph.laneCount : 1
                        head: model.isHead
                        localOnly: model.isLocalOnly // hollow dot for unpushed commits
                    }

                    // Branch/tag chips, stacked vertically in a fixed-width column
                    // so the summary text starts at the same X on every row.
                    ColumnLayout {
                        Layout.preferredWidth: kRefColW
                        Layout.minimumWidth: kRefColW
                        Layout.maximumWidth: kRefColW
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: (kRowH - 16) / 2 // align first chip with the first line
                        spacing: 2
                        Repeater {
                            model: (typeof refLabels !== "undefined" && refLabels) ? refLabels : []
                            delegate: Rectangle {
                                radius: 3
                                color: theme.surfaceRaised
                                border.width: 1
                                border.color: theme.border
                                implicitHeight: 16
                                Layout.preferredWidth: Math.min(chipLabel.implicitWidth + 10, kRefColW)
                                Layout.alignment: Qt.AlignLeft
                                Label {
                                    id: chipLabel
                                    anchors.left: parent.left
                                    anchors.leftMargin: 5
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 10
                                    text: modelData
                                    elide: Text.ElideRight
                                    color: theme.textSecondary
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    Avatar {
                        name: model.author
                        email: model.authorEmail
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 12
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 6
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
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: graphList.activeFocus ? theme.focusBorder : "transparent"
            border.width: 1
            enabled: false
        }
    }
}
