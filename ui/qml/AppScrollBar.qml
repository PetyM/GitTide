import QtQuick
import QtQuick.Controls.Basic

// Shared vertical/horizontal scrollbar. Visible whenever the content overflows
// (AsNeeded) and stays put rather than auto-fading — the handle only changes
// tint on hover/press. Colour comes from theme tokens, never a hex literal.
ScrollBar {
    id: control
    policy: ScrollBar.AsNeeded
    minimumSize: 0.1

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.pressed ? theme.textSecondary
             : control.hovered ? theme.textMuted
             : theme.border
        // Show the handle only while the content actually overflows; AsNeeded
        // already hides the whole bar, this guards the brief in-between state.
        opacity: control.size < 1.0 ? 1.0 : 0.0
    }

    background: Rectangle {
        color: "transparent"
    }
}
