import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: changesPane
    objectName: "changesPane"
    spacing: 0

    // ---- Files + commit column (fixed width) ----
    ColumnLayout {
        Layout.preferredWidth: 320
        Layout.fillHeight: true
        spacing: 0

        // Header with tri-state master checkbox + count
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 10

            CheckBox {
                objectName: "fileHeaderCheck"
                tristate: true
                // 0 Unchecked, 1 PartiallyChecked, 2 Checked — mirror file states.
                checkState: {
                    var n = repoVm ? repoVm.checkedCount : 0
                    var total = repoVm && repoVm.changedFiles ? repoVm.changedFiles.rowCount() : 0
                    if (n === 0) return Qt.Unchecked
                    if (n === total) return Qt.Checked
                    return Qt.PartiallyChecked
                }
                onClicked: if (repoVm) repoVm.setAllFilesChecked(checkState !== Qt.Checked)
            }
            Label {
                text: "Changed files"
                color: theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: repoVm && repoVm.changedFiles
                      ? (repoVm.checkedCount + " / " + repoVm.changedFiles.rowCount())
                      : ""
                color: theme.textMuted
                font.pixelSize: 11
            }
        }

        ListView {
            id: fileList
            objectName: "fileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: repoVm ? repoVm.changedFiles : null

            delegate: Rectangle {
                width: ListView.view.width
                height: 30
                color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    CheckBox {
                        checkState: model.checkState === 2 ? Qt.Checked
                                    : model.checkState === 1 ? Qt.PartiallyChecked
                                    : Qt.Unchecked
                        onClicked: if (repoVm) repoVm.setFileChecked(index, checkState !== Qt.Checked)
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.family: "monospace"
                        font.pixelSize: 12
                        textFormat: Text.RichText
                        text: "<font color='" + theme.textMuted + "'>" + model.fileDir + "</font>"
                              + "<font color='" + theme.textPrimary + "'>" + model.fileName + "</font>"
                    }
                    Label {
                        text: model.statusLetter
                        font.family: "monospace"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: model.statusKind === "added" ? theme.stateAdded
                               : model.statusKind === "deleted" ? theme.stateDeleted
                               : model.statusKind === "untracked" ? theme.stateUntracked
                               : theme.stateModified
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: {
                        fileList.currentIndex = index
                        if (repoVm) repoVm.selectFile(model.filePath)
                    }
                }
            }
        }

        // ---- Commit box ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8

            TextField {
                id: commitSummary
                objectName: "commitSummary"
                Layout.fillWidth: true
                placeholderText: "Summary (required)"
                color: theme.textPrimary
                background: Rectangle {
                    radius: 6
                    color: theme.surfaceBase
                    border.color: theme.border
                    border.width: 1
                }
            }
            TextArea {
                id: commitDescription
                objectName: "commitDescription"
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                placeholderText: "Description"
                color: theme.textPrimary
                background: Rectangle {
                    radius: 6
                    color: theme.surfaceBase
                    border.color: theme.border
                    border.width: 1
                }
            }
            Button {
                id: commitButton
                objectName: "commitButton"
                Layout.fillWidth: true
                enabled: repoVm && repoVm.checkedCount > 0 && commitSummary.text.length > 0
                contentItem: Label {
                    text: repoVm
                          ? ("Commit " + repoVm.checkedCount + " file" + (repoVm.checkedCount === 1 ? "" : "s")
                             + " to " + repoVm.currentBranch)
                          : "Commit"
                    color: parent.enabled ? theme.surfaceBase : theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    radius: 6
                    color: parent.enabled ? theme.accent : theme.surfaceOverlay
                }
                onClicked: {
                    if (repoVm) repoVm.commit(commitSummary.text, commitDescription.text)
                }
            }
        }
    }

    // Hairline divider
    Rectangle {
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        color: theme.border
    }

    // ---- Diff column placeholder (filled in Task 6) ----
    Item {
        objectName: "diffColumn"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    // Clear the commit fields once a commit succeeds.
    Connections {
        target: repoVm
        function onCommittedOk() {
            commitSummary.text = ""
            commitDescription.text = ""
        }
    }
}
