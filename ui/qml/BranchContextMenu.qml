import QtQuick
import QtQuick.Controls.Basic

// Right-click context menu for a branch row in BranchDropdown.
// Disabled vs. hidden rule (spec §1.3):
//   - Switch: disabled when isHead (contextually inapplicable).
//   - Rename/Delete: hidden when isRemote (structurally inapplicable — can't mutate remotes locally).
//   - Delete: disabled when isHead (can't delete current branch).
//   - Merge: hidden when isHead (no sense merging a branch into itself).
AppMenu {
    id: menu
    objectName: "branchContextMenu"

    property string branchName: ""
    property bool   isHead:     false
    property bool   isRemote:   false

    signal switchBranch()
    signal newBranchFromHere()
    signal rename()
    signal deleteBranch()
    signal merge()

    AppMenuItem {
        text: "Switch to branch"
        enabled: !menu.isHead
        onTriggered: menu.switchBranch()
    }
    AppMenuItem {
        text: "New branch from here"
        onTriggered: menu.newBranchFromHere()
    }

    AppMenuSeparator {
        visible: !menu.isRemote
    }
    AppMenuItem {
        text: "Rename"
        visible: !menu.isRemote
        onTriggered: menu.rename()
    }
    AppMenuItem {
        text: "Delete"
        visible: !menu.isRemote
        enabled: !menu.isHead
        destructive: true
        onTriggered: menu.deleteBranch()
    }

    AppMenuSeparator {
        visible: !menu.isHead
    }
    AppMenuItem {
        text: repoVm ? ("Merge into " + repoVm.currentBranch) : "Merge into current"
        visible: !menu.isHead
        onTriggered: menu.merge()
    }
}
