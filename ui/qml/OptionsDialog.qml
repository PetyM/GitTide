import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// App-level settings: theme mode and pull default. Changes apply instantly (no
// OK/Cancel). Receives appSettings from Main.qml — writes go to the shared
// Settings instance so they auto-persist and trigger Main.qml bindings.
Dialog {
    id: dialog
    objectName: "optionsDialog"
    modal: true
    title: "Options"
    anchors.centerIn: parent
    width: 360
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    required property var appSettings

    background: OverlayCard {}

    contentItem: ColumnLayout {
        spacing: 20

        // Theme section
        ColumnLayout {
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
                    objectName: "themeSystemRadio"
                    text: "System"
                    ButtonGroup.group: themeGroup
                    checked: dialog.appSettings.themeMode === 0
                    onClicked: {
                        dialog.appSettings.themeMode = 0
                        theme.setMode(0)
                    }
                }
                AppRadioButton {
                    objectName: "themeDarkRadio"
                    text: "Dark"
                    ButtonGroup.group: themeGroup
                    checked: dialog.appSettings.themeMode === 1
                    onClicked: {
                        dialog.appSettings.themeMode = 1
                        theme.setMode(1)
                    }
                }
                AppRadioButton {
                    objectName: "themeLightRadio"
                    text: "Light"
                    ButtonGroup.group: themeGroup
                    checked: dialog.appSettings.themeMode === 2
                    onClicked: {
                        dialog.appSettings.themeMode = 2
                        theme.setMode(2)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.border
        }

        // Pull default section
        ColumnLayout {
            spacing: 8

            Label {
                text: "Pull default"
                color: theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            ButtonGroup { id: pullGroup }

            RowLayout {
                spacing: 16

                AppRadioButton {
                    objectName: "pullMergeRadio"
                    text: "Merge"
                    ButtonGroup.group: pullGroup
                    checked: !dialog.appSettings.pullRebase
                    onClicked: dialog.appSettings.pullRebase = false
                }
                AppRadioButton {
                    objectName: "pullRebaseRadio"
                    text: "Rebase"
                    ButtonGroup.group: pullGroup
                    checked: dialog.appSettings.pullRebase
                    onClicked: dialog.appSettings.pullRebase = true
                }
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        Button {
            objectName: "optionsCloseButton"
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
