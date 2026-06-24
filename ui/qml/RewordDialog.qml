import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Edit the HEAD commit message.
// openFor(oid) — lazy-fetches the full message via repoVm.requestCommitMessage(oid)
//               and pre-fills the summary/body fields when commitMessageReady fires.
// Save emits reworded(message) with the assembled commit message string.
Dialog {
    id: dialog
    objectName: "rewordDialog"
    modal: true
    title: "Reword commit"
    anchors.centerIn: parent
    width: 460
    padding: 20

    property string oid: ""
    property alias summary: summaryField.text
    property alias body: bodyField.text

    signal reworded(string message)

    background: OverlayCard {}

    function openFor(commitOid) {
        dialog.oid = commitOid
        summaryField.text = ""
        bodyField.text = ""
        open()
        summaryField.forceActiveFocus()
        if (repoVm)
            repoVm.requestCommitMessage(commitOid)
    }

    Connections {
        target: repoVm
        function onCommitMessageReady(oid, message) {
            if (oid !== dialog.oid)
                return
            var nl = message.indexOf("\n")
            if (nl < 0) {
                summaryField.text = message
                bodyField.text = ""
            } else {
                summaryField.text = message.substring(0, nl)
                // Skip the blank separator line between subject and body.
                bodyField.text = message.substring(message.charAt(nl + 1) === "\n" ? nl + 2 : nl + 1)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: "Summary"
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: summaryField
            objectName: "rewordSummary"
            Layout.fillWidth: true
            placeholderText: "One-line summary"
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: summaryField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
            Keys.onReturnPressed: if (saveButton.enabled) dialog.accept()
        }

        Label {
            text: "Extended description (optional)"
            color: theme.textMuted
            font.pixelSize: 11
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            TextArea {
                id: bodyField
                objectName: "rewordBody"
                placeholderText: "Paragraph break separates subject from body"
                color: theme.textPrimary
                background: Rectangle {
                    radius: 6
                    color: theme.surfaceBase
                    border.color: bodyField.activeFocus ? theme.accent : theme.border
                    border.width: 1
                }
                wrapMode: TextArea.Wrap
            }
        }
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
            id: saveButton
            objectName: "rewordSave"
            text: "Save"
            enabled: summaryField.text.trim().length > 0
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        var msg = summaryField.text.trim()
        if (bodyField.text.trim().length > 0)
            msg += "\n\n" + bodyField.text.trim() + "\n"
        else
            msg += "\n"
        dialog.reworded(msg)
    }
}
