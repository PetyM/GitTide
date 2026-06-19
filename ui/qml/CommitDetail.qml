import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: commitDetail
    spacing: 0

    // Header: selected commit short-oid (empty when nothing selected).
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        Label {
            Layout.fillWidth: true
            text: repoVm && repoVm.selectedCommit.length > 0
                  ? ("Commit " + repoVm.selectedCommit.substring(0, 7))
                  : "Select a commit"
            color: theme.textSecondary
            font.family: "monospace"
            font.pixelSize: 12
        }
        Button {
            objectName: "checkoutCommitButton"
            visible: repoVm && repoVm.selectedCommit.length > 0
            text: "Checkout"
            contentItem: Label {
                text: parent.text
                color: theme.textPrimary
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 6
                color: parent.hovered ? theme.surfaceOverlay : "transparent"
                border.color: theme.border
                border.width: 1
            }
            onClicked: if (repoVm) repoVm.checkoutCommit(repoVm.selectedCommit)
        }
    }

    // ---- Files in the commit (read-only) ----
    ListView {
        id: commitFilesList
        objectName: "commitFilesList"
        Layout.fillWidth: true
        Layout.preferredHeight: 160
        clip: true
        model: repoVm ? repoVm.commitFiles : null

        delegate: Rectangle {
            width: ListView.view.width
            height: 28
            color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    commitFilesList.currentIndex = index
                    if (repoVm) repoVm.selectCommitFile(model.filePath)
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
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
        }
    }

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

    // ---- Read-only diff (no per-line checkboxes) ----
    ListView {
        id: commitDiffList
        objectName: "commitDiffList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: repoVm ? repoVm.commitDiff : null

        delegate: Rectangle {
            width: ListView.view.width
            height: 20
            color: model.lineKind === "added" ? Qt.rgba(theme.stateAdded.r, theme.stateAdded.g, theme.stateAdded.b, 0.12)
                   : model.lineKind === "removed" ? Qt.rgba(theme.stateDeleted.r, theme.stateDeleted.g, theme.stateDeleted.b, 0.12)
                   : model.lineKind === "hunk" ? theme.surfaceOverlay
                   : "transparent"

            RowLayout {
                anchors.fill: parent
                spacing: 6
                Label {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignRight
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: theme.textMuted
                    text: model.lineKind === "hunk" ? ""
                          : (model.oldNo > 0 ? model.oldNo : "") + " " + (model.newNo > 0 ? model.newNo : "")
                }
                Label {
                    Layout.preferredWidth: 10
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: model.lineKind === "added" ? "+" : model.lineKind === "removed" ? "−" : ""
                    color: model.lineKind === "added" ? theme.stateAdded
                           : model.lineKind === "removed" ? theme.stateDeleted
                           : theme.textMuted
                }
                Label {
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    text: model.lineText
                    color: model.lineKind === "hunk" ? theme.textMuted
                           : model.lineKind === "added" ? theme.stateAdded
                           : model.lineKind === "removed" ? theme.stateDeleted
                           : theme.textPrimary
                }
            }
        }
    }
}
