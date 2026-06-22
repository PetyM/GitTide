import QtQuick
import QtQuick.Controls.Basic

// Themed tri-state checkbox (design §8 file list). A rounded 18px box that reads
// off the surface palette: unchecked = hollow with a border ring, checked =
// accent fill with a tick, partial = accent fill with a dash. Drop-in for the
// Basic CheckBox — same checkState/tristate/onClicked API, no text label.
CheckBox {
    id: cb

    implicitWidth: 18
    implicitHeight: 18
    padding: 0
    spacing: 0

    indicator: Rectangle {
        width: 18
        height: 18
        anchors.verticalCenter: cb.verticalCenter
        radius: 4
        color: cb.checkState === Qt.Unchecked ? "transparent" : theme.accent
        border.color: cb.checkState === Qt.Unchecked
                      ? (cb.hovered ? theme.textSecondary : theme.border)
                      : theme.accent
        border.width: 1

        // Tick for the fully-checked state.
        Label {
            anchors.centerIn: parent
            visible: cb.checkState === Qt.Checked
            text: "✓"
            color: theme.surfaceBase
            font.pixelSize: 12
            font.weight: Font.Bold
        }

        // Dash for the partially-checked (some-but-not-all) state.
        Rectangle {
            anchors.centerIn: parent
            visible: cb.checkState === Qt.PartiallyChecked
            width: 8
            height: 2
            radius: 1
            color: theme.surfaceBase
        }
    }
}
