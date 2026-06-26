import QtQuick
import QtQuick.Controls.Basic

// Right-click context menu for a repository row in the Sidebar.
// All items always enabled — no contextual disable rules for this entity.
AppMenu {
    id: menu
    objectName: "repoContextMenu"

    property string repoPath: ""

    signal revealInFileManager()
    signal removeFromProject()
    signal updateAllSubmodules()

    AppMenuItem {
        text: "Reveal in file manager"
        onTriggered: menu.revealInFileManager()
    }
    AppMenuItem {
        text: "Update all submodules"
        onTriggered: menu.updateAllSubmodules()
    }

    AppMenuSeparator {}

    AppMenuItem {
        text: "Remove from project"
        destructive: true
        onTriggered: menu.removeFromProject()
    }
}
