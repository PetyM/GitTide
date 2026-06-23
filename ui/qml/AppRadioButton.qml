import QtQuick
import QtQuick.Controls.Basic

// Themed radio button with a text label. Same API as RadioButton — use with
// ButtonGroup for mutually exclusive groups. Indicator is a 16px circle:
// hollow when unchecked, accent-filled with a white inner dot when checked.
RadioButton {
    id: rb

    implicitHeight: 28
    spacing: 8
    padding: 0

    indicator: Rectangle {
        width: 16
        height: 16
        anchors.verticalCenter: rb.verticalCenter
        radius: 8
        color: rb.checked ? theme.accent : "transparent"
        border.color: rb.checked ? theme.accent
                                 : (rb.hovered ? theme.textSecondary : theme.border)
        border.width: 1

        Rectangle {
            anchors.centerIn: parent
            visible: rb.checked
            width: 6
            height: 6
            radius: 3
            color: theme.surfaceBase
        }
    }

    contentItem: Label {
        leftPadding: rb.indicator.width + rb.spacing
        text: rb.text
        color: theme.textPrimary
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
    }
}
