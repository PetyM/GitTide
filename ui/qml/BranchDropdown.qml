import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Grouped, filterable branch switcher. Sections are Local / Worktrees / Remote
// (model "section" role). Clicking a local/worktree row switches to it; remote
// rows are display-only (dimmed). Sentinel rows below a divider raise actions.
Popup {
    id: dropdown
    objectName: "branchDropdown"

    signal newRequested()
    signal renameRequested()
    signal deleteRequested()

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

    background: Rectangle {
        color: theme.surfaceOverlay
        radius: 10
        border.color: theme.border
        border.width: 1
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
                width: ListView.view.width
                height: 34
                color: hover.hovered && !model.remote ? theme.surfaceRaised : "transparent"
                opacity: model.remote ? 0.55 : 1.0

                HoverHandler { id: hover }
                MouseArea {
                    anchors.fill: parent
                    enabled: !model.remote
                    onClicked: {
                        if (repoVm)
                            repoVm.switchBranch(model.branchName)
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
                        else if (modelData.key === "rename") dropdown.renameRequested()
                        else dropdown.deleteRequested()
                    }
                }
            }
        }
    }
}
