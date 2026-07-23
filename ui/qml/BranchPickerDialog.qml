import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Reusable local-branch picker. Lists local branches except the current one
// (remote=false, isHead=false); calls accept() (Dialog.accepted) on confirm.
// Read selectedRef in the onAccepted handler. promptText and actionLabel are
// set by the caller (rebase vs merge).
AppDialog {
    id: dialog
    objectName: "branchPickerDialog"
    width: 380
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// The RepoViewModel — provides the branches model and currentBranch name.
    property var repo

    /// Prompt shown above the list, e.g. "Rebase main onto:".
    property string promptText: ""

    /// Confirm-button label, e.g. "Rebase" / "Merge".
    property string actionLabel: "OK"

    /// Currently highlighted branch name; empty until the user selects one.
    property string selectedRef: ""

    onOpened: dialog.selectedRef = ""

    contentItem: DialogColumn {
        spacing: 12

        Label {
            text: dialog.promptText
            color: theme.textPrimary
            font.pixelSize: 14
            Layout.fillWidth: true
        }

        ListView {
            id: branchList
            objectName: "branchPickerList"
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

    footer: DialogButtons {
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "branchPickerConfirm"
            variant: "primary"
            text: dialog.actionLabel
            enabled: dialog.selectedRef.length > 0
            onClicked: dialog.accept()
        }
    }
}
