import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GitTide 1.0

RowLayout {
    id: historyPane
    objectName: "historyPane"
    spacing: 0

    // ---- Commit context menu (right-click on a history row) ----
    AppMenu {
        id: commitContextMenu
        objectName: "commitContextMenu"
        property string rowBranchName: ""

        AppMenuItem {
            objectName: "mergeIntoItem"
            text: repoVm ? ("Merge into " + repoVm.currentBranch) : "Merge"
            enabled: commitContextMenu.rowBranchName !== ""
            visible: commitContextMenu.rowBranchName !== ""
            onTriggered: if (repoVm && commitContextMenu.rowBranchName !== "")
                             repoVm.startMerge(commitContextMenu.rowBranchName)
        }
    }

    // ---- Commit list (graph + avatar + summary/author/date) ----
    ListView {
        id: historyList
        objectName: "historyList"
        Layout.preferredWidth: 420
        Layout.fillHeight: true
        clip: true
        model: repoVm ? repoVm.history : null

        delegate: Rectangle {
            width: ListView.view.width
            height: 48
            color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

            // Accent left border on the selected row (over the graph cell, x=0).
            Rectangle {
                visible: parent.ListView.isCurrentItem
                width: 2
                height: parent.height
                color: theme.accent
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        historyList.currentIndex = index
                        commitContextMenu.rowBranchName = model.localBranchName ?? ""
                        commitContextMenu.popup()
                    } else {
                        historyList.currentIndex = index
                        if (repoVm) repoVm.selectCommit(model.oid)
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
        objectName: "commitDetail"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
