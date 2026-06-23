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
                onRenameRequested: (branchName) => branchName ? renameBranchDialog.openFor(branchName) : renameBranchDialog.openDialog()
                onDeleteRequested: (branchName) => branchName ? deleteBranchDialog.openFor(branchName) : deleteBranchDialog.openDialog()
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

        // Transfer progress — a real bar beside the sync buttons. Determinate once
        // libgit2 reports object counts; an animated sweep before that (total 0).
        RowLayout {
            spacing: 8
            visible: repoVm && repoVm.syncing
            ProgressBar {
                id: syncBar
                Layout.preferredWidth: 120
                from: 0
                to: 1
                indeterminate: repoVm && repoVm.syncProgress < 0
                value: (repoVm && repoVm.syncProgress >= 0) ? repoVm.syncProgress : 0
                background: Rectangle {
                    implicitHeight: 6
                    radius: 3
                    color: theme.surfaceOverlay
                }
                contentItem: Item {
                    implicitHeight: 6
                    Rectangle {
                        id: bar
                        height: 6
                        radius: 3
                        color: theme.accent
                        property real indetX: 0
                        x: syncBar.indeterminate ? indetX : 0
                        width: syncBar.indeterminate ? syncBar.width * 0.3
                                                     : syncBar.visualPosition * syncBar.width
                        SequentialAnimation on indetX {
                            running: syncBar.indeterminate
                            loops: Animation.Infinite
                            NumberAnimation { from: 0; to: syncBar.width * 0.7; duration: 850; easing.type: Easing.InOutQuad }
                            NumberAnimation { from: syncBar.width * 0.7; to: 0; duration: 850; easing.type: Easing.InOutQuad }
                        }
                    }
                }
            }
            Label {
                text: (repoVm && repoVm.syncTotal > 0)
                      ? (repoVm.syncReceived + " / " + repoVm.syncTotal)
                      : "Working…"
                color: theme.textSecondary
                font.pixelSize: 12
            }
        }

        Item { Layout.fillWidth: true }

    }

    NewBranchDialog { id: newBranchDialog }
    RenameBranchDialog { id: renameBranchDialog }
    DeleteBranchDialog { id: deleteBranchDialog }
}
