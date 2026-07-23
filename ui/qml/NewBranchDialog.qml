import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Create a new branch.
// openDialog()       — branch from HEAD (or the base-combo selection, deferred).
// openFromCommit(id) — branch from a specific commit OID; hides the base combo.
AppDialog {
    id: dialog
    objectName: "newBranchDialog"
    title: "New branch"
    width: 380
    padding: 20

    property string fromOid: ""

    function openDialog() {
        fromOid = ""   // always reset; callers that want a specific OID use openFromCommit
        nameField.text = ""
        baseCombo.model = repoVm ? repoVm.branches.localBranchNames() : []
        baseCombo.currentIndex = repoVm ? Math.max(0, baseCombo.model.indexOf(repoVm.currentBranch)) : 0
        open()
        nameField.forceActiveFocus()
    }

    function openFromCommit(oid) {
        fromOid = oid
        nameField.text = ""
        baseCombo.model = []
        open()
        nameField.forceActiveFocus()
    }

    contentItem: DialogColumn {
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
            visible: dialog.fromOid.length === 0
        }
        AppComboBox {
            id: baseCombo
            objectName: "newBranchBase"
            Layout.fillWidth: true
            visible: dialog.fromOid.length === 0
        }
        Label {
            visible: dialog.fromOid.length > 0
            text: "From commit " + dialog.fromOid.slice(0, 7)
            color: theme.textMuted
            font.pixelSize: 11
            font.family: "monospace"
        }
    }

    footer: DialogButtons {
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.reject()
        }
        AppButton {
            id: createButton
            objectName: "newBranchCreate"
            variant: "primary"
            text: "Create"
            enabled: nameField.text.trim().length > 0
            onClicked: dialog.accept()
        }
    }

    onAccepted: {
        if (repoVm)
            repoVm.createBranch(nameField.text.trim(), dialog.fromOid, true)
    }
}
