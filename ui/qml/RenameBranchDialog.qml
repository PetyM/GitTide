import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Rename a local branch. Old name preselected (the current branch).
Dialog {
    id: dialog
    objectName: "renameBranchDialog"
    modal: true
    title: "Rename branch"
    anchors.centerIn: parent
    width: 380
    padding: 20

    property string oldName: ""

    background: Rectangle {
        color: theme.surfaceRaised
        radius: 18
        border.color: theme.border
        border.width: 1
    }

    function openDialog() {
        oldName = repoVm ? repoVm.currentBranch : ""
        newField.text = oldName
        open()
        newField.forceActiveFocus()
        newField.selectAll()
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
        Button {
            text: "Cancel"
            onClicked: dialog.reject()
        }
        Button {
            id: renameButton
            objectName: "renameBranchConfirm"
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
