import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Grouped, filterable branch switcher. Sections are Local / Worktrees / Remote
// (model "section" role). Clicking a local/worktree row switches to it; clicking
// a remote row checks it out (DWIM: create a tracking local branch or switch to
// an existing one). Sentinel rows below a divider raise actions.
Popup {
    id: dropdown
    objectName: "branchDropdown"

    signal newRequested()
    signal renameRequested(string branchName)
    signal deleteRequested(string branchName)

    width: 320
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Reset the filter each time the dropdown opens.
    onOpened: {
        if (repoVm)
            repoVm.branches.filter = ""
        filterField.text = ""
        filterField.forceActiveFocus()
    }

    background: OverlayCard {
        color: theme.surfaceOverlay
        radius: 10
    }

    BranchContextMenu {
        id: branchMenu
        onSwitchBranch:      { if (repoVm) { if (branchMenu.isRemote) repoVm.checkoutRemoteBranch(branchMenu.branchName); else repoVm.switchBranch(branchMenu.branchName) }; dropdown.close() }
        onNewBranchFromHere: { dropdown.newRequested(); dropdown.close() }
        onRename:            { dropdown.renameRequested(branchMenu.branchName); dropdown.close() }
        onDeleteBranch:      { dropdown.deleteRequested(branchMenu.branchName); dropdown.close() }
        onCopyName:          { if (repoVm) repoVm.copyToClipboard(branchMenu.branchName); dropdown.close() }
        onMerge:             { if (repoVm) repoVm.startMerge(branchMenu.branchName); dropdown.close() }
        onRebase:            { if (repoVm) repoVm.startRebase(branchMenu.branchName); dropdown.close() }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // ---- Filter field ----
        TextField {
            id: filterField
            objectName: "branchFilterField"
            Layout.fillWidth: true
            Layout.margins: 8
            placeholderText: "Filter branches"
            color: theme.textPrimary
            onTextChanged: if (repoVm) repoVm.branches.filter = text
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: theme.border
                border.width: 1
            }
        }

        // ---- Branch list (sectioned) ----
        ListView {
            id: branchList
            objectName: "branchList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 320)
            clip: true
            model: repoVm ? repoVm.branches : null

            section.property: "section"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                width: ListView.view.width
                height: 24
                color: theme.surfaceRaised
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    text: section
                    color: theme.textMuted
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            delegate: Rectangle {
                id: branchDelegate
                width: ListView.view.width
                height: 34
                color: hover.hovered ? theme.surfaceRaised : "transparent"
                // Remote-only rows stay slightly dimmed to read as "not yet local",
                // but are actionable: clicking checks the remote branch out (DWIM —
                // create a tracking local branch, or switch if one already exists).
                opacity: model.remote ? 0.8 : 1.0

                HoverHandler { id: hover }
                // Both buttons handled by one MouseArea. A MouseArea takes an
                // exclusive grab on press and consumes the event, so a right-click
                // never leaks through this non-modal popup to a right-click handler
                // in the view behind (e.g. HistoryPane's commit context menu).
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            branchMenu.branchName = model.branchName
                            branchMenu.isHead     = model.isHead
                            branchMenu.isRemote   = model.remote
                            branchMenu.popup()
                            return
                        }
                        if (repoVm) {
                            if (model.remote)
                                repoVm.checkoutRemoteBranch(model.branchName)
                            else
                                repoVm.switchBranch(model.branchName)
                        }
                        dropdown.close()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    // Current-branch marker.
                    Label {
                        text: model.isHead ? "✓" : (model.remote ? "☁" : "")
                        color: model.isHead ? theme.accent : theme.textMuted
                        font.pixelSize: 12
                        Layout.preferredWidth: 14
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: model.branchName
                        color: model.isHead ? theme.accent : theme.textPrimary
                        font.pixelSize: 13
                        font.weight: model.isHead ? Font.DemiBold : Font.Normal
                    }
                    // Worktree path (when present).
                    Label {
                        visible: model.worktreePath.length > 0
                        text: model.worktreePath
                        color: theme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideLeft
                        Layout.maximumWidth: 120
                    }
                }
            }
        }

        // ---- Divider ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.border
        }

        // ---- Action sentinels ----
        Repeater {
            model: [
                { key: "new", label: "New branch…", obj: "newBranchSentinel" },
                { key: "rename", label: "Rename branch…", obj: "renameBranchSentinel" },
                { key: "delete", label: "Delete branch…", obj: "deleteBranchSentinel" }
            ]
            delegate: Rectangle {
                required property var modelData
                objectName: modelData.obj
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                color: sentinelHover.hovered ? theme.surfaceRaised : "transparent"
                HoverHandler { id: sentinelHover }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    text: modelData.label
                    color: modelData.key === "delete" ? theme.stateDeleted : theme.textPrimary
                    font.pixelSize: 13
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        dropdown.close()
                        if (modelData.key === "new") dropdown.newRequested()
                        else if (modelData.key === "rename") dropdown.renameRequested("")
                        else dropdown.deleteRequested("")
                    }
                }
            }
        }
    }
}
