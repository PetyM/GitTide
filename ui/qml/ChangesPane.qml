import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Resizable two-pane layout: drag the handle to trade width between the
// file/commit column and the diff. The diff keeps a generous minimum so it
// never gets squeezed away.
SplitView {
    id: changesPane
    objectName: "changesPane"
    orientation: Qt.Horizontal

    handle: Rectangle {
        implicitWidth: 3
        // HoverHandler (rather than the SplitView attached props) keeps the
        // highlight binding clear of SplitView's handle init-order warning.
        color: handleHover.hovered ? theme.accent : theme.border
        HoverHandler { id: handleHover }
    }

    // ---- Files + commit column (resizable) ----
    ColumnLayout {
        SplitView.preferredWidth: 300
        SplitView.minimumWidth: 240
        spacing: 0

        // Header with tri-state master checkbox + count
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 10

            AppCheckBox {
                objectName: "fileHeaderCheck"
                tristate: true
                // 0 Unchecked, 1 PartiallyChecked, 2 Checked — mirror file states.
                checkState: {
                    var n = repoVm ? repoVm.checkedCount : 0
                    var total = fileList.count
                    if (n === 0) return Qt.Unchecked
                    if (n === total) return Qt.Checked
                    return Qt.PartiallyChecked
                }
                onClicked: {
                    var allChecked = repoVm && fileList.count > 0 && repoVm.checkedCount === fileList.count
                    if (repoVm) repoVm.setAllFilesChecked(!allChecked)
                }
            }
            Label {
                text: "Changed files"
                color: theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: repoVm ? (repoVm.checkedCount + " / " + fileList.count) : ""
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

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: {
                        fileList.currentIndex = index
                        if (repoVm) repoVm.selectFile(model.filePath)
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        fileMenu.filePath   = model.filePath
                        fileMenu.fileName   = model.fileName
                        fileMenu.statusKind = model.statusKind
                        fileMenu.checkState = model.checkState
                        fileMenu.rowIndex   = index
                        fileMenu.popup()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    AppCheckBox {
                        checkState: model.checkState === 2 ? Qt.Checked
                                    : model.checkState === 1 ? Qt.PartiallyChecked
                                    : Qt.Unchecked
                        onClicked: if (repoVm) repoVm.setFileChecked(index, model.checkState !== 2)
                    }
                    Label {
                        Layout.fillWidth: true
                        // StyledText (unlike RichText) honours elide, so a long
                        // path truncates instead of overrunning the status letter.
                        elide: Text.ElideMiddle
                        font.family: "monospace"
                        font.pixelSize: 12
                        textFormat: Text.StyledText
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
                wrapMode: TextArea.Wrap
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

    // ---- Diff column (takes the remaining width) ----
    DiffView {
        objectName: "diffColumn"
        SplitView.fillWidth: true
        SplitView.minimumWidth: 360
    }

    // Clear the commit fields once a commit succeeds.
    Connections {
        target: repoVm
        function onCommittedOk() {
            commitSummary.text = ""
            commitDescription.text = ""
        }
    }

    // ---- File context menu (right-click on a changed file row) ----
    FileContextMenu {
        id: fileMenu
        onStage:               if (repoVm) repoVm.setFileChecked(fileMenu.rowIndex, true)
        onUnstage:             if (repoVm) repoVm.setFileChecked(fileMenu.rowIndex, false)
        onDiscard:             discardDialog.open()
        onOpenInEditor:        if (repoVm) repoVm.openInEditor(fileMenu.filePath)
        onRevealInFileManager: if (repoVm) repoVm.revealInFileManager(fileMenu.filePath)
        onCopyPath:            if (repoVm) repoVm.copyToClipboard(fileMenu.filePath)
    }

    DiscardChangesDialog {
        id: discardDialog
        fileName: fileMenu.fileName
        onAccepted: if (repoVm) repoVm.discardFile(fileMenu.filePath)
    }
}
