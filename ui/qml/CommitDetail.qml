import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "PathElide.js" as PathElide

ColumnLayout {
    id: commitDetail
    spacing: 0

    signal tabBackward()
    signal tabForward()
    function takeFocus() { commitFilesList.forceActiveFocus() }

    // Keep the diff model's syntax theme aligned with the app theme.
    property bool syntaxDark: theme.dark
    onSyntaxDarkChanged: if (repoVm && repoVm.commitDiff) repoVm.commitDiff.setSyntaxDark(syntaxDark)
    Component.onCompleted: if (repoVm && repoVm.commitDiff) repoVm.commitDiff.setSyntaxDark(theme.dark)

    // Range header / hint shown when a multi-commit selection is active.
    Label {
        objectName: "rangeHeaderLabel"
        Layout.fillWidth: true
        visible: repoVm && (repoVm.historyDetailHeader.length > 0 || repoVm.historyDetailHint.length > 0)
        text: repoVm ? (repoVm.historyDetailHint.length > 0
                        ? repoVm.historyDetailHint
                        : repoVm.historyDetailHeader) : ""
        color: repoVm && repoVm.historyDetailHint.length > 0 ? theme.textMuted : theme.textPrimary
        wrapMode: Text.WordWrap
        padding: 8
    }

    // Commit medallion: summary, body, author, stats, copyable hash.
    // Shown only for a single-commit selection (range/stash keep their own header).
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        spacing: 6
        visible: repoVm && repoVm.selectedCommit.length > 0
                 && repoVm.historyDetailHeader.length === 0
                 && repoVm.historyDetailHint.length === 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: repoVm ? repoVm.detailSummary : ""
                color: theme.textPrimary
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            AppButton {
                objectName: "checkoutCommitButton"
                variant: "secondary"
                text: "Checkout"
                onClicked: if (repoVm) repoVm.checkoutCommit(repoVm.selectedCommit)
            }
        }

        Label {
            Layout.fillWidth: true
            visible: repoVm && repoVm.detailBody.length > 0
            text: repoVm ? repoVm.detailBody : ""
            color: theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: repoVm ? (repoVm.detailAuthor
                            + (repoVm.detailAuthorEmail.length > 0
                               ? " <" + repoVm.detailAuthorEmail + ">" : "")
                            + "  ·  " + repoVm.detailDate) : ""
            color: theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        RowLayout {
            spacing: 12
            Label {
                text: (repoVm ? repoVm.detailFilesChanged : 0)
                      + (repoVm && repoVm.detailFilesChanged === 1 ? " file changed" : " files changed")
                color: theme.textMuted
                font.pixelSize: 11
            }
            Label {
                text: "+" + (repoVm ? repoVm.detailAdditions : 0)
                color: theme.stateAdded
                font.family: "monospace"
                font.pixelSize: 11
            }
            Label {
                text: "−" + (repoVm ? repoVm.detailDeletions : 0)
                color: theme.stateDeleted
                font.family: "monospace"
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            spacing: 6
            Label {
                text: repoVm && repoVm.selectedCommit.length > 0
                      ? repoVm.selectedCommit.substring(0, 10) : ""
                color: theme.textMuted
                font.family: "monospace"
                font.pixelSize: 11
            }
            AppButton {
                objectName: "copyHashButton"
                variant: "secondary"
                text: "Copy"
                visible: repoVm && repoVm.selectedCommit.length > 0
                onClicked: if (repoVm) repoVm.copyToClipboard(repoVm.selectedCommit)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy full commit hash")
            }
        }
    }

    // Files (top) and the read-only diff (bottom) are split by a draggable handle
    // that mirrors the Changes pane's separator (accent on hover) — a clear,
    // consistent divide between the changed-files list and the selected diff.
    SplitView {
        orientation: Qt.Vertical
        Layout.fillWidth: true
        Layout.fillHeight: true

        handle: Rectangle {
            implicitHeight: 3
            color: detailHandleHover.hovered ? theme.accent : theme.border
            HoverHandler { id: detailHandleHover }
        }

        // ---- Files in the commit (read-only) ----
        Item {
            SplitView.preferredHeight: 160
            SplitView.minimumHeight: 80

            ListView {
                id: commitFilesList
                objectName: "commitFilesList"
                anchors.fill: parent
                clip: true
                model: repoVm ? repoVm.commitFiles : null

                ScrollBar.vertical: AppScrollBar {}
                WheelScroller {}

                activeFocusOnTab: true
                Keys.onUpPressed: {
                    if (currentIndex > 0) {
                        currentIndex--
                        if (repoVm) repoVm.selectCommitFileAtRow(currentIndex)
                    }
                }
                Keys.onDownPressed: {
                    if (currentIndex < count - 1) {
                        currentIndex++
                        if (repoVm) repoVm.selectCommitFileAtRow(currentIndex)
                    }
                }
                Keys.onTabPressed: {
                    commitDetail.tabForward()
                    event.accepted = true
                }
                Keys.onBacktabPressed: {
                    commitDetail.tabBackward()
                    event.accepted = true
                }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 28
                    color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            commitFilesList.forceActiveFocus()   // arrows work right after a click
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
                            id: pathLabel
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.family: "monospace"
                            font.pixelSize: 12
                            textFormat: Text.RichText
                            // Abbreviate dir segments only as much as the width needs;
                            // see PathElide.js. Hidden ruler measures in the same font.
                            TextMetrics { id: pathRuler; font: pathLabel.font }
                            readonly property string shortDir: PathElide.fit(
                                model.fileDir, model.fileName, width,
                                function (t) { pathRuler.text = t; return pathRuler.advanceWidth })
                            text: "<font color='" + theme.textMuted + "'>" + shortDir + "</font>"
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

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: commitFilesList.activeFocus ? theme.focusBorder : "transparent"
                border.width: 1
                enabled: false
            }
        }

        // ---- Read-only diff (no per-line checkboxes) ----
        ListView {
            id: commitDiffList
            objectName: "commitDiffList"
            SplitView.fillHeight: true
            SplitView.minimumHeight: 120
            clip: true
            model: repoVm ? repoVm.commitDiff : null

            ScrollBar.vertical: AppScrollBar {}
            WheelScroller {}

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
                        clip: true
                        textFormat: model.lineHtml && model.lineHtml.length > 0
                                    ? Text.RichText : Text.PlainText
                        text: model.lineHtml && model.lineHtml.length > 0
                              ? model.lineHtml : model.lineText
                        color: model.lineKind === "hunk"   ? theme.textMuted
                             : (model.lineHtml && model.lineHtml.length > 0) ? theme.textPrimary
                             : model.lineKind === "added"  ? theme.stateAdded
                             : model.lineKind === "removed" ? theme.stateDeleted
                             : theme.textPrimary
                    }
                }
            }
        }
    }
}
