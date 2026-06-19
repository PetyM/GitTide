import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Brand-centered welcome card shown when no repositories are registered.
// Emits a request signal per CTA; the host wires each to the matching flow.
Item {
    id: emptyState
    objectName: "emptyState"

    signal addExistingRequested()
    signal cloneRequested()
    signal initRequested()
    signal newProjectRequested()

    // A ghost button used for every CTA.
    component Cta: Button {
        property string objName: ""
        property bool ghostEnabled: true
        objectName: objName
        Layout.fillWidth: true
        enabled: ghostEnabled
        contentItem: Label {
            text: parent.text
            color: parent.enabled ? theme.textPrimary : theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 13
        }
        background: Rectangle {
            radius: 10
            color: "transparent"
            border.color: parent.enabled ? theme.border : theme.surfaceOverlay
            border.width: 1
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        spacing: 12

        Image {
            source: theme.iconSource
            sourceSize.width: 48
            sourceSize.height: 48
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Welcome to GitTide"
            color: theme.textPrimary
            font.pixelSize: 22
            font.weight: Font.Bold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Add a repository to get started."
            color: theme.textMuted
            font.pixelSize: 13
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 8
        }

        Cta {
            objName: "addExistingCta"
            text: "Add existing repository"
            onClicked: emptyState.addExistingRequested()
        }
        Cta {
            objName: "cloneCta"
            text: "Clone repository"
            onClicked: emptyState.cloneRequested()
        }
        Cta {
            objName: "initRepoCta"
            text: "Initialize new repository"
            onClicked: emptyState.initRequested()
        }
        Cta {
            objName: "createProjectCta"
            text: "Create project"
            onClicked: emptyState.newProjectRequested()
        }
        Cta {
            objName: "manifestProjectCta"
            text: "Create project from manifest (coming soon)"
            ghostEnabled: false
        }
    }
}
