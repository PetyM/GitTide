import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: workingPane
    objectName: "workingPane"
    spacing: 0

    BranchBar {
        Layout.fillWidth: true
    }

    TabBar {
        id: tabs
        objectName: "changesTabBar"
        Layout.fillWidth: true
        background: Rectangle { color: theme.surfaceRaised }
        TabButton {
            text: "Changes"
            contentItem: Label {
                text: parent.text
                color: parent.checked ? theme.textPrimary : theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: "transparent" }
        }
        TabButton {
            text: "History"
            contentItem: Label {
                text: parent.text
                color: parent.checked ? theme.textPrimary : theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: "transparent" }
        }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: tabs.currentIndex

        // Index 0: Changes — filled in by Task 5/6 (ChangesPane).
        Item { objectName: "changesTabBody" }

        // Index 1: History — placeholder (later plan).
        Item {
            objectName: "historyTabBody"
            Label {
                anchors.centerIn: parent
                text: "History — coming soon"
                color: theme.textMuted
                font.pixelSize: 13
            }
        }
    }
}
