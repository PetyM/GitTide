import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "PathElide.js" as PathElide

// Resizable two-pane layout: drag the handle to trade width between the
// file/commit column and the diff. The diff keeps a generous minimum so it
// never gets squeezed away.
SplitView {
    id: changesPane
    objectName: "changesPane"
    orientation: Qt.Horizontal

    // Public API used by WorkingPane global shortcuts (spec §2.2).
    function takeFocus() { fileList.forceActiveFocus() }
    // Entry from the Tab chain in reverse — land on the last element.
    function takeFocusLast() { commitDescription.forceActiveFocus() }
    readonly property bool commitSummaryActive:     commitSummary.activeFocus
    readonly property bool commitDescriptionActive: commitDescription.activeFocus

    // Tab handoff to the surrounding chain (sidebar repo tree). tabNext fires when
    // Tab moves past the last element; tabPrev when Shift+Tab moves before the first.
    signal tabNext()
    signal tabPrev()

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
                // Hidden in stash preview (read-only file list).
                visible: !repoVm || !repoVm.stashPreviewActive
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

        // ---- Files list with focus-ring overlay ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: fileList
                objectName: "fileList"
                anchors.fill: parent
                clip: true
                // In stash preview show the stash's snapshot files; otherwise working changes.
                model: repoVm ? (repoVm.stashPreviewActive ? repoVm.commitFiles : repoVm.changedFiles) : null

                ScrollBar.vertical: AppScrollBar {}
                WheelScroller {}

                // Keyboard navigation (spec §2.3). Tab order is driven by explicit
                // Keys handlers (not KeyNavigation) so the multi-line description
                // TextArea can't swallow Tab and trap focus.
                activeFocusOnTab: true
                Keys.onTabPressed: { commitSummary.forceActiveFocus(); event.accepted = true }
                Keys.onBacktabPressed: { changesPane.tabPrev(); event.accepted = true }
                Keys.onUpPressed: {
                    if (currentIndex > 0) {
                        currentIndex--
                        if (repoVm) repoVm.selectFileAtRow(currentIndex)
                    }
                }
                Keys.onDownPressed: {
                    if (currentIndex < count - 1) {
                        currentIndex++
                        if (repoVm) repoVm.selectFileAtRow(currentIndex)
                    }
                }
                Keys.onSpacePressed: {
                    if (currentIndex >= 0 && repoVm && currentItem)
                        repoVm.setFileChecked(currentIndex, currentItem.fileCheckState !== 2)
                }

                delegate: Rectangle {
                    // Expose checkState so Keys.onSpacePressed can read it via currentItem.
                    property int fileCheckState: model.checkState

                    width: ListView.view.width
                    height: 30
                    // Current row uses the selection highlight; otherwise a faint
                    // status tint — green for added/untracked, red for deleted.
                    color: ListView.isCurrentItem ? theme.surfaceOverlay
                         : (model.statusKind === "added" || model.statusKind === "untracked")
                           ? Qt.rgba(theme.stateAdded.r, theme.stateAdded.g, theme.stateAdded.b, 0.12)
                         : model.statusKind === "deleted"
                           ? Qt.rgba(theme.stateDeleted.r, theme.stateDeleted.g, theme.stateDeleted.b, 0.12)
                         : "transparent"

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onClicked: {
                            fileList.forceActiveFocus()   // arrows work right after a click
                            fileList.currentIndex = index
                            if (repoVm) {
                                if (repoVm.stashPreviewActive)
                                    repoVm.selectCommitFile(model.filePath)
                                else
                                    repoVm.selectFile(model.filePath)
                            }
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
                            // Hidden in stash preview (read-only).
                            visible: !repoVm || !repoVm.stashPreviewActive
                            // A checked submodule whose working tree is dirty renders
                            // as partial (a dash): committing moves its pointer to the
                            // submodule's last commit, leaving the uncommitted changes
                            // behind in the submodule. Clean/normal rows map directly.
                            checkState: (model.isSubmodule && model.submoduleDirty && model.checkState === 2)
                                          ? Qt.PartiallyChecked
                                        : model.checkState === 2 ? Qt.Checked
                                        : model.checkState === 1 ? Qt.PartiallyChecked
                                        : Qt.Unchecked
                            onClicked: if (repoVm) repoVm.setFileChecked(index, model.checkState !== 2)
                            ToolTip.visible: hovered && model.isSubmodule && model.submoduleDirty
                            ToolTip.text: qsTr("Submodule has uncommitted changes — committing moves its pointer to the last commit; the changes stay in the submodule.")
                        }
                        Label {
                            id: pathLabel
                            Layout.fillWidth: true
                            // StyledText (unlike RichText) honours elide, so a long
                            // path truncates instead of overrunning the status letter.
                            elide: Text.ElideMiddle
                            font.family: "monospace"
                            font.pixelSize: 12
                            textFormat: Text.StyledText
                            // File name tinted by status — added/untracked green,
                            // deleted red — so the change kind reads at a glance;
                            // the directory prefix stays muted.
                            readonly property color nameColor:
                                  (model.statusKind === "added" || model.statusKind === "untracked") ? theme.stateAdded
                                : model.statusKind === "deleted"   ? theme.stateDeleted
                                : theme.textPrimary
                            // Hidden ruler: measures candidate strings in the same
                            // font so the dir prefix is abbreviated only as much as
                            // the available width demands (full path when it fits).
                            TextMetrics { id: pathRuler; font: pathLabel.font }
                            readonly property string shortDir: PathElide.fit(
                                model.fileDir, model.fileName, width,
                                function (t) { pathRuler.text = t; return pathRuler.advanceWidth })
                            text: "<font color='" + theme.textMuted + "'>" + shortDir + "</font>"
                                  + "<font color='" + nameColor + "'>" + model.fileName + "</font>"
                        }
                        Label {
                            text: model.statusLetter
                            font.family: "monospace"
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            // Untracked reads as a new file → green, same as added.
                            color: (model.statusKind === "added" || model.statusKind === "untracked") ? theme.stateAdded
                                   : model.statusKind === "deleted" ? theme.stateDeleted
                                   : theme.stateModified
                        }
                    }
                }
            }

            // Focus ring — overlay Rectangle whose 1px border lights up when fileList
            // has active focus. Drawn on top so it never insets the list content.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: fileList.activeFocus ? theme.focusBorder : "transparent"
                border.width: 1
                // Pointer-transparent so mouse events pass through to the ListView.
                enabled: false
            }
        }

        // ---- Stash stack panel (collapsible; hidden when empty) ----
        StashPanel {
            Layout.fillWidth: true
        }

        // ---- Commit box (hidden during stash preview) ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            visible: !repoVm || !repoVm.stashPreviewActive

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
                // BeforeItem so our Tab handling wins over the editor's own.
                Keys.priority: Keys.BeforeItem
                Keys.onTabPressed: { commitDescription.forceActiveFocus(); event.accepted = true }
                Keys.onBacktabPressed: { fileList.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: {
                    if ((event.modifiers & Qt.ControlModifier) && commitButton.enabled) {
                        repoVm.commit(commitSummary.text, commitDescription.text)
                        event.accepted = true
                    }
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
                // BeforeItem so Tab leaves the multi-line field instead of being
                // swallowed as input — the focus-trap bug this fixes.
                Keys.priority: Keys.BeforeItem
                Keys.onTabPressed: { changesPane.tabNext(); event.accepted = true }
                Keys.onBacktabPressed: { commitSummary.forceActiveFocus(); event.accepted = true }
                Keys.onReturnPressed: {
                    if ((event.modifiers & Qt.ControlModifier) && commitButton.enabled) {
                        repoVm.commit(commitSummary.text, commitDescription.text)
                        event.accepted = true
                    }
                    // else: default TextArea behaviour inserts newline — do not accept event.
                }
            }
            AppButton {
                id: commitButton
                objectName: "commitButton"
                variant: "primary"
                Layout.fillWidth: true
                enabled: repoVm && repoVm.checkedCount > 0 && commitSummary.text.length > 0
                text: repoVm
                      ? ("Commit " + repoVm.checkedCount + " file" + (repoVm.checkedCount === 1 ? "" : "s")
                         + " to " + repoVm.currentBranch)
                      : "Commit"
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
