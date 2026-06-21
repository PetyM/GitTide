import QtQuick
import QtQuick.Effects

// Elevated surface for overlays and dialogs (design §9): a themed rounded card
// with a 1px ring and a translucent drop shadow, so popovers and modals read as
// floating above the content rather than painted flat onto it. Use as a Popup's
// or Dialog's `background`. Colour and radius default to the dialog card; the
// branch dropdown overrides them (surfaceOverlay / radius 10).
Item {
    id: card

    property color color: theme.surfaceRaised
    property real radius: 18
    property color borderColor: theme.border

    // The card surface. Painted into a layer so MultiEffect can take it as a
    // source and add the drop shadow without a second copy of the geometry.
    Rectangle {
        id: surface
        anchors.fill: parent
        radius: card.radius
        color: card.color
        border.color: card.borderColor
        border.width: 1
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: surface
        source: surface
        shadowEnabled: true
        shadowColor: theme.shadow
        shadowBlur: 0.7
        shadowVerticalOffset: 6
        autoPaddingEnabled: true
    }
}
