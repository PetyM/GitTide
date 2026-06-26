import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Create a new project and activate it.
Dialog {
    id: dialog
    objectName: "newProjectDialog"
    modal: true
    title: "New project"
    anchors.centerIn: parent
    width: 380
    padding: 20

    background: OverlayCard {}

    function openDialog() {
        nameField.text = ""
        open()
        nameField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: "Project name"
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: nameField
            objectName: "newProjectName"
            Layout.fillWidth: true
            placeholderText: "My project"
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: nameField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
            Keys.onReturnPressed: if (createButton.enabled) dialog.accept()
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
            id: createButton
            objectName: "newProjectCreate"
            variant: "primary"
            text: "Create"
            enabled: nameField.text.trim().length > 0
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        if (projectController)
            projectController.createProject(nameField.text.trim())
    }

    // Activate the project once the store reports it created.
    Connections {
        target: projectController
        function onProjectCreated(projectId) {
            projectController.activate(projectId)
        }
    }
}
