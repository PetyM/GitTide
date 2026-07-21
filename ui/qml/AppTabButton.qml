import QtQuick
import QtQuick.Controls.Basic

// Flat tab button shared by the working-pane tab strip and the Options dialog.
// Active = text.primary (demibold) over a 2px accent underline; inactive =
// text.secondary; hover tints an inactive row.
TabButton {
    id: tabBtn
    implicitHeight: 36
    implicitWidth: 96
    contentItem: Label {
        text: tabBtn.text
        color: tabBtn.checked ? theme.textPrimary : theme.textSecondary
        font.pixelSize: 13
        font.weight: tabBtn.checked ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: (tabBtn.hovered && !tabBtn.checked) ? theme.surfaceOverlay : "transparent"
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 2
            color: theme.accent
            visible: tabBtn.checked
        }
    }
}
