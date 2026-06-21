import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Branded welcome shown in the main area when no repository is open. A centered
// elevated card (design §Empty-state cards). When no project is active it offers
// only "Create project"; with an active project it offers the add-repo actions.
// Emits a request signal per CTA; the host wires each to the matching flow.
Item {
    id: emptyState
    objectName: "emptyState"

    // True when a project is active: gates add-repo actions vs. the create-project
    // call-to-action.
    property bool hasProject: false

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
        padding: 10
        contentItem: Label {
            text: parent.text
            color: parent.enabled ? theme.textPrimary : theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 13
        }
        background: Rectangle {
            radius: 6
            color: parent.enabled ? "transparent" : "transparent"
            border.color: parent.enabled ? theme.border : theme.surfaceOverlay
            border.width: 1
        }
    }

    // Centered elevated card.
    OverlayCard {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        height: cardCol.implicitHeight + 48

        ColumnLayout {
            id: cardCol
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 12

            Image {
                source: theme.iconSource
                sourceSize.width: 48
                sourceSize.height: 48
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: emptyState.hasProject ? "Add a repository" : "Welcome to GitTide"
                color: theme.textPrimary
                font.pixelSize: 22
                font.weight: Font.Bold
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: emptyState.hasProject
                      ? "Add a repository to this project to get started."
                      : "Create a project to get started."
                color: theme.textSecondary
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.bottomMargin: 8
            }

            // Add-repo actions — only meaningful once a project exists.
            Cta {
                objName: "addExistingCta"
                text: "Add existing repository"
                visible: emptyState.hasProject
                onClicked: emptyState.addExistingRequested()
            }
            Cta {
                objName: "cloneCta"
                text: "Clone repository"
                visible: emptyState.hasProject
                onClicked: emptyState.cloneRequested()
            }
            Cta {
                objName: "initRepoCta"
                text: "Initialize new repository"
                visible: emptyState.hasProject
                onClicked: emptyState.initRequested()
            }

            // Always available: create a (new) project.
            Cta {
                objName: "createProjectCta"
                text: emptyState.hasProject ? "Create another project" : "Create project"
                onClicked: emptyState.newProjectRequested()
            }
            Cta {
                objName: "manifestProjectCta"
                text: "Create project from manifest (coming soon)"
                visible: emptyState.hasProject
                ghostEnabled: false
            }
        }
    }
}
