import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: sidebar
    objectName: "sidebar"
    color: theme.surfaceRaised

    // Add-repo requests bubble up to the host (Main), which owns the dialogs so
    // they centre on the window rather than this narrow rail.
    signal addExistingRequested()
    signal cloneRequested()
    signal initRequested()
    signal newProjectRequested()
    signal deleteProjectRequested()

    // Tab handoff with the working pane — repoTree is the sidebar's single stop in
    // the global Tab cycle. tabNext fires on Tab, tabPrev on Shift+Tab.
    signal tabNext()
    signal tabPrev()
    function takeFocus() { repoTree.forceActiveFocus() }

    // Reveal the active repo/subrepo in the tree when it changes — e.g. the repo
    // restored on launch, or a submodule that sits collapsed under its parent.
    // Tracks the last revealed path so an ordinary status refresh (which also
    // fires repoVm.changed) doesn't fight a manual collapse of the active subtree.
    property string revealedRepoPath: ""
    function revealActiveRepo() {
        if (!repoVm || !repoVm.repoOpen || repoVm.repoPath.length === 0)
            return
        var idx = repoModel.indexForRepoPath(repoVm.repoPath)
        if (idx.row < 0)
            return
        repoTree.expandToIndex(idx)
        repoTree.forceLayout()
    }
    Connections {
        target: repoVm
        enabled: repoVm !== null
        function onChanged() {
            if (!repoVm || !repoVm.repoOpen || repoVm.repoPath === sidebar.revealedRepoPath)
                return
            sidebar.revealedRepoPath = repoVm.repoPath
            Qt.callLater(sidebar.revealActiveRepo)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.margins: 16
            spacing: 10

            Item { Layout.fillWidth: true }

            // Fetch-all action: runs fetch on every repo in the active project.
            Button {
                objectName: "fetchAllButton"
                flat: true
                implicitWidth: 28
                implicitHeight: 28
                enabled: projectController !== null
                         && projectController.activeProjectId.length > 0
                         && !projectController.fetchingAll
                ToolTip.visible: hovered
                ToolTip.text: "Fetch all repositories"
                contentItem: Label {
                    text: "⟳"
                    color: theme.textSecondary
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: parent.hovered ? theme.surfaceOverlay : "transparent"
                }
                onClicked: if (projectController) projectController.fetchAll()
            }

            BusyIndicator {
                running: projectController !== null && projectController.fetchingAll
                visible: running
                implicitWidth: 16
                implicitHeight: 16
            }

            Label {
                objectName: "fetchSummary"
                visible: projectController !== null
                         && projectController.fetchSummary.length > 0
                         && !projectController.fetchingAll
                text: projectController ? projectController.fetchSummary : ""
                color: theme.textMuted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

        }

        // ---- Project switcher (combo with inline New/Delete items) ----
        Button {
            id: projectSwitcher
            objectName: "projectSwitcher"
            visible: projectController !== null
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 6
            flat: true
            contentItem: RowLayout {
                spacing: 8
                Label {
                    text: (projectController && projectController.activeProjectName.length > 0)
                          ? projectController.activeProjectName : "No project"
                    color: theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: "▾"
                    color: theme.textSecondary
                    font.pixelSize: 12
                }
            }
            background: Rectangle {
                radius: 8
                color: projectSwitcher.hovered ? theme.surfaceOverlay : theme.surfaceBase
                border.color: theme.border
                border.width: 1
            }
            onClicked: projectMenu.popup()
        }

        AppMenu {
            id: projectMenu
            objectName: "projectMenu"
            // One item per project, kept in sync with the model, then a
            // separator and the New/Delete actions.
            Instantiator {
                model: projectModel
                delegate: AppMenuItem {
                    text: model.display
                    onTriggered: if (projectController) projectController.activate(model.projectId)
                }
                onObjectAdded: (index, object) => projectMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => projectMenu.removeItem(object)
            }
            MenuSeparator {
                padding: 6
                contentItem: Rectangle { implicitHeight: 1; color: theme.border }
            }
            AppMenuItem {
                objectName: "newProjectItem"
                text: "New project…"
                onTriggered: sidebar.newProjectRequested()
            }
            AppMenuItem {
                objectName: "deleteProjectItem"
                text: "Delete current project…"
                enabled: projectController && projectController.activeProjectId.length > 0
                onTriggered: sidebar.deleteProjectRequested()
            }
        }

        // ---- Repo tree ----
        TreeView {
            id: repoTree
            objectName: "repoTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            model: repoModel

            ScrollBar.vertical: AppScrollBar {}
            WheelScroller {}

            // TreeView sizes its column to content by default, which leaves the
            // row (and its active-repo highlight) only as wide as the text. Force
            // the single column to span the full view width instead.
            columnWidthProvider: function (column) { return width }
            onWidthChanged: forceLayout()

            // ---- Keyboard navigation (Tab focus chain + arrows/Enter) ----
            // navRow is the keyboard cursor over visible (flattened) rows; the
            // delegate paints a focus ring on it while the tree holds focus.
            activeFocusOnTab: true
            property int navRow: -1

            onActiveFocusChanged: if (activeFocus && navRow < 0 && rows > 0) navRow = 0

            // Open the repo under the cursor — same effect as clicking the row.
            function activateNav() {
                if (navRow < 0 || navRow >= rows)
                    return
                positionViewAtRow(navRow, TableView.Contain)
                var item = itemAtCell(Qt.point(0, navRow))
                if (item)
                    item.activate()
            }

            Keys.onUpPressed: if (navRow > 0) { navRow--; positionViewAtRow(navRow, TableView.Contain) }
            Keys.onDownPressed: if (navRow < rows - 1) { navRow++; positionViewAtRow(navRow, TableView.Contain) }
            Keys.onLeftPressed: if (navRow >= 0 && isExpanded(navRow)) collapse(navRow)
            Keys.onRightPressed: if (navRow >= 0 && !isExpanded(navRow)) expand(navRow)
            Keys.onReturnPressed: activateNav()
            Keys.onEnterPressed: activateNav()
            Keys.onSpacePressed: activateNav()
            Keys.onTabPressed: { sidebar.tabNext(); event.accepted = true }
            Keys.onBacktabPressed: { sidebar.tabPrev(); event.accepted = true }

            // Rows start collapsed; opening a repo (row click) expands it, and the
            // chevron toggles any subtree manually.

            delegate: TreeViewDelegate {
                id: row
                implicitHeight: 30
                indentation: 16
                // Drop the built-in indicator: its tap-to-toggle proved unreliable
                // here. We draw our own chevron in the content row with a MouseArea
                // that toggles expansion directly (row clicks already work).
                indicator: null

                // Selecting a repo — or an initialised submodule — opens it as a
                // first-class repo and expands its subtree (rows are collapsed by
                // default). The chevron still toggles it manually afterwards. An
                // uninitialised submodule has no repo on disk, so it does nothing.
                // Shared by mouse clicks and keyboard activation (Enter/Space).
                function activate() {
                    if (!repoVm || row.uninit)
                        return
                    repoVm.open(model.repoPath)
                    if (row.hasChildren)
                        repoTree.expand(row.row)
                }
                onClicked: {
                    repoTree.navRow = row.row
                    row.activate()
                }

                readonly property bool isSub: model.isSubmodule === true
                readonly property bool uninit: isSub && model.status === 2
                // The repository currently open in the working pane — the row the
                // user is acting on. Matched by path so it tracks auto-open too,
                // not just click-selection; a submodule opened as a repo lights up
                // the same way.
                readonly property bool activeRepo: repoVm && repoVm.repoOpen
                                                   && model.repoPath === repoVm.repoPath

                contentItem: RowLayout {
                    spacing: 8

                    // Expand/collapse chevron. Fixed-width so leaf rows (no
                    // children) keep their name aligned with expandable ones. Its
                    // own MouseArea toggles the subtree and swallows the click so
                    // the row's repo-open handler doesn't also fire.
                    Item {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        Layout.alignment: Qt.AlignVCenter
                        Label {
                            anchors.centerIn: parent
                            visible: row.hasChildren
                            text: row.expanded ? "▾" : "▸"
                            color: theme.textSecondary
                            font.pixelSize: 10
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: row.hasChildren
                            onClicked: repoTree.toggleExpanded(row.row)
                        }
                    }

                    Label {
                        // The model already resolves the display name (alias or
                        // directory basename) for both repos and submodules.
                        text: model.display
                        color: (model.missing || row.uninit) ? theme.textMuted : theme.textPrimary
                        font.pixelSize: 13
                        font.weight: row.activeRepo ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Submodule: pinned short OID (mono) — hidden when uninitialised.
                    Label {
                        visible: row.isSub && !row.uninit
                        text: model.shortOid
                        color: theme.textMuted
                        font.family: "monospace"
                        font.pixelSize: 11
                    }

                    // Submodule: status dot (dirty amber / clean green @0.55).
                    Rectangle {
                        visible: row.isSub && !row.uninit
                        implicitWidth: 7
                        implicitHeight: 7
                        radius: 3.5
                        color: model.status === 1 ? theme.stateModified : theme.stateAdded
                        opacity: model.status === 1 ? 1.0 : 0.55
                    }

                    // Submodule: inline initialise affordance (uninitialised only).
                    ToolButton {
                        visible: row.uninit && !model.submoduleBusy
                        text: "Init"
                        font.pixelSize: 10
                        onClicked: { if (projectController) projectController.initSubmodule(model.ownerRepoPath, model.repoPath) }
                    }
                    // Spinner while an op runs on this row.
                    BusyIndicator {
                        running: model.submoduleBusy === true
                        visible: running
                        implicitWidth: 14
                        implicitHeight: 14
                    }

                    // Repository: missing-on-disk warning.
                    Label {
                        visible: !row.isSub && model.missing === true
                        text: "⚠"
                        color: theme.stateModified
                    }

                    // Fetch status: spinner while running, then a result glyph.
                    // fetchState: 0 Idle, 1 Running, 2 UpToDate, 3 Updated, 4 Failed.
                    BusyIndicator {
                        running: model.fetchState === 1
                        visible: running
                        implicitWidth: 14
                        implicitHeight: 14
                    }
                    Label {
                        visible: model.fetchState === 3 && model.behind > 0
                        text: "↓" + model.behind
                        color: theme.accent
                        font.pixelSize: 11
                    }
                    Label {
                        visible: model.fetchState === 2
                        text: "✓"
                        color: theme.textMuted
                        font.pixelSize: 11
                    }
                    Label {
                        id: fetchFailedLabel
                        visible: model.fetchState === 4
                        text: "!"
                        color: theme.stateDeleted
                        font.pixelSize: 11
                        ToolTip.text: model.fetchError
                        ToolTip.visible: fetchFailHover.hovered
                        HoverHandler { id: fetchFailHover }
                    }
                }

                background: Rectangle {
                    color: row.activeRepo ? theme.surfaceBase
                         : row.hovered ? theme.surfaceOverlay : "transparent"
                    radius: 10
                    // Keyboard-cursor focus ring — only while the tree holds focus.
                    Rectangle {
                        visible: repoTree.activeFocus && repoTree.navRow === row.row
                        anchors.fill: parent
                        color: "transparent"
                        radius: parent.radius
                        border.color: theme.focusBorder
                        border.width: 1
                    }
                    // Divider above each top-level repo after the first.
                    Rectangle {
                        visible: !row.isSub && row.row > 0
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 4
                        height: 1
                        color: theme.border
                        opacity: 0.5
                    }
                    // Active-repo accent border (repos) at x=0.
                    Rectangle {
                        visible: row.activeRepo
                        width: 2
                        height: parent.height
                        color: theme.accent
                    }
                    // Submodule guide rail + elbow connector.
                    Rectangle {
                        visible: row.isSub
                        x: row.depth * row.indentation - 8
                        width: 1
                        height: parent.height
                        color: theme.border
                    }
                    Rectangle {
                        visible: row.isSub
                        x: row.depth * row.indentation - 8
                        y: parent.height / 2
                        width: 7
                        height: 1
                        color: theme.border
                    }
                }

                // Right-click → context menu (submodule rows open SubmoduleContextMenu;
                // top-level repo rows open RepoContextMenu).
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        if (row.isSub) {
                            submoduleContextMenu.ownerRepoPath = model.ownerRepoPath
                            submoduleContextMenu.submodulePath = model.repoPath
                            submoduleContextMenu.status        = model.status
                            submoduleContextMenu.popup()
                        } else {
                            repoContextMenu.repoPath = model.repoPath
                            repoContextMenu.popup()
                        }
                    }
                }
            }
        }

        Button {
            objectName: "addRepoButton"
            Layout.fillWidth: true
            Layout.margins: 8
            text: "Add repository"
            flat: true
            contentItem: Label {
                text: parent.text
                color: theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 10
                color: "transparent"
                border.color: theme.border
                border.width: 1
            }
            onClicked: addRepoMenu.popup()
        }
    }

    // ---- Fleet credential prompt ----
    // Opened once per fleet-fetch run when authRequired fires; on accept the
    // controller retries all auth-failed rows with the supplied credentials.
    Connections {
        target: projectController
        function onAuthRequired() { fleetCredentialDialog.openDialog() }
    }

    CredentialDialog {
        id: fleetCredentialDialog
        onAccepted: projectController.submitFleetCredentials(username, token)
    }

    // ---- Add repository menu ----
    AppMenu {
        id: addRepoMenu
        objectName: "addRepoMenu"
        AppMenuItem { text: "Add existing repository…"; onTriggered: sidebar.addExistingRequested() }
        AppMenuItem { text: "Initialize new repository…"; onTriggered: sidebar.initRequested() }
        AppMenuItem { text: "Clone repository…"; onTriggered: sidebar.cloneRequested() }
    }

    // ---- Remove-repo context menu ----
    RepoContextMenu {
        id: repoContextMenu
        onRevealInFileManager:  if (repoVm) repoVm.revealInFileManager(repoContextMenu.repoPath)
        onRemoveFromProject:    if (projectController && repoContextMenu.repoPath.length > 0)
                                   projectController.removeRepo(repoContextMenu.repoPath)
        onUpdateAllSubmodules:  if (projectController) projectController.updateAllSubmodules(repoContextMenu.repoPath)
    }

    // ---- Submodule context menu ----
    SubmoduleContextMenu {
        id: submoduleContextMenu
        onInitRequested:      if (projectController) projectController.initSubmodule(ownerRepoPath, submodulePath)
        onUpdateAllRequested: if (projectController) projectController.updateAllSubmodules(submodulePath)
        onDeinitRequested:    if (projectController) projectController.deinitSubmodule(ownerRepoPath, submodulePath)
    }
}
