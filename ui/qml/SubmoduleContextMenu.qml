import QtQuick
import QtQuick.Controls.Basic

AppMenu {
    id: menu
    objectName: "submoduleContextMenu"

    property string ownerRepoPath: ""
    property string submodulePath: ""
    property int    status: 0
    readonly property bool initialised: status !== 2

    signal initRequested()
    signal updateAllRequested()
    signal deinitRequested()

    AppMenuItem {
        text: menu.initialised ? "Update submodule" : "Initialize submodule"
        onTriggered: menu.initRequested()
    }
    AppMenuItem {
        text: "Update all submodules"
        enabled: menu.initialised
        onTriggered: menu.updateAllRequested()
    }
    AppMenuSeparator {}
    AppMenuItem {
        text: "Deinitialize submodule"
        destructive: true
        enabled: menu.initialised
        onTriggered: menu.deinitRequested()
    }
}
