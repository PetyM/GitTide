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
            // Hidden when no file active and during stash preview (read-only).
            visible: repoVm && repoVm.activeFile.length > 0 && !repoVm.stashPreviewActive
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
            // In stash preview show the stash label; otherwise the active file path.
            text: repoVm
                  ? (repoVm.stashPreviewActive
                     ? ("Preview: " + repoVm.stashPreviewLabel)
                     : repoVm.activeFile)
                  : ""
        }
        // Exit-preview bar: visible only in stash-preview mode.
        AppButton {
            objectName: "stashPreviewBar"
            variant: "secondary"
            compact: true
            text: qsTr("Exit Preview")
            visible: repoVm && repoVm.stashPreviewActive
            onClicked: if (repoVm) repoVm.exitStashPreview()
        }
    }

    ListView {
        id: diffList
        objectName: "diffList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        // In stash preview show the commit (stash) diff; otherwise the working diff.
        model: repoVm ? (repoVm.stashPreviewActive ? repoVm.commitDiff : repoVm.diffLines) : null

        ScrollBar.vertical: AppScrollBar {}
        WheelScroller {}

        delegate: Rectangle {
            id: diffRow
            width: ListView.view.width
            // Conflict-start rows are taller to accommodate the action header.
            height: model.lineKind === "conflict-start" ? 44 : 20
            color: model.lineKind === "added"   ? Qt.rgba(theme.stateAdded.r,     theme.stateAdded.g,     theme.stateAdded.b,     0.12)
                 : model.lineKind === "removed" ? Qt.rgba(theme.stateDeleted.r,   theme.stateDeleted.g,   theme.stateDeleted.b,   0.12)
                 : model.lineKind === "hunk"    ? theme.surfaceOverlay
                 : model.lineKind === "ours"    ? Qt.rgba(theme.stateAdded.r,     theme.stateAdded.g,     theme.stateAdded.b,     0.10)
                 : model.lineKind === "theirs"  ? Qt.rgba(theme.stateIncoming.r,  theme.stateIncoming.g,  theme.stateIncoming.b,  0.10)
                 : model.lineKind === "conflict-start" ? Qt.rgba(theme.stateAdded.r, theme.stateAdded.g, theme.stateAdded.b, 0.06)
                 : model.lineKind === "conflict-sep"   ? theme.surfaceOverlay
                 : model.lineKind === "conflict-end"   ? Qt.rgba(theme.stateIncoming.r, theme.stateIncoming.g, theme.stateIncoming.b, 0.06)
                 : "transparent"

            // Per-region action header rendered on "conflict-start" rows.
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: model.lineKind === "conflict-start"

                // Top line: marker text
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 96
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: model.lineText
                    color: theme.textMuted
                    elide: Text.ElideRight
                }

                // Bottom line: Accept action buttons
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 96
                    spacing: 6

                    AppButton {
                        objectName: "acceptCurrentButton"
                        variant: "secondary"
                        compact: true
                        text: qsTr("Accept Current")
                        onClicked: if (repoVm) repoVm.acceptConflict(model.conflictRegion, 0)
                    }
                    AppButton {
                        objectName: "acceptIncomingButton"
                        variant: "secondary"
                        compact: true
                        text: qsTr("Accept Incoming")
                        onClicked: if (repoVm) repoVm.acceptConflict(model.conflictRegion, 1)
                    }
                    AppButton {
                        objectName: "acceptBothButton"
                        variant: "secondary"
                        compact: true
                        text: qsTr("Accept Both")
                        onClicked: if (repoVm) repoVm.acceptConflict(model.conflictRegion, 2)
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // Normal diff row content (hidden on conflict-start rows which use the
            // ColumnLayout above).
            RowLayout {
                anchors.fill: parent
                spacing: 6
                visible: model.lineKind !== "conflict-start"

                // Per-line checkbox column (changed lines) + block checkbox (block rows)
                Item {
                    Layout.preferredWidth: 22
                    Layout.fillHeight: true
                    AppCheckBox {
                        anchors.centerIn: parent
                        // Hidden when line is not checkable OR when in stash preview (read-only).
                        visible: model.checkable && (!repoVm || !repoVm.stashPreviewActive)
                        checked: model.lineChecked
                        accentColor: model.lineKind === "added" ? theme.stateAdded
                                     : model.lineKind === "removed" ? theme.stateDeleted
                                     : theme.accent
                        onClicked: if (repoVm) repoVm.setLineChecked(index, !model.lineChecked)
                    }
                    AppCheckBox {
                        anchors.centerIn: parent
                        objectName: "diffBlockCheck"
                        visible: model.lineKind === "block"
                        tristate: true
                        checkState: model.blockState
                        onClicked: if (repoVm) repoVm.setBlockChecked(index, model.blockState !== Qt.Checked)
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

                // Code / hunk header / conflict marker text
                Label {
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    text: model.lineText
                    color: model.lineKind === "hunk"         ? theme.textMuted
                         : model.lineKind === "added"        ? theme.stateAdded
                         : model.lineKind === "removed"      ? theme.stateDeleted
                         : model.lineKind === "ours"         ? theme.stateAdded
                         : model.lineKind === "theirs"       ? theme.stateIncoming
                         : model.lineKind === "conflict-sep" ? theme.textMuted
                         : model.lineKind === "conflict-end" ? theme.textMuted
                         : theme.textPrimary
                }
            }
        }
    }
}
