import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Merge-in-progress banner. Collapses to height 0 when not visible so the
// ColumnLayout above the Changes list takes no space during normal operation.
// Reads exclusively from the VM's MergeState properties (D30 — disk truth only).
Rectangle {
    id: root
    objectName: "mergeBanner"

    /// The RepoViewModel (or stub) that exposes the MergeState properties.
    property var repo

    visible: repo && repo.mergeInProgress
    height: visible ? 44 : 0
    color: theme.surfaceRaised
    clip: true

    // Left accent bar — conflict token (orange) signals attention.
    Rectangle
    {
        width: 3
        height: parent.height
        color: theme.stateConflict
    }

    RowLayout
    {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12

        Label
        {
            text: "⚠" // ⚠
            color: theme.stateConflict
            font.pixelSize: 16
        }

        Label
        {
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: theme.textPrimary
            font.pixelSize: 13
            text: repo
                  ? ("Merging " + (repo.mergedRef.length ? repo.mergedRef : "branch")
                     + " — " + repo.conflictedCount + " conflicted file"
                     + (repo.conflictedCount === 1 ? "" : "s"))
                  : ""
        }

        AppButton
        {
            objectName: "mergeRetryButton"
            variant: "secondary"
            visible: repo && repo.hasSubmoduleConflicts
            text: "Deinit submodules & retry"
            onClicked: repo.retryMergeDeinitSubmodules()
        }

        Button
        {
            objectName: "mergeAbortButton"
            text: "Abort merge"
            onClicked: repo.abortMerge()
        }

        Button
        {
            objectName: "mergeCommitButton"
            text: "Commit merge"
            enabled: repo && repo.conflictedCount === 0
            onClicked: repo.commitMerge(
                "Merge branch '" + repo.mergedRef + "' into " + repo.currentBranch)
        }
    }
}
