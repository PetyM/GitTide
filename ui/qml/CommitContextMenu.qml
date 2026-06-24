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

    signal copySha()
    signal newBranchFromHere()
    signal checkoutCommit()
    signal reword()
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
        visible: menu.isHead
        onTriggered: menu.reword()
    }
    AppMenuItem {
        objectName: "editHistoryItem"
        text: "Edit history from here…"
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
