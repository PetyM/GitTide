import QtQuick
import QtQuick.Controls.Basic

// Flat themed text button in the title-bar menu bar. Opens the AppMenu assigned
// to its `menu` property below the button. Hover tint matches the app-icon button.
Button {
    id: control

    property string label: ""
    property Menu menu: null

    flat: true
    text: control.label
    implicitHeight: 40
    leftPadding: 10
    rightPadding: 10

    contentItem: Label {
        text: control.text
        color: theme.textPrimary
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    background: Rectangle {
        color: (control.hovered || (control.menu && control.menu.opened))
               ? theme.surfaceOverlay : "transparent"
        radius: 4
    }

    onClicked: if (control.menu) control.menu.popup(control, 0, control.height)
}
