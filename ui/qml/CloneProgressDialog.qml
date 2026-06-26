import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Modal clone progress. Bound to ProjectController.cloneProgress; closes on
// repoAdded / repoAddFailed. Cancel aborts the in-flight clone.
Dialog {
    id: dialog
    objectName: "cloneProgressDialog"
    modal: true
    title: "Cloning…"
    anchors.centerIn: parent
    width: 380
    padding: 20
    closePolicy: Popup.NoAutoClose

    property int received: 0
    property int total: 0
    property string errorText: ""

    background: OverlayCard {}

    function openDialog() {
        received = 0
        total = 0
        errorText = ""
        open()
    }

    contentItem: ColumnLayout {
        spacing: 12

        ProgressBar {
            id: bar
            Layout.fillWidth: true
            indeterminate: dialog.total <= 0
            from: 0
            to: Math.max(1, dialog.total)
            value: dialog.received

            // Themed track + accent fill (the Basic style ships unstyled grey).
            background: Rectangle {
                implicitHeight: 6
                radius: 3
                color: theme.surfaceBase
                border.color: theme.border
                border.width: 1
            }
            contentItem: Item {
                Rectangle {
                    visible: !bar.indeterminate
                    width: bar.visualPosition * parent.width
                    height: parent.height
                    radius: 3
                    color: theme.accent
                }
            }
        }
        Label {
            text: dialog.errorText.length > 0
                  ? dialog.errorText
                  : (dialog.total > 0
                     ? (dialog.received + " / " + dialog.total + " objects ("
                        + Math.round(100 * dialog.received / dialog.total) + "%)")
                     : "Connecting…")
            color: dialog.errorText.length > 0 ? theme.stateDeleted : theme.textMuted
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            objectName: "cloneCancel"
            variant: "secondary"
            text: "Cancel"
            onClicked: {
                if (projectController)
                    projectController.cancelClone()
                dialog.close()
            }
        }
    }

    Connections {
        target: projectController
        function onCloneProgress(received, total) {
            dialog.received = received
            dialog.total = total
        }
        function onRepoAdded(path) {
            dialog.close()
        }
        function onRepoAddFailed(message) {
            dialog.errorText = message
        }
    }
}
