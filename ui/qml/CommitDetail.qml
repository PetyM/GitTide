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

    // Header: selected commit short-oid (empty when nothing selected).
    // Hidden when a range header or hint is active (range mode uses its own header).
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        visible: repoVm && repoVm.historyDetailHeader.length === 0
                 && repoVm.historyDetailHint.length === 0
        Label {
            Layout.fillWidth: true
            text: repoVm && repoVm.selectedCommit.length > 0
                  ? ("Commit " + repoVm.selectedCommit.substring(0, 7))
                  : "Select a commit"
            color: theme.textSecondary
            font.family: "monospace"
            font.pixelSize: 12
        }
        AppButton {
            objectName: "checkoutCommitButton"
            variant: "secondary"
            visible: repoVm && repoVm.selectedCommit.length > 0
                     && repoVm.historyDetailHeader.length === 0
                     && repoVm.historyDetailHint.length === 0
            text: "Checkout"
            onClicked: if (repoVm) repoVm.checkoutCommit(repoVm.selectedCommit)
        }
    }

    // ---- Files in the commit (read-only) ----
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 160

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

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

    // ---- Read-only diff (no per-line checkboxes) ----
    ListView {
        id: commitDiffList
        objectName: "commitDiffList"
        Layout.fillWidth: true
        Layout.fillHeight: true
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
