import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: window
    objectName: "appWindow"
    visible: true
    // macOS uses native window decorations (real traffic lights + native
    // fullscreen); Windows/Linux stay frameless with the custom TitleBar.
    readonly property bool isMac: Qt.platform.os === "osx"
    flags: isMac ? Qt.Window : (Qt.FramelessWindowHint | Qt.Window)
    // Shown in the native macOS title bar; the custom TitleBar is hidden there.
    title: "GitTide"
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
            window.restoreGeometry()
            window.showNormal()
        }
        if (repoVm) repoVm.applyPullDefault(appSettings.pullRebase)
        openFirstRepo()
        // Start the non-active-repo poll if we launch focused (D35).
        if (projectController) projectController.setWindowActive(window.active)
    }

    // Restore the saved windowed geometry, clamped to the current screen's
    // available area (`Screen.desktopAvailable*`, offset by the virtual-desktop
    // origin). Guards against a window saved on a now-absent or rearranged
    // monitor — or with stale negative coordinates — launching partly off-screen.
    function restoreGeometry() {
        var availW = Screen.desktopAvailableWidth
        var availH = Screen.desktopAvailableHeight
        var w = Math.max(minimumWidth, Math.min(appSettings.windowWidth, availW))
        var h = Math.max(minimumHeight, Math.min(appSettings.windowHeight, availH))
        var minX = Screen.virtualX
        var minY = Screen.virtualY
        var maxX = minX + availW - w
        var maxY = minY + availH - h
        width = w
        height = h
        x = Math.min(Math.max(appSettings.windowX, minX), Math.max(minX, maxX))
        y = Math.min(Math.max(appSettings.windowY, minY), Math.max(minY, maxY))
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
            // Hidden on macOS — the native title bar owns window controls and the
            // menu moves to the system menu bar (see nativeMenuLoader). Kept
            // instantiated so the shared action wiring/tests still resolve.
            visible: !window.isMac
            Layout.fillWidth: true
            appSettings: appSettings
            onOptionsRequested: optionsDialog.open()
            onAboutRequested: aboutDialog.open()
            onRebaseRequested: rebaseTargetDialog.open()
            onUndoLastCommitRequested: if (repoVm) repoVm.undoLastCommit()
            onOpenRepoFolderRequested: if (repoVm) repoVm.openRepoFolder()
            onDiscardAllRequested: discardAllDialog.open()
            onMergeRequested: mergeTargetDialog.open()
            onStashRequested: if (repoVm) repoVm.stashChanges()
            onPopStashRequested: if (repoVm) repoVm.popStash()
        }

        // Resizable seam between the repo tree and the changes/history/graph pane.
        // The draggable handle (accent on hover) doubles as the divider.
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 3
                color: mainHandleHover.hovered ? theme.accent : theme.border
                HoverHandler { id: mainHandleHover }
            }

            Sidebar {
                id: sidebar
                SplitView.preferredWidth: 272
                SplitView.minimumWidth: 200
                onAddExistingRequested: addExistingFolder.open()
                onCloneRequested: cloneRepoDialog.openDialog()
                onInitRequested: initRepoDialog.openDialog()
                onNewProjectRequested: newProjectDialog.openDialog()
                onDeleteProjectRequested: deleteProjectDialog.open()
                onProjectOptionsRequested: projectOptionsDialog.openDialog()
                // Tab cycle: repo tree → working pane (forward) / its last element
                // (reverse).
                onTabNext: workingPane.takeFocus()
                onTabPrev: workingPane.takeFocusLast()
            }

            WorkingPane {
                id: workingPane
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                onAddExistingRequested: addExistingFolder.open()
                onCloneRequested: cloneRepoDialog.openDialog()
                onInitRequested: initRepoDialog.openDialog()
                onNewProjectRequested: newProjectDialog.openDialog()
                // Tab cycle: leaving the pane in either direction wraps to the
                // sidebar repo tree.
                onFocusSidebar: sidebar.takeFocus()
            }
        }
    }

    // ---- Edge resize zones (7 zones — no top: title bar drag covers it) ----
    // Left
    EdgeResizer {
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 4
        edges: Qt.LeftEdge
        active: !window.isMac && window.visibility !== Window.Maximized
    }
    // Right
    EdgeResizer {
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 4
        edges: Qt.RightEdge
        active: !window.isMac && window.visibility !== Window.Maximized
    }
    // Bottom
    EdgeResizer {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 4
        edges: Qt.BottomEdge
        active: !window.isMac && window.visibility !== Window.Maximized
    }
    // Bottom-left corner
    EdgeResizer {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        width: 10; height: 10
        edges: Qt.LeftEdge | Qt.BottomEdge
        active: !window.isMac && window.visibility !== Window.Maximized
    }
    // Bottom-right corner
    EdgeResizer {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 10; height: 10
        edges: Qt.RightEdge | Qt.BottomEdge
        active: !window.isMac && window.visibility !== Window.Maximized
    }
    // Top-left corner
    EdgeResizer {
        anchors.left: parent.left; anchors.top: parent.top
        width: 10; height: 10
        edges: Qt.LeftEdge | Qt.TopEdge
        active: !window.isMac && window.visibility !== Window.Maximized
    }
    // Top-right corner
    EdgeResizer {
        anchors.right: parent.right; anchors.top: parent.top
        width: 10; height: 10
        edges: Qt.RightEdge | Qt.TopEdge
        active: !window.isMac && window.visibility !== Window.Maximized
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

    // ---- Native macOS menu bar ----
    // Only on macOS: the menu lives in the system menu bar instead of the custom
    // TitleBar (which is hidden there). Its signals bind to the same handlers as
    // the TitleBar/AppMenuBar above.
    Loader {
        id: nativeMenuLoader
        active: window.isMac
        source: "NativeMenuBar.qml"
        onLoaded: {
            item.appSettings = appSettings
            item.repo = repoVm
        }
    }
    Connections {
        target: nativeMenuLoader.item
        ignoreUnknownSignals: true
        function onOptionsRequested() { optionsDialog.open() }
        function onAboutRequested() { aboutDialog.open() }
        function onOpenRepoFolderRequested() { if (repoVm) repoVm.openRepoFolder() }
        function onUndoLastCommitRequested() { if (repoVm) repoVm.undoLastCommit() }
        function onDiscardAllRequested() { discardAllDialog.open() }
        function onMergeRequested() { mergeTargetDialog.open() }
        function onRebaseRequested() { rebaseTargetDialog.open() }
        function onStashRequested() { if (repoVm) repoVm.stashChanges() }
        function onPopStashRequested() { if (repoVm) repoVm.popStash() }
    }

    // ---- App dialogs ----
    OptionsDialog {
        id: optionsDialog
        appSettings: appSettings
    }
    ProjectOptionsDialog {
        id: projectOptionsDialog
    }
    AboutDialog { id: aboutDialog }
    BranchPickerDialog {
        id: rebaseTargetDialog
        repo: repoVm
        title: "Rebase branch"
        actionLabel: "Rebase"
        promptText: repoVm ? ("Rebase " + repoVm.currentBranch + " onto:") : "Rebase onto:"
        onAccepted: if (repoVm) repoVm.startRebase(rebaseTargetDialog.selectedRef)
    }
    BranchPickerDialog {
        id: mergeTargetDialog
        objectName: "mergeTargetDialog"
        repo: repoVm
        title: "Merge branch"
        actionLabel: "Merge"
        promptText: repoVm ? ("Merge selected branch into " + repoVm.currentBranch + ":")
                           : "Merge into current branch:"
        onAccepted: if (repoVm) repoVm.startMerge(mergeTargetDialog.selectedRef)
    }
    DiscardChangesDialog {
        id: discardAllDialog
        objectName: "discardAllDialog"
        fileName: "all working-tree changes"
        onAccepted: if (repoVm) repoVm.discardAll()
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
            AppButton { variant: "secondary"; text: "Cancel"; onClicked: deleteProjectDialog.reject() }
            AppButton {
                objectName: "deleteProjectConfirm"
                variant: "danger"
                text: "Delete"
                onClicked: deleteProjectDialog.accept()
            }
        }

        onAccepted: if (projectController) projectController.removeProject()
    }

    // Fleet-fetch error report — lists the repos that failed (non-auth) so the
    // user sees why, in place of the old passing "N fetched, M failed" caption.
    Dialog {
        id: fetchErrorDialog
        objectName: "fetchErrorDialog"
        modal: true
        title: "Fetch failed"
        anchors.centerIn: parent
        width: 460
        padding: 20
        background: OverlayCard {}

        property var failures: []
        function showFailures(list) { failures = list; open() }

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: fetchErrorDialog.failures.length === 1
                      ? "One repository failed to fetch:"
                      : (fetchErrorDialog.failures.length + " repositories failed to fetch:")
                color: theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(failuresText.implicitHeight, 220)
                clip: true
                Label {
                    id: failuresText
                    width: fetchErrorDialog.availableWidth
                    text: fetchErrorDialog.failures.join("\n")
                    color: theme.textSecondary
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
        }

        footer: RowLayout {
            spacing: 8
            Layout.margins: 16
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "fetchErrorClose"
                variant: "secondary"
                text: "Close"
                onClicked: fetchErrorDialog.close()
            }
        }
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
                && repoModel && repoModel.rowCount() > 0) {
            // Reopen the repo/subrepo left active last session; fall back to the
            // first repo when there is no (valid) stored hint.
            var last = projectController.lastActiveRepo()
            repoVm.open(last.length > 0 ? last : repoModel.firstRepoPath())
        } else {
            repoVm.close()
        }
    }

    Connections {
        target: projectController
        enabled: projectController !== null
        function onRepoAdded(path) { if (repoVm) repoVm.open(path) }
        function onActiveProjectChanged() { window.openFirstRepo() }
        function onRepoAddFailed(message) { errorBanner.show(message) }
        function onSubmoduleOpFailed(repoPath, submodulePath, message) { errorBanner.show(message) }
        // A fleet fetch finished with hard (non-auth) failures — surface them in a
        // dialog rather than a passing status line.
        function onFleetFetchFailed(failures) { fetchErrorDialog.showFailures(failures) }
    }

    Connections {
        target: repoVm
        enabled: repoVm !== null
        function onAuthRequired() { credentialDialog.openDialog() }
        function onOperationFailed(message) { errorBanner.show(message) }
        // Remember the open repo/subrepo so the next launch restores it.
        function onChanged() {
            if (projectController && repoVm.repoOpen && repoVm.repoPath.length > 0)
                projectController.setActiveRepo(repoVm.repoPath)
        }
        // Refresh the sidebar submodule tree when the git-dir watcher fires a full
        // refresh (e.g. external `git submodule update` in a terminal). Non-active
        // repos are covered by the fleet poll in ProjectController::pollRepos.
        function onRepoStructureChanged() {
            if (repoVm.repoOpen && projectController)
                projectController.refreshSubmodules(repoVm.repoPath)
        }
    }
}
