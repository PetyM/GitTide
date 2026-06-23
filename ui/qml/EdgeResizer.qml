import QtQuick

// Transparent resize handle for a frameless window. Anchor to a window edge;
// set `edges` to the Qt.Edge flag(s) for this zone. Set `active: false` when
// the window is maximised to disable resizing.
Item {
    id: root

    property int edges: Qt.LeftEdge
    property bool active: true

    HoverHandler {
        cursorShape: {
            const e = root.edges
            if (e === (Qt.TopEdge | Qt.LeftEdge) || e === (Qt.BottomEdge | Qt.RightEdge))
                return Qt.SizeFDiagCursor
            if (e === (Qt.TopEdge | Qt.RightEdge) || e === (Qt.BottomEdge | Qt.LeftEdge))
                return Qt.SizeBDiagCursor
            if (e === Qt.LeftEdge || e === Qt.RightEdge)
                return Qt.SizeHorCursor
            return Qt.SizeVerCursor
        }
    }

    DragHandler {
        target: null
        enabled: root.active
        onActiveChanged: if (active) window.startSystemResize(root.edges)
    }
}
