import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Create a new branch. Base-ref picker defaults to the current branch; only the
// default (current HEAD) is honoured for now — a non-default base needs a
// name->OID resolver (deferred), so the picker is informational until then.
Dialog {
    id: dialog
    objectName: "newBranchDialog"
    modal: true
    title: "New branch"
    anchors.centerIn: parent
    width: 380
    padding: 20

    background: OverlayCard {}

    function openDialog() {
        nameField.text = ""
        baseCombo.model = repoVm ? repoVm.branches.localBranchNames() : []
        baseCombo.currentIndex = repoVm ? Math.max(0, baseCombo.model.indexOf(repoVm.currentBranch)) : 0
        open()
        nameField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: "Branch name"
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: nameField
            objectName: "newBranchName"
            Layout.fillWidth: true
            placeholderText: "feature/my-change"
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: nameField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
            Keys.onReturnPressed: if (createButton.enabled) dialog.accept()
        }

        Label {
            text: "Create from"
            color: theme.textMuted
            font.pixelSize: 11
        }
        ComboBox {
            id: baseCombo
            objectName: "newBranchBase"
            Layout.fillWidth: true
        }
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
            id: createButton
            objectName: "newBranchCreate"
            text: "Create"
            enabled: nameField.text.trim().length > 0
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        if (repoVm)
            repoVm.createBranch(nameField.text.trim(), "", true)
    }
}
