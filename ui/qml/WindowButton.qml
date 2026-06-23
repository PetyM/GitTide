import QtQuick
import QtQuick.Controls.Basic

// Cross-platform window control button.
// macOs=true: colored circle (close/minimise/maximise traffic lights).
// macOs=false: flat glyph button (Win/Linux style).
AbstractButton {
    id: btn

    property bool macOs: false
    property color circleColor: "transparent"
    property string glyph: ""
    property color glyphColor: theme.textSecondary
    property color hoverColor: theme.surfaceOverlay

    implicitWidth: macOs ? 28 : 46
    implicitHeight: 40
    padding: 0

    contentItem: Item {
        // macOS circle with glyph shown on hover
        Rectangle {
            visible: btn.macOs
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: btn.circleColor
            opacity: btn.hovered ? 1.0 : 0.85

            Label {
                anchors.centerIn: parent
                visible: btn.hovered
                text: btn.glyph
                color: "#333333"
                font.pixelSize: 7
                font.weight: Font.Bold
            }
        }

        // Win/Linux glyph
        Label {
            visible: !btn.macOs
            anchors.centerIn: parent
            text: btn.glyph
            color: btn.glyphColor
            font.pixelSize: 13
        }
    }

    background: Rectangle {
        visible: !btn.macOs
        color: btn.hovered ? btn.hoverColor : "transparent"
    }
}
