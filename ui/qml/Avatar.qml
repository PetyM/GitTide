import QtQuick
import QtQuick.Controls.Basic

// Circular avatar. Gravatar-by-email is a future enhancement (CommitNode carries
// no email yet), so this renders initials on an accent-tinted disc — the design's
// "initials fallback now" path.
Rectangle {
    id: avatar
    property string name: ""
    implicitWidth: 24
    implicitHeight: 24
    radius: width / 2
    color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)

    Label {
        anchors.centerIn: parent
        text: {
            var parts = avatar.name.trim().split(/\s+/).filter(function (p) { return p.length > 0 })
            if (parts.length === 0) return "?"
            if (parts.length === 1) return parts[0].charAt(0).toUpperCase()
            return (parts[0].charAt(0) + parts[parts.length - 1].charAt(0)).toUpperCase()
        }
        color: theme.accent
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
