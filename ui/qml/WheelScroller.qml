import QtQuick

// Snappy mouse-wheel scrolling for a Flickable (ListView/TableView). The default
// Flickable wheel handling animates a decelerating flick, which reads as
// "braked" / sluggish; this jumps contentY by the wheel delta immediately.
// Mouse only — touchpads keep their native smooth (pixel-delta) scrolling.
//
// Declared as a child of the target list; `view` defaults to that parent.
WheelHandler {
    id: scroller
    property Flickable view: parent
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
