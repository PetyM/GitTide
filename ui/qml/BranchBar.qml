import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: branchBar
    objectName: "branchBar"
    property alias text: branchLabel.text

    implicitHeight: 56
    color: theme.surfaceRaised

    Rectangle { // bottom hairline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.border
    }

    // Equal-size sync action. Optional inline count pill sits beside the label
    // (badgeCount < 0 hides it) so ahead/behind read at a glance without the old
    // corner badge.
    component SyncButton: Button {
        id: sb
        property int badgeCount: -1
        Layout.preferredWidth: 96
        Layout.preferredHeight: 36
        enabled: repoVm && !repoVm.syncing
        contentItem: RowLayout {
            spacing: 6
            Item { Layout.fillWidth: true }
            Label {
                text: sb.text
                color: sb.enabled ? theme.textPrimary : theme.textMuted
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
            }
            Rectangle {
                visible: sb.badgeCount > 0
                implicitWidth: Math.max(20, badgeLabel.implicitWidth + 10)
                implicitHeight: 20
                radius: 10
                color: theme.accent
                Label {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: sb.badgeCount
                    color: theme.surfaceBase
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }
            Item { Layout.fillWidth: true }
        }
        background: Rectangle {
            radius: 6
            color: sb.hovered ? theme.surfaceOverlay : "transparent"
            border.color: theme.border
            border.width: 1
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 10

        // accent-tinted current-branch chip — opens the branch dropdown
        Rectangle {
            id: branchChip
            objectName: "branchChip"
            Layout.preferredHeight: 36
            Layout.preferredWidth: 200
            radius: 6
            color: chipHover.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.22)
                                     : Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.14)
            border.color: theme.accent
            border.width: 1

            HoverHandler { id: chipHover }
            MouseArea {
                anchors.fill: parent
                onClicked: branchDropdown.open()
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                ColumnLayout {
                    id: branchCol
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        id: branchLabel
                        Layout.fillWidth: true
                        text: repoVm ? repoVm.currentBranch : ""
                        color: theme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        text: "Current branch"
                        color: theme.textMuted
                        font.pixelSize: 11
                    }
                }
                Label { // chevron
                    text: "▾"
                    color: theme.textMuted
                    font.pixelSize: 12
                }
            }

            BranchDropdown {
                id: branchDropdown
                y: branchChip.height + 4
                onNewRequested: newBranchDialog.openDialog()
                onRenameRequested: renameBranchDialog.openDialog()
                onDeleteRequested: deleteBranchDialog.openDialog()
            }
        }

        // ---- sync cluster, sitting right beside the branch chip ----

        SyncButton {
            text: "Fetch"
            onClicked: if (repoVm) repoVm.fetch()
        }

        SyncButton { // Pull — behind count inline; only with an upstream
            text: "Pull"
            visible: repoVm && repoVm.hasUpstream
            badgeCount: repoVm ? repoVm.behindCount : -1
            onClicked: if (repoVm) repoVm.pull()
        }

        SyncButton { // Push — ahead count inline; only with an upstream
            text: "Push"
            visible: repoVm && repoVm.hasUpstream
            badgeCount: repoVm ? repoVm.aheadCount : -1
            onClicked: if (repoVm) repoVm.push()
        }

        // Publish — shown only when on a real branch with no upstream
        SyncButton {
            text: "Publish"
            visible: repoVm && !repoVm.hasUpstream && repoVm.onBranch
            onClicked: if (repoVm) repoVm.publishBranch()
        }

        // Busy feedback — sits right beside the sync buttons so an in-flight
        // fetch/pull/push is obvious at the point of action, not in a far corner.
        RowLayout {
            spacing: 6
            visible: repoVm && repoVm.syncing
            BusyIndicator {
                running: repoVm && repoVm.syncing
                implicitWidth: 18
                implicitHeight: 18
            }
            Label {
                text: "Working…"
                color: theme.textSecondary
                font.pixelSize: 12
            }
        }

        Item { Layout.fillWidth: true }

        // Pull-strategy toggle
        Button {
            text: "⋯"
            enabled: repoVm !== null
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
            onClicked: pullMenu.open()

            AppMenu {
                id: pullMenu
                MenuItem {
                    text: "Pull: rebase"
                    checkable: true
                    checked: repoVm && repoVm.pullRebase
                    onToggled: if (repoVm) repoVm.setPullRebase(checked)
                }
            }
        }
    }

    NewBranchDialog { id: newBranchDialog }
    RenameBranchDialog { id: renameBranchDialog }
    DeleteBranchDialog { id: deleteBranchDialog }
}
