import QtQuick
import QtQuick.Controls.Basic

// Themed dropdown. Drop-in for Basic ComboBox (same model/currentIndex/currentText
// API). Themed field, popup card, and delegates off the surface palette.
ComboBox {
    id: combo
    implicitHeight: 30

    contentItem: Label {
        leftPadding: 10
        rightPadding: combo.indicator.width + 10
        text: combo.displayText
        color: theme.textPrimary
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 6
        color: theme.surfaceBase
        border.width: 1
        border.color: (combo.activeFocus || combo.pressed) ? theme.accent : theme.border
    }

    indicator: Label {
        x: combo.width - width - 10
        anchors.verticalCenter: combo.verticalCenter
        text: "▾"
        color: theme.textMuted
        font.pixelSize: 12
    }

    delegate: ItemDelegate {
        id: itemDel
        width: combo.width
        height: 28
        highlighted: combo.highlightedIndex === index
        contentItem: Label {
            // Resolve the label. For a JS array-of-objects model the role lives on
            // `modelData` (the element), not `model` — so prefer modelData when it's
            // an object; fall back to `model[role]` for C++/ListModel role models.
            text: combo.textRole.length
                  ? ((modelData !== undefined && modelData !== null && typeof modelData === "object")
                     ? (modelData[combo.textRole] ?? "")
                     : (model[combo.textRole] ?? ""))
                  : (modelData ?? "")
            color: theme.textPrimary
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
        }
        background: Rectangle {
            color: itemDel.highlighted ? theme.surfaceOverlay : "transparent"
        }
    }

    popup: Popup {
        y: combo.height + 2
        width: combo.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 240)
        padding: 1
        background: Rectangle {
            radius: 6
            color: theme.surfaceRaised
            border.color: theme.border
            border.width: 1
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
            ScrollBar.vertical: AppScrollBar {}
        }
    }
}
