import QtQuick
import QtQuick.Controls.Basic

// Right-click menu for the diff pane. Instantiate once per diff view, set
// hasSelection, then call popup(). The copy items are disabled — not hidden —
// when nothing is selected, so the menu keeps a stable shape.
AppMenu {
    id: menu
    objectName: "diffContextMenu"

    property bool hasSelection: false

    signal copy()
    signal copyWithMarkers()
    signal selectAll()

    AppMenuItem {
        objectName: "diffMenuCopy"
        text: qsTr("Copy")
        enabled: menu.hasSelection
        onTriggered: menu.copy()
    }
    AppMenuItem {
        objectName: "diffMenuCopyMarkers"
        text: qsTr("Copy with Diff Markers")
        enabled: menu.hasSelection
        onTriggered: menu.copyWithMarkers()
    }

    AppMenuSeparator {}

    AppMenuItem {
        objectName: "diffMenuSelectAll"
        text: qsTr("Select All")
        onTriggered: menu.selectAll()
    }
}
