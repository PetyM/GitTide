import QtQuick
import QtQuick.Controls.Basic

// Thin horizontal rule for grouping items inside an AppMenu (design §context-menus).
MenuSeparator {
    contentItem: Rectangle {
        implicitHeight: 1
        color: theme.border
    }
    padding: 4
}
