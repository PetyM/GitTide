import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    title: "Authentication required"
    standardButtons: Dialog.Ok | Dialog.Cancel

    property alias username: userField.text
    property alias token: tokenField.text

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Label {
            text: "HTTPS username"
            color: theme.textPrimary
        }

        TextField {
            id: userField
            Layout.fillWidth: true
        }

        Label {
            text: "Personal access token"
            color: theme.textPrimary
        }

        TextField {
            id: tokenField
            Layout.fillWidth: true
            echoMode: TextInput.Password
        }

        Label {
            text: "Stored for this session only."
            color: theme.textMuted
            font.pixelSize: 11
        }
    }

    onAccepted: if (repoVm) repoVm.submitCredentials(userField.text, tokenField.text)
}
