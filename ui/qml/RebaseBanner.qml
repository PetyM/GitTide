import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Rebase-in-progress banner. Collapses to height 0 when not rebasing. Reads only
// the VM's RebaseState properties (D30 — disk truth only). Mutually exclusive with
// MergeBanner: rebase and merge are never both in progress.
//
// Pause-reason variants:
//   - "message" (interactive rebase pause for editing commit message):
//       headline says "Rebasing — step N/T — editing message (summary)";
//       Continue always enabled; clicking Continue emits requestMessageEdit()
//       so the host (HistoryPane) can open RewordDialog prefilled.
//   - "" / other (conflict pause):
//       headline says "Rebasing onto … — step N/T — M conflicted files";
//       Continue enabled only when rebaseConflictedCount == 0;
//       clicking Continue calls repo.continueRebase() directly.
// Abort is present in both pause states.
Rectangle {
    id: root
    objectName: "rebaseBanner"

    property var repo

    // Emitted when pauseReason == "message" and the user clicks Continue.
    // The host should open RewordDialog prefilled from repoVm.rebaseMessagePrefill,
    // then call repoVm.continueRebase(message) on save.
    signal requestMessageEdit()

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
                  ? (repo.rebasePauseReason === "message"
                     ? ("Rebasing — step " + repo.rebaseStep + "/" + repo.rebaseTotal
                        + " — editing message"
                        + (repo.rebaseStepSummary.length ? (" (" + repo.rebaseStepSummary + ")") : ""))
                     : ("Rebasing onto " + (repo.rebaseOnto.length ? repo.rebaseOnto : "target")
                        + " — step " + repo.rebaseStep + "/" + repo.rebaseTotal
                        + (repo.rebaseStepSummary.length ? (" — " + repo.rebaseStepSummary) : "")
                        + (repo.rebaseConflictedCount > 0
                           ? (" — " + repo.rebaseConflictedCount + " conflicted file"
                              + (repo.rebaseConflictedCount === 1 ? "" : "s"))
                           : "")))
                  : ""
        }

        Button
        {
            objectName: "rebaseContinueButton"
            text: "Continue"
            // Conflict pause: enabled only when nothing is unresolved.
            // Message pause: always enabled (opens the editor).
            enabled: repo && (repo.rebasePauseReason === "message"
                              || repo.rebaseConflictedCount === 0)
            onClicked:
            {
                if (repo.rebasePauseReason === "message")
                    root.requestMessageEdit()
                else
                    repo.continueRebase()
            }
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
