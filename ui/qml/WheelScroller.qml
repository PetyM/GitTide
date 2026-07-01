import QtQuick

// Snappy mouse-wheel scrolling for a Flickable (ListView/TableView). The default
// Flickable wheel handling animates a decelerating flick, which reads as
// "braked" / sluggish; this jumps contentY by the wheel delta immediately.
// Mouse only — touchpads keep their native smooth (pixel-delta) scrolling.
//
// Declared as a child of the target list. Flickable's default property
// (`flickableData`) redirects declared children into its internal
// contentItem, so `parent` here is that contentItem, not the Flickable —
// `parent.parent` is the actual Flickable.
WheelHandler {
    id: scroller
    property Flickable view: parent ? parent.parent : null
    property real factor: 1.0

    acceptedDevices: PointerDevice.Mouse
    onWheel: function(event) {
        if (!view)
            return
        const max = Math.max(0, view.contentHeight - view.height)
        view.contentY = Math.max(0, Math.min(max, view.contentY - event.angleDelta.y * factor))
        event.accepted = true
    }
}
