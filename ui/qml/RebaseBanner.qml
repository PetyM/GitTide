import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Rebase-in-progress banner. Collapses to height 0 when not rebasing. Reads only
// the VM's RebaseState properties (D30 — disk truth only). Mutually exclusive with
// MergeBanner: rebase and merge are never both in progress.
Rectangle {
    id: root
    objectName: "rebaseBanner"

    property var repo

    visible: repo && repo.rebaseInProgress
    height: visible ? 44 : 0
    color: theme.surfaceRaised
    clip: true

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
            text: "⤴"
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
                  ? ("Rebasing onto " + (repo.rebaseOnto.length ? repo.rebaseOnto : "target")
                     + " — step " + repo.rebaseStep + "/" + repo.rebaseTotal
                     + (repo.rebaseStepSummary.length ? (" — " + repo.rebaseStepSummary) : "")
                     + (repo.rebaseConflictedCount > 0
                        ? (" — " + repo.rebaseConflictedCount + " conflicted file"
                           + (repo.rebaseConflictedCount === 1 ? "" : "s"))
                        : ""))
                  : ""
        }

        Button
        {
            objectName: "rebaseContinueButton"
            text: "Continue"
            enabled: repo && repo.rebaseConflictedCount === 0
            onClicked: repo.continueRebase()
        }

        Button
        {
            objectName: "rebaseSkipButton"
            text: "Skip"
            onClicked: repo.skipRebase()
        }

        Button
        {
            objectName: "rebaseAbortButton"
            text: "Abort rebase"
            onClicked: repo.abortRebase()
        }
    }
}
