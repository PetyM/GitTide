import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Confirmation for a drag-to-reorder in the history view. Reordering replays the
// affected commits via an interactive rebase, so it rewrites history — guard it
// behind an explicit confirmation (spec rebase-interactive.md, D36).
//
// API: openFor(fromRow, toRow, band) → on accept calls
// repoVm.reorderCommits(from, to, band). band ("above"/"below") selects the side
// of the target the dragged commit lands on; defaults to "below".
AppDialog {
    id: dialog
    objectName: "reorderConfirmDialog"
    title: "Reorder commits"
    width: 380
    padding: 20
    closePolicy: Popup.CloseOnEscape

    property int fromRow: -1
    property int toRow: -1
    property string band: "below"

    function openFor(from, to, dropBand) {
        fromRow = from
        toRow = to
        band = dropBand ?? "below"
        open()
    }

    contentItem: Label {
        text: "Reordering replays these commits and rewrites history from this "
              + "point. You can abort the rebase if anything goes wrong. Continue?"
        color: theme.textPrimary
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "reorderConfirmButton"
            variant: "primary"
            text: "Reorder"
            onClicked: {
                if (repoVm)
                    repoVm.reorderCommits(dialog.fromRow, dialog.toRow, dialog.band)
                dialog.close()
            }
        }
    }
}
