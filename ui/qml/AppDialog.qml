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
    anchors.centerIn: parent
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
