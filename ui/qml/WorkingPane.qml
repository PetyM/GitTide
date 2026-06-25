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

    // Fired when Tab moves focus out of this pane (either direction) — the host
    // routes it back to the sidebar repo tree, closing the global Tab cycle.
    signal focusSidebar()

    // Entry points for the global Tab cycle: forward lands on the active tab's
    // primary list, reverse lands on its last element.
    function takeFocus() {
        if (tabs.currentIndex === 0) changesTabBody.takeFocus()
        else historyTabBody.takeFocus()
    }
    function takeFocusLast() {
        if (tabs.currentIndex === 0) changesTabBody.takeFocusLast()
        else historyTabBody.takeFocusLast()
    }

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

        // Header band: a full-width raised strip with the compact Changes/History
        // tabs tucked into its left edge (above the file column), so the toggle
        // reads tightly rather than stretching the whole pane width.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 37
            color: theme.surfaceRaised

            Rectangle { // baseline hairline spanning the full width
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: theme.border
            }

            TabBar {
                id: tabs
                objectName: "changesTabBar"
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                spacing: 0
                background: null

                // Flat tab: active = text.primary (demibold) over a 2px accent
                // underline; inactive = text.secondary; hover tints the row.
                component MainTab: TabButton {
                    id: tabBtn
                    implicitHeight: 36
                    implicitWidth: 96
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
        }

        // Merge-in-progress banner — collapses to height 0 when not in a merge.
        MergeBanner {
            Layout.fillWidth: true
            repo: repoVm
        }

        // Rebase-in-progress banner — collapses to height 0 when not rebasing.
        // onRequestMessageEdit: emitted when pauseReason == "message"; opens
        // rebaseMessageDialog prefilled from repoVm.rebaseMessagePrefill so the user
        // can edit the commit message before continuing the interactive rebase.
        RebaseBanner {
            Layout.fillWidth: true
            repo: repoVm
            onRequestMessageEdit: {
                // Split prefill into summary (first line) and body (rest). The
                // canonical "summary\n\nbody" form has a blank separator line; drop
                // one leading blank so the body field shows the paragraph only.
                var prefill = repoVm ? repoVm.rebaseMessagePrefill : ""
                var nl = prefill.indexOf("\n")
                if (nl < 0) {
                    rebaseMessageDialog.summary = prefill
                    rebaseMessageDialog.body = ""
                } else {
                    rebaseMessageDialog.summary = prefill.substring(0, nl)
                    rebaseMessageDialog.body =
                        prefill.substring(prefill.charAt(nl + 1) === "\n" ? nl + 2 : nl + 1)
                }
                rebaseMessageDialog.open()
            }
        }

        // Separate RewordDialog for the interactive-rebase message-pause flow.
        // Distinct from HistoryPane's rewordDialog (tip-reword) so the two flows
        // never clash. On save, calls repoVm.continueRebase(message) to proceed.
        RewordDialog {
            id: rebaseMessageDialog
            onReworded: function(message) { if (repoVm) repoVm.continueRebase(message) }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // Index 0: Changes — ChangesPane (file list + colored diff with per-line staging).
            ChangesPane {
                id: changesTabBody
                objectName: "changesTabBody"
                onTabNext: workingPane.focusSidebar()
                onTabPrev: workingPane.focusSidebar()
            }

            // Index 1: History — commit list + graph + read-only commit detail.
            HistoryPane {
                id: historyTabBody
                objectName: "historyTabBody"
                onTabNext: workingPane.focusSidebar()
                onTabPrev: workingPane.focusSidebar()
            }
        }
    }

    // ---- Global keyboard shortcuts (spec §2.2) ----

    readonly property bool anyTextInputActive:
        changesTabBody.commitSummaryActive || changesTabBody.commitDescriptionActive

    // Ctrl+1 / Ctrl+2 — switch tabs and route focus.
    Shortcut {
        sequence: "Ctrl+1"
        enabled: repoVm !== null && repoVm.repoOpen
        onActivated: {
            tabs.currentIndex = 0
            changesTabBody.takeFocus()
        }
    }
    Shortcut {
        sequence: "Ctrl+2"
        enabled: repoVm !== null && repoVm.repoOpen
        onActivated: {
            tabs.currentIndex = 1
            historyTabBody.takeFocus()
        }
    }

    // Ctrl+R — refresh history (status refresh is triggered by the controller automatically).
    Shortcut {
        sequence: "Ctrl+R"
        enabled: repoVm !== null && repoVm.repoOpen
        onActivated: repoVm.refreshHistory()
    }

    // ? — toggle shortcuts overlay (guarded: don't fire while typing in commit fields).
    Shortcut {
        sequence: "?"
        context: Qt.WindowShortcut
        enabled: repoVm !== null && repoVm.repoOpen && !anyTextInputActive
        onActivated: shortcutsPopup.visible ? shortcutsPopup.close() : shortcutsPopup.open()
    }

    // Focus fileList when a repo first opens.
    Connections {
        target: repoVm
        enabled: repoVm !== null
        function onChanged() {
            if (repoVm && repoVm.repoOpen)
                Qt.callLater(function() { changesTabBody.takeFocus() })
        }
    }

    ShortcutsHelpPopup {
        id: shortcutsPopup
    }
}
