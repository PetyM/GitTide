import QtQuick
import QtQuick.Controls.Basic

// Right-click context menu for a commit row in HistoryPane.
// Disabled vs. hidden rule (spec §1.3):
//   - Checkout commit: disabled when isHead (contextually inapplicable — already here).
//   - Merge: hidden when localBranchName is empty (structurally inapplicable —
//     only branch tips can be merged by name; arbitrary commits cannot).
AppMenu {
    id: menu
    objectName: "commitContextMenu"

    property string oid:             ""
    property string shortOid:        ""
    property string localBranchName: ""
    property bool   isHead:          false
    // Number of selected history rows (≥2 enables "Squash N commits…").
    property int    selectionCount:  1
    // Set to false in read-only contexts (e.g. the all-refs Graph tab) to hide
    // history-editing items (Reword, Undo, Squash, Edit history from here).
    property bool   allowHistoryEditing: true

    signal copySha()
    signal newBranchFromHere()
    signal checkoutCommit()
    signal reword()
    signal undoLastCommit()
    signal squashSelected()
    signal editHistory()
    signal merge()

    AppMenuItem {
        text: "Copy SHA"
        onTriggered: menu.copySha()
    }

    AppMenuSeparator {}

    AppMenuItem {
        text: "New branch from here"
        onTriggered: menu.newBranchFromHere()
    }
    AppMenuItem {
        text: "Checkout commit"
        enabled: !menu.isHead
        onTriggered: menu.checkoutCommit()
    }
    AppMenuItem {
        objectName: "rewordItem"
        text: "Reword…"
        visible: menu.isHead && menu.allowHistoryEditing
        onTriggered: menu.reword()
    }
    AppMenuItem {
        objectName: "undoLastCommitItem"
        text: "Undo last commit"
        // Only HEAD can be undone (soft reset to its parent keeps changes staged).
        visible: menu.isHead && menu.allowHistoryEditing
        onTriggered: menu.undoLastCommit()
    }
    AppMenuItem {
        objectName: "squashSelectedItem"
        text: "Squash " + menu.selectionCount + " commits…"
        // Only when a multi-commit range is selected (squash needs ≥2).
        visible: menu.selectionCount >= 2 && menu.allowHistoryEditing
        onTriggered: menu.squashSelected()
    }
    AppMenuItem {
        objectName: "editHistoryItem"
        text: "Edit history from here…"
        visible: menu.allowHistoryEditing
        onTriggered: menu.editHistory()
    }

    AppMenuSeparator {
        visible: menu.localBranchName.length > 0
    }
    AppMenuItem {
        objectName: "mergeIntoItem"
        text: (repoVm && menu.localBranchName.length > 0)
              ? ("Merge " + menu.localBranchName + " into " + repoVm.currentBranch)
              : "Merge into current"
        visible: menu.localBranchName.length > 0
        onTriggered: menu.merge()
    }
}
