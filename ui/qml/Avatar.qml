import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects

// Circular avatar. Shows deterministic initials immediately, then swaps in the
// author's real avatar (Gravatar via the "avatar" image provider, keyed on the
// email md5) once it arrives — so rows never jump. A missing/failed/disabled
// avatar leaves the initials disc showing.
Rectangle {
    id: avatar
    property string name: ""
    property string email: ""
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

    // The fetched image, rendered off-screen and masked into a circle below.
    // (A rounded Rectangle's `clip` only clips to the rectangular bounds, not the
    // radius, so the image would otherwise show square.)
    Image {
        id: avatarImage
        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        // The provider serves a 128px image; render it at the target device pixel
        // ratio and mipmap so the downscale to the ~24px disc stays crisp on HiDPI.
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        smooth: true
        mipmap: true
        // Empty email → no source, initials stay. Otherwise fetch by email hash;
        // the provider dedups identical authors across virtualized rows.
        source: avatar.email !== "" ? "image://avatar/" + avatarService.emailHash(avatar.email) : ""
        visible: false
    }

    // Circular alpha mask: a rounded-rect rendered to its own layer.
    Item {
        id: avatarMask
        anchors.fill: avatarImage
        layer.enabled: true
        visible: false
        Rectangle {
            anchors.fill: parent
            radius: width / 2
        }
    }

    // Compose image + mask → a true circular avatar. Hidden until the image is
    // ready so the initials disc shows through in the meantime.
    MultiEffect {
        anchors.fill: avatarImage
        source: avatarImage
        maskEnabled: true
        maskSource: avatarMask
        visible: avatarImage.status === Image.Ready
    }
}
