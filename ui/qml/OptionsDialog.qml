import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// App-level settings: theme mode and pull default. Changes apply instantly (no
// OK/Cancel). Receives appSettings from Main.qml — writes go to the shared
// Settings instance so they auto-persist and trigger Main.qml bindings.
AppDialog {
    id: dialog
    objectName: "optionsDialog"
    title: "Options"
    width: 360
    padding: 20

    required property var appSettings

    // Raised when the user opens identity management (Main.qml opens the dialog).
    signal identityRequested()

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

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.border
        }

        // Git identity section
        ColumnLayout {
            spacing: 8
            Layout.fillWidth: true

            Label {
                text: "Git identity"
                color: theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            AppButton {
                objectName: "manageIdentitiesButton"
                variant: "secondary"
                text: "Manage identities…"
                onClicked: dialog.identityRequested()
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            objectName: "optionsCloseButton"
            variant: "secondary"
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
