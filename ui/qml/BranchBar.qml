import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: branchBar
    objectName: "branchBar"
    property alias text: branchLabel.text

    implicitHeight: 56
    color: theme.surfaceRaised

    Rectangle { // bottom hairline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        // accent-tinted current-branch chip — opens the branch dropdown
        Rectangle {
            id: branchChip
            objectName: "branchChip"
            Layout.preferredHeight: 36
            Layout.preferredWidth: branchCol.implicitWidth + 40
            radius: 6
            color: chipHover.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.22)
                                     : Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.14)
            border.color: theme.accent
            border.width: 1

            HoverHandler { id: chipHover }
            MouseArea {
                anchors.fill: parent
                onClicked: branchDropdown.open()
            }

            RowLayout {
                anchors.centerIn: parent
                spacing: 8
                ColumnLayout {
                    id: branchCol
                    spacing: 0
                    Label {
                        id: branchLabel
                        text: repoVm ? repoVm.currentBranch : ""
                        color: theme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: "Current branch"
                        color: theme.textMuted
                        font.pixelSize: 11
                    }
                }
                Label { // chevron
                    text: "▾"
                    color: theme.textMuted
                    font.pixelSize: 12
                }
            }

            BranchDropdown {
                id: branchDropdown
                y: branchChip.height + 4
                onNewRequested: newBranchDialog.openDialog()
                onRenameRequested: renameBranchDialog.openDialog()
                onDeleteRequested: deleteBranchDialog.openDialog()
            }
        }

        Item { Layout.fillWidth: true }

        // ---- right-aligned sync cluster ----

        // Fetch
        Button {
            text: "Fetch"
            enabled: repoVm && !repoVm.syncing
            contentItem: Label {
                text: parent.text
                color: theme.textPrimary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 6
                color: parent.hovered ? theme.surfaceOverlay : "transparent"
                border.color: theme.border
                border.width: 1
            }
            onClicked: if (repoVm) repoVm.fetch()
        }

        // Pull (with behind badge) — shown only when there is an upstream
        Item {
            visible: repoVm && repoVm.hasUpstream
            implicitWidth: pullBtn.implicitWidth
            implicitHeight: pullBtn.implicitHeight

            Button {
                id: pullBtn
                text: "Pull"
                enabled: repoVm && !repoVm.syncing
                contentItem: Label {
                    text: parent.text
                    color: theme.textPrimary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    radius: 6
                    color: parent.hovered ? theme.surfaceOverlay : "transparent"
                    border.color: theme.border
                    border.width: 1
                }
                onClicked: if (repoVm) repoVm.pull()
            }

            Rectangle {
                visible: repoVm && repoVm.behindCount > 0
                anchors.right: pullBtn.right
                anchors.top: pullBtn.top
                anchors.topMargin: -4
                anchors.rightMargin: -4
                width: behindLabel.implicitWidth + 8
                height: 16
                radius: 8
                color: theme.accent
                z: 1

                Label {
                    id: behindLabel
                    anchors.centerIn: parent
                    text: repoVm ? repoVm.behindCount : 0
                    color: theme.surfaceBase
                    font.pixelSize: 10
                }
            }
        }

        // Push (with ahead badge) — shown only when there is an upstream
        Item {
            visible: repoVm && repoVm.hasUpstream
            implicitWidth: pushBtn.implicitWidth
            implicitHeight: pushBtn.implicitHeight

            Button {
                id: pushBtn
                text: "Push"
                enabled: repoVm && !repoVm.syncing
                contentItem: Label {
                    text: parent.text
                    color: theme.textPrimary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    radius: 6
                    color: parent.hovered ? theme.surfaceOverlay : "transparent"
                    border.color: theme.border
                    border.width: 1
                }
                onClicked: if (repoVm) repoVm.push()
            }

            Rectangle {
                visible: repoVm && repoVm.aheadCount > 0
                anchors.right: pushBtn.right
                anchors.top: pushBtn.top
                anchors.topMargin: -4
                anchors.rightMargin: -4
                width: aheadLabel.implicitWidth + 8
                height: 16
                radius: 8
                color: theme.accent
                z: 1

                Label {
                    id: aheadLabel
                    anchors.centerIn: parent
                    text: repoVm ? repoVm.aheadCount : 0
                    color: theme.surfaceBase
                    font.pixelSize: 10
                }
            }
        }

        // Publish — shown only when on a real branch with no upstream
        Button {
            visible: repoVm && !repoVm.hasUpstream && repoVm.onBranch
            text: "Publish branch"
            enabled: repoVm && !repoVm.syncing
            contentItem: Label {
                text: parent.text
                color: theme.textPrimary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 6
                color: parent.hovered ? theme.surfaceOverlay : "transparent"
                border.color: theme.border
                border.width: 1
            }
            onClicked: if (repoVm) repoVm.publishBranch()
        }

        // Busy spinner
        BusyIndicator {
            running: repoVm && repoVm.syncing
            visible: running
            implicitWidth: 20
            implicitHeight: 20
        }

        // Pull-strategy toggle
        Button {
            text: "⋯"
            enabled: repoVm !== null
            contentItem: Label {
                text: parent.text
                color: theme.textPrimary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 6
                color: parent.hovered ? theme.surfaceOverlay : "transparent"
                border.color: theme.border
                border.width: 1
            }
            onClicked: pullMenu.open()

            Menu {
                id: pullMenu
                MenuItem {
                    text: "Pull: rebase"
                    checkable: true
                    checked: repoVm && repoVm.pullRebase
                    onToggled: if (repoVm) repoVm.setPullRebase(checked)
                }
            }
        }
    }

    NewBranchDialog { id: newBranchDialog }
    RenameBranchDialog { id: renameBranchDialog }
    DeleteBranchDialog { id: deleteBranchDialog }
}
