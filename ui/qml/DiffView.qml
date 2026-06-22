import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: diffView
    objectName: "diffView"
    spacing: 0

    // ---- Header: master line checkbox + active file path ----
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        spacing: 10

        AppCheckBox {
            id: diffHeaderCheck
            objectName: "diffHeaderCheck"
            tristate: true
            property bool allOn: false
            visible: repoVm && repoVm.activeFile.length > 0
            checkState: allOn ? Qt.Checked : Qt.PartiallyChecked
            onClicked: {
                allOn = !allOn
                if (repoVm) repoVm.setAllLinesChecked(allOn)
            }
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            font.family: "monospace"
            font.pixelSize: 12
            color: theme.textSecondary
            text: repoVm ? repoVm.activeFile : ""
        }
    }

    ListView {
        id: diffList
        objectName: "diffList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: repoVm ? repoVm.diffLines : null

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

                // Per-line checkbox column (only for changed lines)
                Item {
                    Layout.preferredWidth: 22
                    Layout.fillHeight: true
                    AppCheckBox {
                        anchors.centerIn: parent
                        visible: model.checkable
                        checked: model.lineChecked
                        accentColor: model.lineKind === "added" ? theme.stateAdded
                                     : model.lineKind === "removed" ? theme.stateDeleted
                                     : theme.accent
                        onClicked: if (repoVm) repoVm.setLineChecked(index, !model.lineChecked)
                    }
                }

                // Old/new line-number gutter
                Label {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignRight
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: theme.textMuted
                    text: model.lineKind === "hunk" ? ""
                          : (model.oldNo > 0 ? model.oldNo : "") + " " + (model.newNo > 0 ? model.newNo : "")
                }

                // Sign
                Label {
                    Layout.preferredWidth: 10
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: model.lineKind === "added" ? "+" : model.lineKind === "removed" ? "−" : ""
                    color: model.lineKind === "added" ? theme.stateAdded
                           : model.lineKind === "removed" ? theme.stateDeleted
                           : theme.textMuted
                }

                // Code / hunk header text
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
