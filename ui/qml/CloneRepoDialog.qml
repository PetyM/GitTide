import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Clone a repository from a URL into a chosen destination folder.
AppDialog {
    id: dialog
    objectName: "cloneRepoDialog"
    title: "Clone repository"
    width: 460
    padding: 20

    property string destDir: ""
    // Emitted when a clone is kicked off, so the host can show progress.
    signal cloneStarted()

    function openDialog() {
        urlField.text = ""
        destDir = ""
        open()
        urlField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: "Remote URL"
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: urlField
            objectName: "cloneUrl"
            Layout.fillWidth: true
            placeholderText: "https://github.com/owner/repo.git"
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: urlField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
        }

        Label {
            text: "Destination folder"
            color: theme.textMuted
            font.pixelSize: 11
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: dialog.destDir.length > 0 ? dialog.destDir : "No folder chosen"
                color: dialog.destDir.length > 0 ? theme.textPrimary : theme.textMuted
                elide: Text.ElideMiddle
                font.pixelSize: 12
            }
            AppButton {
                variant: "secondary"
                text: "Choose…"
                onClicked: destFolder.open()
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
            onClicked: dialog.reject()
        }
        AppButton {
            objectName: "cloneConfirm"
            variant: "primary"
            text: "Clone"
            enabled: urlField.text.trim().length > 0 && dialog.destDir.length > 0
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        if (projectController) {
            projectController.startClone(urlField.text.trim(), dialog.destDir)
            dialog.cloneStarted()
        }
    }

    FolderDialog {
        id: destFolder
        title: "Choose destination folder"
        onAccepted: dialog.destDir = selectedFolder.toString().replace(/^file:\/\//, "")
    }
}
