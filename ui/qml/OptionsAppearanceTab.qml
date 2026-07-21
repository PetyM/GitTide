import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Appearance: theme mode (applies instantly, auto-persists via appSettings).
ColumnLayout {
    id: tab
    required property var appSettings
    spacing: 8

    Label {
        text: "Theme"
        color: theme.textMuted
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }

    ButtonGroup { id: themeGroup }

    RowLayout {
        spacing: 16
        AppRadioButton {
            objectName: "themeSystemRadio"; text: "System"
            ButtonGroup.group: themeGroup
            checked: tab.appSettings.themeMode === 0
            onClicked: { tab.appSettings.themeMode = 0; theme.setMode(0) }
        }
        AppRadioButton {
            objectName: "themeDarkRadio"; text: "Dark"
            ButtonGroup.group: themeGroup
            checked: tab.appSettings.themeMode === 1
            onClicked: { tab.appSettings.themeMode = 1; theme.setMode(1) }
        }
        AppRadioButton {
            objectName: "themeLightRadio"; text: "Light"
            ButtonGroup.group: themeGroup
            checked: tab.appSettings.themeMode === 2
            onClicked: { tab.appSettings.themeMode = 2; theme.setMode(2) }
        }
    }
}
