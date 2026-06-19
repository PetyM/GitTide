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
    }

    NewBranchDialog { id: newBranchDialog }
    RenameBranchDialog { id: renameBranchDialog }
    DeleteBranchDialog { id: deleteBranchDialog }
}
