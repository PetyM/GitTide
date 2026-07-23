import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// HTTPS credential prompt, raised when a network op reports authRequired. Styled
// as a raised card (design §9) to match the other dialogs; tokens are kept for
// the session only.
AppDialog {
    id: root
    objectName: "credentialDialog"
    title: "Authentication required"
    width: 380
    padding: 20

    property alias username: userField.text
    property alias token: tokenField.text

    function openDialog() {
        userField.text = ""
        tokenField.text = ""
        open()
        userField.forceActiveFocus()
    }

    contentItem: DialogColumn {
        spacing: 12

        Label {
            text: "HTTPS username"
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: userField
            objectName: "credentialUsername"
            Layout.fillWidth: true
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: userField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
            Keys.onReturnPressed: tokenField.forceActiveFocus()
        }

        Label {
            text: "Personal access token"
            color: theme.textMuted
            font.pixelSize: 11
        }
        TextField {
            id: tokenField
            objectName: "credentialToken"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            color: theme.textPrimary
            background: Rectangle {
                radius: 6
                color: theme.surfaceBase
                border.color: tokenField.activeFocus ? theme.accent : theme.border
                border.width: 1
            }
            Keys.onReturnPressed: if (okButton.enabled) root.accept()
        }

        Label {
            text: "Saved securely to your OS keychain for this host."
            color: theme.textMuted
            font.pixelSize: 11
        }
    }

    footer: DialogButtons {
        AppButton {
            objectName: "credentialCancel"
            variant: "secondary"
            text: "Cancel"
            onClicked: root.reject()
        }
        AppButton {
            id: okButton
            objectName: "credentialOk"
            variant: "primary"
            text: "Sign in"
            enabled: userField.text.length > 0 && tokenField.text.length > 0
            onClicked: root.accept()
        }
    }

}
