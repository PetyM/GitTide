import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Branch picker for the app-menu "Rebase current branch…" route. Lists local
// branches except the current one (model.remote=false, model.isHead=false);
// calls accept() (Dialog built-in accepted()) on Rebase. Read selectedRef
// in the onAccepted handler.
Dialog {
    id: dialog
    objectName: "rebaseTargetDialog"
    modal: true
    title: "Rebase branch"
    anchors.centerIn: parent
    width: 380
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// The RepoViewModel — provides branches model and currentBranch name.
    property var repo

    /// Currently highlighted branch name; empty until user selects one.
    property string selectedRef: ""

    // Reset selection each time the dialog opens.
    onOpened: dialog.selectedRef = ""

    background: OverlayCard {}

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: repo ? ("Rebase " + repo.currentBranch + " onto:") : "Rebase onto:"
            color: theme.textPrimary
            font.pixelSize: 14
            Layout.fillWidth: true
        }

        // Branch list — local non-current branches.
        ListView {
            id: branchList
            objectName: "rebaseTargetList"
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true
            model: repo ? repo.branches : null

            delegate: ItemDelegate {
                id: branchDelegate
                required property string branchName
                required property bool   isHead
                required property bool   remote
                width: branchList.width
                // Filter: show only local (non-remote, non-current) branches.
                // Height 0 prevents gaps for hidden rows.
                visible: !branchDelegate.remote && !branchDelegate.isHead
                height: visible ? implicitHeight : 0
                highlighted: dialog.selectedRef === branchDelegate.branchName
                onClicked: dialog.selectedRef = branchDelegate.branchName
                contentItem: Label {
                    text: branchDelegate.branchName
                    color: branchDelegate.highlighted ? theme.accent : theme.textPrimary
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                background: Rectangle {
                    color: branchDelegate.highlighted ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.15)
                                                      : (branchDelegate.hovered ? theme.surfaceRaised : "transparent")
                    radius: 4
                }
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "rebaseTargetConfirm"
            variant: "primary"
            text: "Rebase"
            enabled: dialog.selectedRef.length > 0
            onClicked: dialog.accept()
        }
    }
}
