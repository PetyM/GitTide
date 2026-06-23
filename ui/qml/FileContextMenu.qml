import QtQuick
import QtQuick.Controls.Basic

// Right-click context menu for a changed file in the working-changes list.
// Instantiate once per view; set properties from the row model, then call popup().
// Disabled vs. hidden rule (spec §1.3):
//   - Stage: disabled when already fully staged (checkState 2).
//   - Unstage: hidden for untracked files; disabled when not staged (checkState 0).
//   - Discard: hidden for untracked files (structurally inapplicable — nothing to discard).
AppMenu {
    id: menu
    objectName: "fileContextMenu"

    property string filePath:   ""
    property string fileName:   ""
    property string statusKind: ""   // "added" | "modified" | "deleted" | "untracked"
    property int    checkState: 0    // 0=Unchecked, 1=Partial, 2=Checked
    property int    rowIndex:   -1

    signal stage()
    signal unstage()
    signal discard()
    signal openInEditor()
    signal revealInFileManager()
    signal copyPath()

    AppMenuItem {
        text: "Stage"
        enabled: menu.checkState !== 2
        onTriggered: menu.stage()
    }
    AppMenuItem {
        text: "Unstage"
        visible: menu.statusKind !== "untracked"
        enabled: menu.checkState !== 0
        onTriggered: menu.unstage()
    }

    AppMenuSeparator {}

    AppMenuItem {
        text: "Open in editor"
        onTriggered: menu.openInEditor()
    }
    AppMenuItem {
        text: "Reveal in file manager"
        onTriggered: menu.revealInFileManager()
    }
    AppMenuItem {
        text: "Copy path"
        onTriggered: menu.copyPath()
    }

    AppMenuSeparator {
        visible: menu.statusKind !== "untracked"
    }
    AppMenuItem {
        text: "Discard changes"
        visible: menu.statusKind !== "untracked"
        destructive: true
        onTriggered: menu.discard()
    }
}
