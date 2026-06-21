import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The main area. Shows the branded empty state until a repository is open, then
// the branch bar + Changes/History tabs + diff. Add-repo requests from the empty
// state bubble up to the host (Main), which owns the dialogs.
Item {
    id: workingPane
    objectName: "workingPane"

    readonly property bool repoOpen: repoVm && repoVm.repoOpen

    signal addExistingRequested()
    signal cloneRequested()
    signal initRequested()
    signal newProjectRequested()

    // ---- Empty state (no repo open) ----
    EmptyState {
        anchors.fill: parent
        visible: !workingPane.repoOpen
        hasProject: projectController ? projectController.activeProjectId.length > 0 : false
        onAddExistingRequested: workingPane.addExistingRequested()
        onCloneRequested: workingPane.cloneRequested()
        onInitRequested: workingPane.initRequested()
        onNewProjectRequested: workingPane.newProjectRequested()
    }

    // ---- Repo working view (a repo is open) ----
    ColumnLayout {
        objectName: "repoView"
        anchors.fill: parent
        spacing: 0
        visible: workingPane.repoOpen

        BranchBar {
            Layout.fillWidth: true
        }

        TabBar {
            id: tabs
            objectName: "changesTabBar"
            Layout.fillWidth: true
            background: Rectangle { color: theme.surfaceRaised }
            TabButton {
                text: "Changes"
                contentItem: Label {
                    text: parent.text
                    color: parent.checked ? theme.textPrimary : theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: "transparent" }
            }
            TabButton {
                text: "History"
                contentItem: Label {
                    text: parent.text
                    color: parent.checked ? theme.textPrimary : theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: "transparent" }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // Index 0: Changes — ChangesPane (file list + colored diff with per-line staging).
            ChangesPane {
                objectName: "changesTabBody"
            }

            // Index 1: History — commit list + graph + read-only commit detail.
            HistoryPane {
                objectName: "historyTabBody"
            }
        }
    }
}
