import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Initialize a new repository: choose a parent folder + a name.
AppDialog {
    id: dialog
    objectName: "initRepoDialog"
    title: "Initialize repository"
    width: 420
    padding: 20

    property string parentDir: ""

    function openDialog() {
        parentDir = ""
        nameField.text = ""
        open()
    }

    contentItem: DialogColumn {
        spacing: 12

        Label {
            text: "Parent folder"
            color: theme.textMuted
            font.pixelSize: 11
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: dialog.parentDir.length > 0 ? dialog.parentDir : "No folder chosen"
                color: dialog.parentDir.length > 0 ? theme.textPrimary : theme.textMuted
                elide: Text.ElideMiddle
                font.pixelSize: 12
            }
            AppButton {
                variant: "secondary"
                text: "Choose…"
                onClicked: parentFolder.open()
            }
        }

        Label {
            text: "Repository name"
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: nameField
            objectName: "initRepoName"
            Layout.fillWidth: true
            placeholderText: "my-repo"
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: nameField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
        }
    }

    footer: DialogButtons {
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.reject()
        }
        AppButton {
            objectName: "initRepoCreate"
            variant: "primary"
            text: "Initialize"
            enabled: dialog.parentDir.length > 0 && nameField.text.trim().length > 0
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        if (projectController)
            projectController.initRepo(dialog.parentDir, nameField.text.trim())
    }

    FolderDialog {
        id: parentFolder
        title: "Choose parent folder"
        onAccepted: dialog.parentDir = selectedFolder.toString().replace(/^file:\/\//, "")
    }
}
