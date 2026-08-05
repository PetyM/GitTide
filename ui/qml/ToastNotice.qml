import QtQuick
import QtQuick.Controls.Basic

// Transient inline notice: fades in at the bottom of the window, holds for a
// few seconds, fades out. Non-blocking by design — used for outcomes the user
// does not have to act on, e.g. repositories picked up by a source rescan.
Item {
    id: toast
    objectName: "toastNotice"

    property string text: ""
    readonly property bool showing: hold.running || fade.running

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 24
    width: card.width
    height: card.height
    visible: opacity > 0
    opacity: 0

    function show(message) {
        if (message.length === 0)
            return
        // A still-running fade-out from a previous toast would otherwise race
        // the opacity = 1 below and win, leaving this new toast invisible.
        fade.stop()
        toast.text = message
        toast.opacity = 1
        hold.restart()
    }

    OverlayCard {
        id: card
        width: label.implicitWidth + 32
        height: label.implicitHeight + 20

        Label {
            id: label
            anchors.centerIn: parent
            text: toast.text
            color: theme.textPrimary
            font.pixelSize: 12
        }
    }

    Timer {
        id: hold
        interval: 3500
        onTriggered: fade.start()
    }
    NumberAnimation {
        id: fade
        target: toast
        property: "opacity"
        to: 0
        duration: 200
    }
}
