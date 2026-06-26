import QtQuick
import QtQuick.Controls.Basic

// Themed action button. Drop-in for Basic Button (same text/enabled/onClicked
// API). `variant` picks primary (filled accent), secondary (outline), or danger
// (filled red); `compact` shrinks it for inline use (e.g. the submodule Init pill).
Button {
    id: btn
    property string variant: "primary"   // "primary" | "secondary" | "danger"
    property bool   compact: false

    implicitHeight: compact ? 22 : 30
    leftPadding:  compact ? 8 : 14
    rightPadding: compact ? 8 : 14
    topPadding: 0
    bottomPadding: 0

    readonly property bool _filled: variant === "primary" || variant === "danger"
    readonly property color _fill: variant === "danger" ? theme.stateDeleted : theme.accent
    readonly property color _fillHover: variant === "danger"
                                        ? Qt.darker(theme.stateDeleted, 1.2)
                                        : theme.accentHover

    contentItem: Label {
        text: btn.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: btn.compact ? 11 : 12
        color: !btn.enabled ? theme.textMuted
               : btn._filled ? theme.surfaceBase
               : theme.textPrimary
    }

    background: Rectangle {
        radius: 6
        color: !btn.enabled ? theme.surfaceOverlay
               : btn._filled ? (btn.hovered ? btn._fillHover : btn._fill)
               : (btn.hovered ? theme.surfaceOverlay : "transparent")
        border.width: btn.variant === "secondary" ? 1 : 0
        border.color: theme.border
    }
}
