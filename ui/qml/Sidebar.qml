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

        // ---- Repo tree ----
        TreeView {
            id: repoTree
            objectName: "repoTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            model: repoModel

            // Expand every node as rows arrive so nested submodules show by default.
            onModelChanged: expandRecursively()
            Connections {
                target: repoModel
                function onModelReset() { repoTree.expandRecursively() }
            }

            delegate: TreeViewDelegate {
                id: row
                implicitHeight: 34
                indentation: 16
                onClicked: if (repoVm && !model.isSubmodule) repoVm.open(model.repoPath)

                readonly property bool isSub: model.isSubmodule === true
                readonly property bool uninit: isSub && model.status === 2

                contentItem: RowLayout {
                    spacing: 8

                    // Glyph: repository (◧) vs submodule (❖, accent @0.7).
                    Label {
                        text: row.isSub ? "❖" : "◧"
                        color: row.isSub ? theme.accent : (row.current ? theme.accent : theme.textSecondary)
                        opacity: row.isSub ? 0.7 : 1.0
                        font.pixelSize: row.isSub ? 14 : 15
                    }

                    Label {
                        text: row.isSub
                              ? model.display
                              : (model.repoPath ? model.repoPath.toString().split("/").pop() : "")
                        color: (model.missing || row.uninit) ? theme.textMuted : theme.textPrimary
                        font.pixelSize: 13
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
                }

                background: Rectangle {
                    color: row.current ? theme.surfaceBase : "transparent"
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
                    // Selection accent border (repos) at x=0.
                    Rectangle {
                        visible: row.current && !row.isSub
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
        MenuItem { text: "New project…"; onTriggered: sidebar.newProjectRequested() }
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
