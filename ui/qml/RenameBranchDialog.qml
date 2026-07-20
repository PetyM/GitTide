import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Rename a local branch. Old name preselected (the current branch).
AppDialog {
    id: dialog
    objectName: "renameBranchDialog"
    title: "Rename branch"
    width: 380
    padding: 20

    property string oldName: ""

    function openDialog() {
        oldName = repoVm ? repoVm.currentBranch : ""
        newField.text = oldName
        open()
        newField.forceActiveFocus()
        newField.selectAll()
    }

    function openFor(name) {
        oldName = name
        newField.text = name
        open()
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: "Renaming \"" + dialog.oldName + "\""
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: newField
            objectName: "renameBranchName"
            Layout.fillWidth: true
            placeholderText: "New name"
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: newField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
            Keys.onReturnPressed: if (renameButton.enabled) dialog.accept()
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.reject()
        }
        AppButton {
            id: renameButton
            objectName: "renameBranchConfirm"
            variant: "primary"
            text: "Rename"
            enabled: newField.text.trim().length > 0 && newField.text.trim() !== dialog.oldName
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        if (repoVm && dialog.oldName.length > 0)
            repoVm.renameBranch(dialog.oldName, newField.text.trim())
    }
}
