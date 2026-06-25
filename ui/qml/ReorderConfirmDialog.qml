import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Confirmation for a drag-to-reorder in the history view. Reordering replays the
// affected commits via an interactive rebase, so it rewrites history — guard it
// behind an explicit confirmation (spec rebase-interactive.md, D36).
//
// API: openFor(fromRow, toRow) → on accept calls repoVm.reorderCommits(from, to).
Dialog {
    id: dialog
    objectName: "reorderConfirmDialog"
    modal: true
    title: "Reorder commits"
    anchors.centerIn: parent
    width: 380
    padding: 20
    closePolicy: Popup.CloseOnEscape

    property int fromRow: -1
    property int toRow: -1

    background: OverlayCard {}

    function openFor(from, to) {
        fromRow = from
        toRow = to
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
        Button {
            text: "Cancel"
            onClicked: dialog.close()
        }
        Button {
            objectName: "reorderConfirmButton"
            text: "Reorder"
            onClicked: {
                if (repoVm)
                    repoVm.reorderCommits(dialog.fromRow, dialog.toRow)
                dialog.close()
            }
        }
    }
}
