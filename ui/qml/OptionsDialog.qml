import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// App settings, tabbed. Appearance/Git write to appSettings (instant, auto-persist);
// Identity/Accounts manage credentials (added in Task 4). No OK/Cancel — Close only.
AppDialog {
    id: dialog
    objectName: "optionsDialog"
    title: "Options"
    width: 560
    padding: 20

    required property var appSettings

    contentItem: ColumnLayout {
        spacing: 16

        TabBar {
            id: tabBar
            objectName: "optionsTabBar"
            Layout.fillWidth: true
            background: null
            spacing: 0
            AppTabButton { text: "Appearance"; implicitWidth: 110 }
            AppTabButton { text: "Git"; implicitWidth: 110 }
            AppTabButton { text: "Identity"; implicitWidth: 110 }
            AppTabButton { text: "Accounts"; implicitWidth: 110 }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: tabBar.currentIndex

            OptionsAppearanceTab { appSettings: dialog.appSettings }
            OptionsGitTab { appSettings: dialog.appSettings }
            Flickable {
                implicitHeight: Math.min(identityTab.implicitHeight, 480)
                contentHeight: identityTab.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                OptionsIdentityTab { id: identityTab; width: parent.width }
            }
            Flickable {
                implicitHeight: Math.min(accountsTab.implicitHeight, 480)
                contentHeight: accountsTab.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                OptionsAccountsTab { id: accountsTab; width: parent.width }
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
