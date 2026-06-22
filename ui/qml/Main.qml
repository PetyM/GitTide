import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    objectName: "appWindow"
    visible: true
    width: 1100
    height: 720
    title: "GitTide"
    color: theme.surfaceBase

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 272
            onAddExistingRequested: addExistingFolder.open()
            onCloneRequested: cloneRepoDialog.openDialog()
            onInitRequested: initRepoDialog.openDialog()
            onNewProjectRequested: newProjectDialog.openDialog()
            onDeleteProjectRequested: deleteProjectDialog.open()
        }

        WorkingPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            onAddExistingRequested: addExistingFolder.open()
            onCloneRequested: cloneRepoDialog.openDialog()
            onInitRequested: initRepoDialog.openDialog()
            onNewProjectRequested: newProjectDialog.openDialog()
        }
    }

    // ---- Transient error banner (floats over the top of the content) ----
    Rectangle {
        id: errorBanner
        objectName: "errorBanner"
        property string message: ""
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: Math.min(parent.width - 48, bannerLabel.implicitWidth + 32)
        height: 36
        radius: 10
        visible: message.length > 0
        color: theme.surfaceOverlay
        border.color: theme.stateDeleted
        border.width: 1
        z: 100

        Label {
            id: bannerLabel
            anchors.centerIn: parent
            text: errorBanner.message
            color: theme.textPrimary
            font.pixelSize: 12
        }

        Timer {
            id: bannerTimer
            interval: 5000
            onTriggered: errorBanner.message = ""
        }
        onMessageChanged: if (message.length > 0) bannerTimer.restart()

        function show(msg) { message = msg }
    }

    // ---- Dialogs (window-scoped so they centre on the whole window) ----
    InitRepoDialog { id: initRepoDialog }
    CloneRepoDialog {
        id: cloneRepoDialog
        onCloneStarted: cloneProgressDialog.openDialog()
    }
    CloneProgressDialog { id: cloneProgressDialog }
    NewProjectDialog { id: newProjectDialog }
    CredentialDialog {
        id: credentialDialog
        onAccepted: if (repoVm) repoVm.submitCredentials(username, token)
    }

    // ---- Delete-project confirmation ----
    Dialog {
        id: deleteProjectDialog
        objectName: "deleteProjectDialog"
        modal: true
        title: "Delete project"
        anchors.centerIn: parent
        width: 400
        padding: 20
        background: OverlayCard {}

        contentItem: Label {
            text: (projectController && projectController.activeProjectName.length > 0)
                  ? ("Remove the project “" + projectController.activeProjectName
                     + "”? The repositories stay on disk — only this grouping is removed.")
                  : "Remove this project?"
            color: theme.textPrimary
            wrapMode: Text.WordWrap
            font.pixelSize: 13
        }

        footer: RowLayout {
            spacing: 8
            Layout.margins: 16
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: deleteProjectDialog.reject() }
            Button {
                objectName: "deleteProjectConfirm"
                text: "Delete"
                onClicked: deleteProjectDialog.accept()
            }
        }

        onAccepted: if (projectController) projectController.removeProject()
    }

    // Folder picker for "add existing repository".
    FolderDialog {
        id: addExistingFolder
        title: "Choose a repository folder"
        onAccepted: if (projectController)
                        projectController.addExistingRepo(selectedFolder.toString().replace(/^file:\/\//, ""))
    }

    // ---- Auto-open a repository so the main area shows working state ----
    function openFirstRepo() {
        if (!repoVm)
            return
        if (projectController && projectController.activeProjectId.length > 0
                && repoModel && repoModel.rowCount() > 0)
            repoVm.open(repoModel.firstRepoPath())
        else
            repoVm.close() // empty/!active project ⇒ clear stale repo view
    }
    Component.onCompleted: openFirstRepo()

    Connections {
        target: projectController
        enabled: projectController !== null
        function onRepoAdded(path) { if (repoVm) repoVm.open(path) }
        function onActiveProjectChanged() { window.openFirstRepo() }
        function onRepoAddFailed(message) { errorBanner.show(message) }
    }

    Connections {
        target: repoVm
        enabled: repoVm !== null
        function onAuthRequired() { credentialDialog.openDialog() }
        function onOperationFailed(message) { errorBanner.show(message) }
    }
}
