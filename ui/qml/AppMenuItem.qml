import QtQuick
import QtQuick.Controls.Basic

// Themed text row for AppMenu. Highlight uses the same accent tint as the branch
// chip so hover reads consistently across popovers. No tick indicator — use a
// plain MenuItem for checkable entries.
// Set destructive: true for danger actions (discard, delete, remove) — renders
// text in theme.stateDeleted rather than text.primary.
MenuItem {
    id: item

    property bool destructive: false

    implicitHeight: 34
    padding: 8

    contentItem: Label {
        text: item.text
        color: item.enabled
               ? (item.destructive ? theme.stateDeleted : theme.textPrimary)
               : theme.textMuted
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 6
        color: item.highlighted
               ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18)
               : "transparent"
    }
}
