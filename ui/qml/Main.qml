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

    // False until the startup geometry restore has run. Guards the geometry
    // persistence handlers so the transient Windowed state the window passes
    // through while mapping (which fires onVisibilityChanged/onWidthChanged
    // before Component.onCompleted) cannot clobber the stored geometry before
    // we have read and applied it.
    property bool _restored: false

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
        // Startup restore done — geometry handlers may persist from here on.
        _restored = true
        if (repoVm) repoVm.applyPullDefault(appSettings.pullRebase)
        openFirstRepo()
        // Start the non-active-repo poll if we launch focused (D35).
        if (projectController) projectController.setWindowActive(window.active)
        if (repoVm) repoVm.setWindowActive(window.active)
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
        // Gates the active repo's status-only safety net, which catches in-place
        // file edits no directory watch reports (D35's residual gap).
        if (repoVm) repoVm.setWindowActive(active)
        if (projectController) projectController.setWindowActive(active)
    }

    // Persist geometry. Gated on `_restored` so the transient startup states
    // don't overwrite the stored values before onCompleted has read them. Only
    // save windowed geometry (Windowed) and skip Minimized/Hidden so closing
    // while minimised doesn't persist a tiny/hidden state.
    onXChanged: if (_restored && window.visibility === Window.Windowed) appSettings.windowX = x
    onYChanged: if (_restored && window.visibility === Window.Windowed) appSettings.windowY = y
    onWidthChanged: if (_restored && window.visibility === Window.Windowed) appSettings.windowWidth = width
    onHeightChanged: if (_restored && window.visibility === Window.Windowed) appSettings.windowHeight = height
    onVisibilityChanged: {
        if (_restored && visibility !== Window.Minimized && visibility !== Window.Hidden)
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
            onProjectOptionsRequested: projectOptionsDialog.openDialog()
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
                onAddFromFolderRequested: addFromFolderDialog.openDialog()
                onCloneRequested: cloneRepoDialog.openDialog()
                onInitRequested: initRepoDialog.openDialog()
                onNewProjectRequested: newProjectDialog.openDialog()
                onDeleteProjectRequested: deleteProjectDialog.open()
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
                onAddFromFolderRequested: addFromFolderDialog.openDialog()
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

    // ---- Transient Undo toast (act-then-offer-undo, Plan 47) ----
    // Shown after a clean, drop-free history edit; the Undo button soft-resets the
    // branch back to the pre-edit tip. Auto-dismisses after a few seconds.
    Rectangle {
        id: undoToast
        objectName: "undoToast"
        property string preTip: ""
        property string label: ""
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 52
        width: undoRow.implicitWidth + 24
        height: 36
        radius: 10
        visible: preTip.length > 0
        color: theme.surfaceOverlay
        border.color: theme.border
        border.width: 1
        z: 100

        RowLayout {
            id: undoRow
            anchors.centerIn: parent
            spacing: 12
            Label {
                text: undoToast.label
                color: theme.textPrimary
                font.pixelSize: 12
            }
            AppButton {
                objectName: "undoToastButton"
                variant: "secondary"
                text: "Undo"
                onClicked: {
                    if (repoVm)
                        repoVm.undoHistoryEdit(undoToast.preTip)
                    undoToast.dismiss()
                }
            }
        }

        Timer {
            id: undoTimer
            interval: 6000
            onTriggered: undoToast.dismiss()
        }
        function show(tip, text) {
            preTip = tip
            label = text
            undoTimer.restart()
        }
        function dismiss() {
            preTip = ""
            label = ""
            undoTimer.stop()
        }
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
        function onProjectOptionsRequested() { projectOptionsDialog.openDialog() }
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

    AppDialog {
        id: deleteProjectDialog
        objectName: "deleteProjectDialog"
        title: "Delete project"
        width: 400

        contentItem: Label {
            text: (projectController && projectController.activeProjectName.length > 0)
                  ? ("Remove the project “" + projectController.activeProjectName
                     + "”? The repositories stay on disk — only this grouping is removed.")
                  : "Remove this project?"
            color: theme.textPrimary
            wrapMode: Text.WordWrap
            font.pixelSize: 13
        }

        footer: DialogButtons {
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
    // Reused (rather than duplicated) for the add-from-folder batch's failures —
    // actionVerb switches the title/wording between the two call sites.
    AppDialog {
        id: fetchErrorDialog
        objectName: "fetchErrorDialog"
        title: fetchErrorDialog.actionVerb === "add" ? "Add failed" : "Fetch failed"
        width: 460

        property var failures: []
        property string actionVerb: "fetch" // "fetch" | "add" — selects the wording
        // "add" also covers a failed *source* registration riding in the same
        // list (prefixed "Source: " by the controller) alongside repository
        // failures, so its noun is the neutral "item" rather than "repository" —
        // a source failure must never read as if a repository failed to add.
        readonly property string failureNounSingular: actionVerb === "add" ? "item" : "repository"
        readonly property string failureNounPlural: actionVerb === "add" ? "items" : "repositories"
        function showFailures(list, verb) { failures = list; actionVerb = verb || "fetch"; open() }

        contentItem: DialogColumn {
            spacing: 12
            Label {
                objectName: "fetchErrorHeader"
                Layout.fillWidth: true
                text: fetchErrorDialog.failures.length === 1
                      ? ("One " + fetchErrorDialog.failureNounSingular + " failed to " + fetchErrorDialog.actionVerb + ":")
                      : (fetchErrorDialog.failures.length + " " + fetchErrorDialog.failureNounPlural
                         + " failed to " + fetchErrorDialog.actionVerb + ":")
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

        footer: DialogButtons {
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
                        projectController.addExistingRepo(projectController.localPathFromUrl(selectedFolder))
    }

    AddFromFolderDialog { id: addFromFolderDialog }

    // ---- Transient toast: non-modal, so a source rescan during project switching
    // never interrupts the switch with a dialog. ----
    ToastNotice { id: toastNotice }

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
        // An add-from-folder batch finished with some repos rejected (e.g. already
        // a repo elsewhere, or invalid) — reuse the same error dialog.
        function onReposAdded(added, failures) {
            if (failures.length > 0)
                fetchErrorDialog.showFailures(failures, "add")
        }
        // A source rescan (on project activation) picked up new repositories — a
        // non-blocking toast, since a modal would interrupt the project switch
        // that triggered it.
        function onSourcesRescanned(added, unavailableSources) {
            if (added > 0)
                toastNotice.show(added === 1 ? "1 repository added from a source"
                                             : added + " repositories added from sources")
        }
    }

    Connections {
        target: repoVm
        enabled: repoVm !== null
        function onAuthRequired() { credentialDialog.openDialog() }
        function onOperationFailed(message) { errorBanner.show(message) }
        // A clean, drop-free history edit (reorder / squash) offers a non-blocking
        // Undo instead of an up-front confirm modal (Plan 47).
        function onHistoryEditUndoable(preTipOid, label) { undoToast.show(preTipOid, label) }
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
