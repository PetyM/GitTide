import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Central credentials management: named git identities (+ global / per-project /
// per-repo assignment), forge host accounts (token validated against the API and
// stored in the keychain), and SSH keys. Backed by the `credentialManager` +
// `identityModel` / `hostModel` / `sshKeyModel` context properties.
Dialog {
    id: root
    objectName: "identityDialog"
    modal: true
    title: "Credentials"
    anchors.centerIn: parent
    width: 520
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: OverlayCard {}

    // Live assignment ids for the open repo / active project (Q_INVOKABLE results
    // are not auto-tracked, so refresh them on the relevant change signals).
    property string repoOverrideId: ""
    property string projectDefaultId: ""
    property string statusText: ""
    readonly property bool hasRepo: (typeof repoVm !== "undefined") && repoVm && repoVm.repoOpen
    readonly property string activeProjectId:
        (typeof projectController !== "undefined" && projectController) ? projectController.activeProjectId : ""

    function refreshAssignments() {
        root.repoOverrideId = (root.hasRepo && typeof credentialManager !== "undefined" && credentialManager)
            ? credentialManager.repoOverrideId(repoVm.repoPath) : ""
        root.projectDefaultId = (root.activeProjectId.length > 0 && typeof credentialManager !== "undefined" && credentialManager)
            ? credentialManager.projectDefaultId(root.activeProjectId) : ""
    }

    function openDialog() {
        nameField.text = ""
        emailField.text = ""
        root.statusText = ""
        refreshAssignments()
        open()
    }

    Connections {
        target: (typeof credentialManager !== "undefined") ? credentialManager : null
        function onChanged() { root.refreshAssignments() }
        function onHostValidated(ok, message) {
            root.statusText = message
            if (ok) { hostField.text = ""; hostTokenField.text = ""; hostApiBaseField.text = "" }
        }
    }
    Connections {
        target: (typeof repoVm !== "undefined") ? repoVm : null
        function onChanged() { root.refreshAssignments() }
    }

    contentItem: Flickable {
        implicitHeight: Math.min(bodyCol.implicitHeight, 560)
        contentHeight: bodyCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: bodyCol
            width: root.availableWidth
            spacing: 18

            // ============ Identities ============
            Label { text: "Identities"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

            ListView {
                objectName: "identityList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 160)
                clip: true
                interactive: false
                model: (typeof identityModel !== "undefined") ? identityModel : null
                spacing: 4
                delegate: Rectangle {
                    width: ListView.view.width
                    implicitHeight: idRow.implicitHeight + 12
                    radius: 6
                    color: (model.identityId === root.repoOverrideId || model.identityId === root.projectDefaultId)
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
                        Label { text: "Project"; visible: model.identityId === root.projectDefaultId && root.activeProjectId.length > 0; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                        Label { text: "Repo"; visible: model.identityId === root.repoOverrideId && root.hasRepo; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                        AppButton { variant: "secondary"; compact: true; text: "Global"; enabled: !model.isGlobal; onClicked: credentialManager.setGlobalIdentity(model.identityId) }
                        AppButton { variant: "secondary"; compact: true; text: "Project"; visible: root.activeProjectId.length > 0; enabled: model.identityId !== root.projectDefaultId; onClicked: credentialManager.setProjectDefault(root.activeProjectId, model.identityId) }
                        AppButton { variant: "secondary"; compact: true; text: "Repo"; visible: root.hasRepo; enabled: model.identityId !== root.repoOverrideId; onClicked: credentialManager.setRepoOverride(repoVm.repoPath, model.identityId) }
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

            Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

            // ============ Host accounts (forge tokens) ============
            Label { text: "Host accounts (HTTPS / forge tokens)"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

            ListView {
                objectName: "hostList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 120)
                clip: true
                interactive: false
                model: (typeof hostModel !== "undefined") ? hostModel : null
                spacing: 4
                delegate: RowLayout {
                    width: ListView.view.width; spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        Label { text: model.host + (model.username ? "  ·  " + model.username : ""); color: theme.textPrimary; font.pixelSize: 13 }
                        Label { text: model.kind; color: theme.textMuted; font.pixelSize: 11 }
                    }
                    AppButton { variant: "danger"; compact: true; text: "✕"; onClicked: credentialManager.removeHost(model.hostId) }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 6
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    AppComboBox {
                        id: hostKind; Layout.preferredWidth: 110
                        model: ["github", "gitlab", "generic"]
                    }
                    TextField {
                        id: hostField; objectName: "hostName"; Layout.fillWidth: true; placeholderText: "github.com"; color: theme.textPrimary
                        background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: hostField.activeFocus ? theme.accent : theme.border }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    TextField {
                        id: hostApiBaseField; Layout.fillWidth: true; placeholderText: "API base (optional)"; color: theme.textPrimary
                        background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: hostApiBaseField.activeFocus ? theme.accent : theme.border }
                    }
                    TextField {
                        id: hostTokenField; objectName: "hostToken"; Layout.fillWidth: true; placeholderText: "Personal access token"; echoMode: TextInput.Password; color: theme.textPrimary
                        background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: hostTokenField.activeFocus ? theme.accent : theme.border }
                    }
                    AppButton {
                        objectName: "hostAdd"; variant: "primary"; compact: true; text: "Validate & add"
                        enabled: hostField.text.length > 0 && hostTokenField.text.length > 0
                        onClicked: { root.statusText = "Validating…"; credentialManager.validateAndAddHost(hostField.text, hostKind.currentText, hostApiBaseField.text, hostTokenField.text) }
                    }
                }
                Label { visible: root.statusText.length > 0; text: root.statusText; color: theme.textMuted; font.pixelSize: 11 }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

            // ============ SSH keys ============
            Label { text: "SSH keys"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

            ListView {
                objectName: "sshKeyList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 120)
                clip: true
                interactive: false
                model: (typeof sshKeyModel !== "undefined") ? sshKeyModel : null
                spacing: 4
                delegate: RowLayout {
                    width: ListView.view.width; spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        Label { text: model.label + (model.hasPassphrase ? "  🔒" : ""); color: theme.textPrimary; font.pixelSize: 13 }
                        Label { text: model.privateKeyPath; color: theme.textMuted; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    }
                    AppButton { variant: "danger"; compact: true; text: "✕"; onClicked: credentialManager.removeSshKey(model.sshKeyId) }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 6
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    TextField {
                        id: sshLabel; Layout.preferredWidth: 120; placeholderText: "Label"; color: theme.textPrimary
                        background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshLabel.activeFocus ? theme.accent : theme.border }
                    }
                    TextField {
                        id: sshPriv; Layout.fillWidth: true; placeholderText: "Private key path"; color: theme.textPrimary
                        background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshPriv.activeFocus ? theme.accent : theme.border }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    TextField {
                        id: sshPub; Layout.fillWidth: true; placeholderText: "Public key path (optional)"; color: theme.textPrimary
                        background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshPub.activeFocus ? theme.accent : theme.border }
                    }
                    TextField {
                        id: sshPass; Layout.preferredWidth: 140; placeholderText: "Passphrase"; echoMode: TextInput.Password; color: theme.textPrimary
                        background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshPass.activeFocus ? theme.accent : theme.border }
                    }
                    AppButton {
                        objectName: "sshAdd"; variant: "primary"; compact: true; text: "Add"
                        enabled: sshLabel.text.length > 0 && sshPriv.text.length > 0
                        onClicked: { credentialManager.addSshKey(sshLabel.text, sshPub.text, sshPriv.text, sshPass.text); sshLabel.text = ""; sshPriv.text = ""; sshPub.text = ""; sshPass.text = "" }
                    }
                }
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            objectName: "identityCloseButton"
            variant: "secondary"
            text: "Close"
            onClicked: root.close()
        }
    }
}
