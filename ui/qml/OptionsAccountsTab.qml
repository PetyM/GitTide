import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Accounts: forge host tokens (validated + keychain-stored) and SSH keys.
ColumnLayout {
    id: tab
    spacing: 18
    property string statusText: ""

    Connections {
        target: (typeof credentialManager !== "undefined") ? credentialManager : null
        function onHostValidated(ok, message) {
            tab.statusText = message
            if (ok) { hostField.text = ""; hostTokenField.text = ""; hostApiBaseField.text = "" }
        }
    }

    // ---- Host accounts ----
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
            AppComboBox { id: hostKind; Layout.preferredWidth: 110; model: ["github", "gitlab", "generic"] }
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
                onClicked: { tab.statusText = "Validating…"; credentialManager.validateAndAddHost(hostField.text, hostKind.currentText, hostApiBaseField.text, hostTokenField.text) }
            }
        }
        Label { visible: tab.statusText.length > 0; text: tab.statusText; color: theme.textMuted; font.pixelSize: 11 }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

    // ---- SSH keys ----
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
