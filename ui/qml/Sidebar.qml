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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.margins: 16
            spacing: 10
            Image {
                source: theme.iconSource
                sourceSize.width: 26
                sourceSize.height: 26
            }
            Label {
                text: "GitTide"
                color: theme.textPrimary
                font.pixelSize: 16
                font.weight: Font.Bold
            }

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
                    text: "↓↑"
                    color: theme.textSecondary
                    font.pixelSize: 11
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
            }

            // Theme toggle: cycles System → Dark → Light. The glyph reflects the
            // chosen mode (☾ dark / ☀ light / ◐ follow-system).
            Button {
                id: themeToggle
                objectName: "themeToggle"
                flat: true
                implicitWidth: 28
                implicitHeight: 28
                ToolTip.visible: hovered
                ToolTip.text: theme.mode === 1 ? "Theme: Dark (click for Light)"
                            : theme.mode === 2 ? "Theme: Light (click for System)"
                            : "Theme: System (click for Dark)"
                contentItem: Label {
                    text: theme.mode === 1 ? "☾" : theme.mode === 2 ? "☀" : "◐"
                    color: theme.textSecondary
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: themeToggle.hovered ? theme.surfaceOverlay : "transparent"
                }
                onClicked: theme.cycleMode()
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

        Menu {
            id: projectMenu
            objectName: "projectMenu"
            // One item per project, kept in sync with the model, then a
            // separator and the New/Delete actions.
            Instantiator {
                model: projectModel
                delegate: MenuItem {
                    text: model.display
                    onTriggered: if (projectController) projectController.activate(model.projectId)
                }
                onObjectAdded: (index, object) => projectMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => projectMenu.removeItem(object)
            }
            MenuSeparator {}
            MenuItem {
                objectName: "newProjectItem"
                text: "New project…"
                onTriggered: sidebar.newProjectRequested()
            }
            MenuItem {
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

            // TreeView sizes its column to content by default, which leaves the
            // row (and its active-repo highlight) only as wide as the text. Force
            // the single column to span the full view width instead.
            columnWidthProvider: function (column) { return width }
            onWidthChanged: forceLayout()

            // Expand every node so nested submodules show by default. Deferred via
            // Qt.callLater: calling expandRecursively() synchronously on model
            // reset runs before the view has built its rows, so it no-ops.
            onModelChanged: Qt.callLater(expandRecursively)
            Connections {
                target: repoModel
                function onModelReset() { Qt.callLater(repoTree.expandRecursively) }
            }

            delegate: TreeViewDelegate {
                id: row
                implicitHeight: 30
                indentation: 16
                // Selecting a repo — or an initialised submodule — opens it as a
                // first-class repo and reveals any nested children. An
                // uninitialised submodule has no repo on disk, so it does nothing.
                onClicked: {
                    if (!repoVm || row.uninit)
                        return
                    repoVm.open(model.repoPath)
                    if (row.hasChildren)
                        repoTree.expand(row.row)
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

                    // Glyph: repository (◧) vs submodule (❖, accent @0.7).
                    Label {
                        text: row.isSub ? "❖" : "◧"
                        color: (row.isSub || row.activeRepo) ? theme.accent : theme.textSecondary
                        opacity: row.isSub ? 0.7 : 1.0
                        font.pixelSize: row.isSub ? 14 : 15
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
                        ToolTip.visible: fetchFailedLabel.visible && hovered !== undefined ? hovered : false
                    }
                }

                background: Rectangle {
                    color: row.activeRepo ? theme.surfaceBase
                         : row.hovered ? theme.surfaceOverlay : "transparent"
                    radius: 10
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

                // Right-click → remove-from-project menu (top-level repos only).
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        if (row.isSub)
                            return
                        repoContextMenu.repoPath = model.repoPath
                        repoContextMenu.popup()
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

    // ---- Add repository menu ----
    Menu {
        id: addRepoMenu
        objectName: "addRepoMenu"
        MenuItem { text: "Add existing repository…"; onTriggered: sidebar.addExistingRequested() }
        MenuItem { text: "Initialize new repository…"; onTriggered: sidebar.initRequested() }
        MenuItem { text: "Clone repository…"; onTriggered: sidebar.cloneRequested() }
    }

    // ---- Remove-repo context menu ----
    Menu {
        id: repoContextMenu
        objectName: "repoContextMenu"
        property string repoPath: ""
        MenuItem {
            text: "Remove from project"
            onTriggered: if (projectController && repoContextMenu.repoPath.length > 0)
                             projectController.removeRepo(repoContextMenu.repoPath)
        }
    }
}
