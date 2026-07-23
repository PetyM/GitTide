import QtQuick
import QtQuick.Controls.Basic

// Shared modal dialog base (design §9). Replaces QtQuick.Controls.Basic's default
// Dialog header — an unthemed `palette.light` (white) bar with square corners and
// no close affordance — with a themed header that carries the title and a close
// button. The header background is transparent so the OverlayCard's rounded top
// corners show through instead of being capped by a square bar.
//
// Instances set `title`, `contentItem` and `footer` as usual; override `closable`
// to hide the ✕ (e.g. a blocking operation with its own Cancel), and `padding`
// when a dialog wants tighter/looser content insets than the 20px default.
Dialog {
    id: control

    modal: true
    // Centre in the whole window, not the item the dialog happens to be declared
    // in (e.g. a dialog nested in the diff pane would otherwise centre over that
    // pane). Parenting to the window overlay makes centerIn resolve to the window.
    parent: Overlay.overlay
    anchors.centerIn: parent
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Size to the full stack (header + padded content + footer). QtQuick's Dialog
    // does not fold the content height into its implicit height once a `footer` is
    // present, so with a footer the card sizes too short — the content overflows
    // below the card and the footer overlaps it. Deriving the height explicitly
    // keeps the card wrapping its content. Dialogs whose content is a Layout must
    // wrap it in a plain Item so `contentItem.implicitHeight` is reported (a Layout
    // used as a Popup's direct contentItem reports 0); see e.g. NewBranchDialog.
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                    + implicitHeaderHeight + implicitFooterHeight
    height: implicitHeight

    // Shows the header ✕; clicking it rejects the dialog. Hide for dialogs that
    // must not be dismissed by a stray click (e.g. an in-flight operation).
    property bool closable: true

    background: OverlayCard {}

    header: Item {
        implicitHeight: control.title.length > 0 ? 52 : (control.closable ? 44 : 0)
        visible: implicitHeight > 0

        Label {
            visible: control.title.length > 0
            anchors {
                left: parent.left
                leftMargin: 20
                right: closeButton.left
                rightMargin: 8
                verticalCenter: parent.verticalCenter
            }
            text: control.title
            color: theme.textPrimary
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Label.ElideRight
        }

        AbstractButton {
            id: closeButton
            objectName: "dialogCloseButton"
            visible: control.closable
            width: 28
            height: 28
            hoverEnabled: true
            anchors {
                right: parent.right
                rightMargin: 12
                top: parent.top
                topMargin: 12
            }
            onClicked: control.reject()

            contentItem: Label {
                text: "✕"   // ✕
                color: closeButton.hovered ? theme.textPrimary : theme.textMuted
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                radius: 6
                color: closeButton.hovered ? theme.surfaceOverlay : "transparent"
            }
        }
    }
}
