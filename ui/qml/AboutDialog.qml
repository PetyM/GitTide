import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// About dialog: app icon, name, version (from appVersion context property), tagline.
AppDialog {
    id: dialog
    objectName: "aboutDialog"
    width: 320
    padding: 24

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
        AppButton {
            variant: "secondary"
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
