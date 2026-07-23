import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Delete a local branch. Confirmation-by-click (design §11): the danger button
// arms on first click and deletes on the second, unless "Don't ask again" is
// set. An unmerged branch surfaces a warning bar and a force-delete button.
AppDialog {
    id: dialog
    objectName: "deleteBranchDialog"
    title: "Delete branch"
    width: 400
    padding: 20

    property bool armed: false
    property bool unmerged: false
    // Session-scoped suppression of the second confirmation click.
    property bool skipConfirm: false
    // Branch a delete was just requested for; cleared once resolved.
    property string pendingName: ""

    function deletableBranches() {
        if (!repoVm)
            return []
        var all = repoVm.branches.localBranchNames()
        return all.filter(function (n) { return n !== repoVm.currentBranch })
    }

    function openDialog() {
        armed = false
        unmerged = false
        branchCombo.model = deletableBranches()
        branchCombo.currentIndex = 0
        open()
    }

    function openFor(name) {
        armed = false
        unmerged = false
        branchCombo.model = deletableBranches()
        var idx = branchCombo.model.indexOf(name)
        branchCombo.currentIndex = idx >= 0 ? idx : 0
        open()
    }

    contentItem: DialogColumn {
        spacing: 12

        Label {
            text: "Select a branch to delete (the current branch cannot be deleted)."
            color: theme.textMuted
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        AppComboBox {
            id: branchCombo
            objectName: "deleteBranchTarget"
            Layout.fillWidth: true
            onCurrentTextChanged: { dialog.armed = false; dialog.unmerged = false }
        }

        // Unmerged warning bar.
        Rectangle {
            objectName: "deleteUnmergedWarning"
            Layout.fillWidth: true
            visible: dialog.unmerged
            color: Qt.rgba(theme.stateDeleted.r, theme.stateDeleted.g, theme.stateDeleted.b, 0.14)
            border.color: theme.stateDeleted
            border.width: 1
            radius: 6
            implicitHeight: warnLabel.implicitHeight + 16
            Label {
                id: warnLabel
                anchors.fill: parent
                anchors.margins: 8
                text: "This branch is not fully merged. Force-delete to discard its unmerged commits."
                color: theme.textPrimary
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        CheckBox {
            id: dontAsk
            objectName: "deleteDontAsk"
            text: "Don't ask for confirmation again"
            checked: dialog.skipConfirm
            onToggled: dialog.skipConfirm = checked
            contentItem: Label {
                text: dontAsk.text
                color: theme.textMuted
                font.pixelSize: 11
                leftPadding: dontAsk.indicator.width + 6
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    footer: DialogButtons {
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.close()
        }
        AppButton {
            id: dangerButton
            objectName: "deleteBranchConfirm"
            variant: "danger"
            enabled: branchCombo.count > 0
            text: dialog.unmerged ? "Force delete"
                  : dialog.armed ? "Click again to delete"
                  : "Delete"
            onClicked: {
                if (!repoVm)
                    return
                var name = branchCombo.currentText
                if (dialog.unmerged) {
                    dialog.pendingName = name
                    repoVm.deleteBranch(name, true)
                } else if (dialog.skipConfirm || dialog.armed) {
                    dialog.pendingName = name
                    dialog.armed = false
                    repoVm.deleteBranch(name, false)
                } else {
                    dialog.armed = true
                }
            }
        }
    }

    // Surface an unmerged failure from the controller.
    Connections {
        target: repoVm
        function onBranchDeleteUnmerged(name) {
            if (dialog.visible) {
                dialog.unmerged = true
                dialog.pendingName = ""
            }
        }
    }

    // Close once the pending branch is gone from the model (delete succeeded).
    Connections {
        target: repoVm ? repoVm.branches : null
        function onModelReset() {
            if (dialog.pendingName.length > 0 && repoVm
                    && repoVm.branches.localBranchNames().indexOf(dialog.pendingName) < 0) {
                dialog.pendingName = ""
                dialog.close()
            }
        }
    }
}
