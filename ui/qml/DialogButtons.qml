import QtQuick
import QtQuick.Layouts

// Footer button row for an AppDialog (design §9). Two problems are solved here,
// both caused by assigning a bare `RowLayout` as a Dialog's `footer`:
//
//  1. A Layout assigned directly as `footer` does not propagate a stable
//     implicit height to the Dialog, so the dialog sizes too short — the content
//     overflows below the card and the footer overlaps it. Wrapping the row in a
//     plain Item with an explicit `implicitHeight` gives the Dialog a reliable
//     footer size and restores correct layout.
//  2. QtQuick's Dialog lays the footer out flush to the card edges, and
//     `Layout.margins` on a footer RowLayout is a no-op (its parent is not a
//     Layout), so buttons sat against the border. This Item insets the row.
//
// Declare AppButtons as children; a leading spacer right-aligns them.
Item {
    id: root
    default property alias buttons: row.data

    implicitWidth: row.implicitWidth + 40
    implicitHeight: row.implicitHeight + 32

    RowLayout {
        id: row
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: 20
            rightMargin: 20
        }
        spacing: 8
        Item { Layout.fillWidth: true }
    }
}
