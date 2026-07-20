import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Confirmation guard before discarding working-tree changes (spec §3).
// Open via .open(); on user confirmation emits Dialog.accepted so the caller
// can invoke repoVm.discardFile(path). Never call discardFile without this guard.
AppDialog {
    id: dialog
    objectName: "discardChangesDialog"
    title: "Discard changes"
    width: 380
    padding: 20

    property string fileName: ""

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
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.reject()
        }
        AppButton {
            objectName: "discardConfirmButton"
            variant: "danger"
            text: "Discard"
            onClicked: dialog.accept()
        }
    }
}
