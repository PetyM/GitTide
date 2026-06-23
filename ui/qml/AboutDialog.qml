import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// About dialog: app icon, name, version (from appVersion context property), tagline.
Dialog {
    id: dialog
    objectName: "aboutDialog"
    modal: true
    anchors.centerIn: parent
    width: 320
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: OverlayCard {}

    contentItem: ColumnLayout {
        spacing: 12

        Image {
            source: theme.iconSource
            sourceSize.width: 48
            sourceSize.height: 48
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "GitTide"
            color: theme.textPrimary
            font.pixelSize: 20
            font.weight: Font.Bold
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Version " + appVersion
            color: theme.textMuted
            font.pixelSize: 13
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "A multi-repo git client."
            color: theme.textSecondary
            font.pixelSize: 13
            Layout.alignment: Qt.AlignHCenter
        }
    }

    footer: RowLayout {
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        Button {
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
