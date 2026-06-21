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
            spacing: 0
            background: Rectangle {
                color: theme.surfaceRaised
                // Baseline the active-tab underline rides on.
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: 1
                    color: theme.border
                }
            }

            // Flat tab: active = text.primary (demibold) over a 2px accent
            // underline; inactive = text.secondary; hover tints the row.
            component MainTab: TabButton {
                id: tabBtn
                implicitHeight: 36
                contentItem: Label {
                    text: tabBtn.text
                    color: tabBtn.checked ? theme.textPrimary : theme.textSecondary
                    font.pixelSize: 13
                    font.weight: tabBtn.checked ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: (tabBtn.hovered && !tabBtn.checked) ? theme.surfaceOverlay : "transparent"
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 2
                        color: theme.accent
                        visible: tabBtn.checked
                    }
                }
            }

            MainTab { text: "Changes" }
            MainTab { text: "History" }
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
