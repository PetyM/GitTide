import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: window
    objectName: "appWindow"
    visible: true
    flags: Qt.FramelessWindowHint | Qt.Window
    minimumWidth: 860
    minimumHeight: 560
    color: theme.surfaceBase

    // App-level settings: persisted via QSettings (platform-native storage).
    // themeMode: 0=System 1=Dark 2=Light; default System.
    // pullRebase: global pull strategy; default true (rebase).
    // Window geometry: restored on startup; default is Maximized on first run.
    Settings {
        id: appSettings
        property int themeMode: 0
        property bool pullRebase: true
        property int windowX: 0
        property int windowY: 0
        property int windowWidth: 1100
        property int windowHeight: 720
        property int windowVisibility: Window.Maximized
    }

    Component.onCompleted: {
        if (appSettings.windowVisibility === Window.Maximized) {
            window.showMaximized()
        } else {
            window.x = appSettings.windowX
            window.y = appSettings.windowY
            window.width = appSettings.windowWidth
            window.height = appSettings.windowHeight
            window.showNormal()
        }
        if (repoVm) repoVm.applyPullDefault(appSettings.pullRebase)
        openFirstRepo()
        // Start the non-active-repo poll if we launch focused (D35).
        if (projectController) projectController.setWindowActive(window.active)
    }

    // Live refresh on focus-in (D35): re-sync the active repo (catches in-place
    // edits the directory watcher can miss) and gate the fleet poll on focus.
    onActiveChanged: {
        if (active && repoVm) repoVm.resync()
        if (projectController) projectController.setWindowActive(active)
    }

    // Persist geometry (skip Minimized so closing while minimised doesn't restore tiny)
    onXChanged: if (window.visibility === Window.Windowed) appSettings.windowX = x
    onYChanged: if (window.visibility === Window.Windowed) appSettings.windowY = y
    onWidthChanged: if (window.visibility === Window.Windowed) appSettings.windowWidth = width
    onHeightChanged: if (window.visibility === Window.Windowed) appSettings.windowHeight = height
    onVisibilityChanged: {
        if (visibility !== Window.Minimized && visibility !== Window.Hidden)
            appSettings.windowVisibility = visibility
    }

    // Propagate pull-default changes from OptionsDialog to the view model
    Connections {
        target: appSettings
        function onPullRebaseChanged() {
            if (repoVm) repoVm.applyPullDefault(appSettings.pullRebase)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TitleBar {
            id: titleBar
            Layout.fillWidth: true
            onOptionsRequested: optionsDialog.open()
            onAboutRequested: aboutDialog.open()
            onRebaseRequested: rebaseTargetDialog.open()
            onUndoLastCommitRequested: if (repoVm) repoVm.undoLastCommit()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
    }

    // ---- Edge resize zones (7 zones — no top: title bar drag covers it) ----
    // Left
    EdgeResizer {
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 4
        edges: Qt.LeftEdge
        active: window.visibility !== Window.Maximized
    }
    // Right
    EdgeResizer {
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 4
        edges: Qt.RightEdge
        active: window.visibility !== Window.Maximized
    }
    // Bottom
    EdgeResizer {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 4
        edges: Qt.BottomEdge
        active: window.visibility !== Window.Maximized
    }
    // Bottom-left corner
    EdgeResizer {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        width: 10; height: 10
        edges: Qt.LeftEdge | Qt.BottomEdge
        active: window.visibility !== Window.Maximized
    }
    // Bottom-right corner
    EdgeResizer {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 10; height: 10
        edges: Qt.RightEdge | Qt.BottomEdge
        active: window.visibility !== Window.Maximized
    }
    // Top-left corner
    EdgeResizer {
        anchors.left: parent.left; anchors.top: parent.top
        width: 10; height: 10
        edges: Qt.LeftEdge | Qt.TopEdge
        active: window.visibility !== Window.Maximized
    }
    // Top-right corner
    EdgeResizer {
        anchors.right: parent.right; anchors.top: parent.top
        width: 10; height: 10
        edges: Qt.RightEdge | Qt.TopEdge
        active: window.visibility !== Window.Maximized
    }

    // ---- Transient error banner ----
    Rectangle {
        id: errorBanner
        objectName: "errorBanner"
        property string message: ""
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 52
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

    // ---- App dialogs ----
    OptionsDialog {
        id: optionsDialog
        appSettings: appSettings
    }
    AboutDialog { id: aboutDialog }
    RebaseTargetDialog {
        id: rebaseTargetDialog
        repo: repoVm
        onAccepted: if (repoVm) repoVm.startRebase(rebaseTargetDialog.selectedRef)
    }

    // ---- Repo-management dialogs (window-scoped so they centre on the whole window) ----
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

    FolderDialog {
        id: addExistingFolder
        title: "Choose a repository folder"
        onAccepted: if (projectController)
                        projectController.addExistingRepo(selectedFolder.toString().replace(/^file:\/\//, ""))
    }

    // ---- Auto-open a repository ----
    function openFirstRepo() {
        if (!repoVm) return
        if (projectController && projectController.activeProjectId.length > 0
                && repoModel && repoModel.rowCount() > 0)
            repoVm.open(repoModel.firstRepoPath())
        else
            repoVm.close()
    }

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
