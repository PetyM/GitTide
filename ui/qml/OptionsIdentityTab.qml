import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Identity: named git identities (+ global / project / repo assignment).
// Backed by credentialManager + identityModel context properties.
ColumnLayout {
    id: tab
    spacing: 18

    property string repoOverrideId: ""
    property string projectDefaultId: ""
    readonly property bool hasRepo: (typeof repoVm !== "undefined") && repoVm && repoVm.repoOpen
    readonly property string activeProjectId:
        (typeof projectController !== "undefined" && projectController) ? projectController.activeProjectId : ""

    function refreshAssignments() {
        tab.repoOverrideId = (tab.hasRepo && typeof credentialManager !== "undefined" && credentialManager)
            ? credentialManager.repoOverrideId(repoVm.repoPath) : ""
        tab.projectDefaultId = (tab.activeProjectId.length > 0 && typeof credentialManager !== "undefined" && credentialManager)
            ? credentialManager.projectDefaultId(tab.activeProjectId) : ""
    }

    Component.onCompleted: refreshAssignments()

    Connections {
        target: (typeof credentialManager !== "undefined") ? credentialManager : null
        function onChanged() { tab.refreshAssignments() }
    }
    Connections {
        target: (typeof repoVm !== "undefined") ? repoVm : null
        function onChanged() { tab.refreshAssignments() }
    }

    Label { text: "Identities"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

    ListView {
        objectName: "identityList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 200)
        clip: true
        interactive: false
        model: (typeof identityModel !== "undefined") ? identityModel : null
        spacing: 4
        delegate: Rectangle {
            width: ListView.view.width
            implicitHeight: idRow.implicitHeight + 12
            radius: 6
            color: (model.identityId === tab.repoOverrideId || model.identityId === tab.projectDefaultId)
                   ? theme.surfaceOverlay : "transparent"
            RowLayout {
                id: idRow
                anchors.fill: parent; anchors.margins: 6; spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Label { text: model.name; color: theme.textPrimary; font.pixelSize: 13 }
                    Label { text: model.email; color: theme.textMuted; font.pixelSize: 11 }
                }
                Label { text: "Global"; visible: model.isGlobal; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { text: "Project"; visible: model.identityId === tab.projectDefaultId && tab.activeProjectId.length > 0; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { text: "Repo"; visible: model.identityId === tab.repoOverrideId && tab.hasRepo; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                AppButton { variant: "secondary"; compact: true; text: "Global"; enabled: !model.isGlobal; onClicked: credentialManager.setGlobalIdentity(model.identityId) }
                AppButton { variant: "secondary"; compact: true; text: "Project"; visible: tab.activeProjectId.length > 0; enabled: model.identityId !== tab.projectDefaultId; onClicked: credentialManager.setProjectDefault(tab.activeProjectId, model.identityId) }
                AppButton { variant: "secondary"; compact: true; text: "Repo"; visible: tab.hasRepo; enabled: model.identityId !== tab.repoOverrideId; onClicked: credentialManager.setRepoOverride(repoVm.repoPath, model.identityId) }
                AppButton { variant: "danger"; compact: true; text: "✕"; onClicked: credentialManager.removeIdentity(model.identityId) }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 8
        TextField {
            id: nameField; objectName: "identityName"; Layout.fillWidth: true; placeholderText: "Name"; color: theme.textPrimary
            background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: nameField.activeFocus ? theme.accent : theme.border }
        }
        TextField {
            id: emailField; objectName: "identityEmail"; Layout.fillWidth: true; placeholderText: "email@example.com"; color: theme.textPrimary
            background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: emailField.activeFocus ? theme.accent : theme.border }
        }
        AppButton {
            objectName: "identityAdd"; variant: "primary"; compact: true; text: "Add"
            enabled: nameField.text.length > 0 && emailField.text.length > 0
            onClicked: { credentialManager.addIdentity(nameField.text, emailField.text); nameField.text = ""; emailField.text = "" }
        }
    }
}
