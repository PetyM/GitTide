import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Confirmation guard before discarding working-tree changes (spec §3).
// Open via .open(); on user confirmation emits Dialog.accepted so the caller
// can invoke repoVm.discardFile(path). Never call discardFile without this guard.
Dialog {
    id: dialog
    objectName: "discardChangesDialog"
    modal: true
    title: "Discard changes"
    anchors.centerIn: parent
    width: 380
    padding: 20

    property string fileName: ""

    background: OverlayCard {}

    contentItem: Label {
        text: "Discard changes to \"" + dialog.fileName + "\"? This cannot be undone."
        color: theme.textPrimary
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        Button {
            text: "Cancel"
            onClicked: dialog.reject()
        }
        Button {
            objectName: "discardConfirmButton"
            text: "Discard"
            contentItem: Label {
                text: parent.text
                color: theme.stateDeleted
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 6
                color: theme.surfaceOverlay
                border.color: theme.stateDeleted
                border.width: 1
            }
            onClicked: dialog.accept()
        }
    }
}
