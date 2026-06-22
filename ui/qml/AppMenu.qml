import QtQuick
import QtQuick.Controls.Basic

// Themed popup menu (design §9 — menus ride on surface.overlay). A rounded card
// with a 1px ring, replacing the Basic style's flat grey box. Pair with
// AppMenuItem for text items; checkable items keep the default MenuItem so their
// tick indicator survives.
Menu {
    id: menu

    padding: 6
    margins: 0

    background: Rectangle {
        implicitWidth: 220
        radius: 10
        color: theme.surfaceOverlay
        border.color: theme.border
        border.width: 1
    }
}
