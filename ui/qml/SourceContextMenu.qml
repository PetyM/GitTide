import QtQuick
import QtQuick.Controls.Basic

// Right-click context menu for a repository-source (folder) row in the Sidebar.
// Mirrors the Sources section of the Project Options dialog so the common
// actions do not require opening a dialog. Removing a source only unregisters
// the folder — the repositories it already added stay in the project.
AppMenu {
    id: menu
    objectName: "sourceContextMenu"

    property string sourcePath: ""

    signal rescanRequested()
    signal clearIgnoredRequested()
    signal removeRequested()

    AppMenuItem {
        text: "Rescan now"
        onTriggered: menu.rescanRequested()
    }
    AppMenuItem {
        text: "Clear ignored"
        onTriggered: menu.clearIgnoredRequested()
    }

    AppMenuSeparator {}

    AppMenuItem {
        text: "Remove source"
        destructive: true
        onTriggered: menu.removeRequested()
    }
}
