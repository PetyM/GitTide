import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Keyboard shortcuts reference overlay. Opened by the ? shortcut in WorkingPane.
Popup {
    id: root
    objectName: "shortcutsPopup"

    width: 380
    height: 292
    anchors.centerIn: parent

    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0

    background: OverlayCard {}

    contentItem: ColumnLayout {
        id: content
        spacing: 0

        readonly property bool isMac: Qt.platform.os === "osx"
        readonly property string tabKeys: isMac ? "⌘1 / ⌘2 / ⌘3" : "Alt+1 / +2 / +3"

        // Title row
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.bottomMargin: 12

            Label {
                text: "Keyboard shortcuts"
                color: theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: "Esc"
                color: theme.textMuted
                font.pixelSize: 11
                font.family: "monospace"
            }
        }

        // Hairline under title
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.border
        }

        // Shortcut rows
        component Row: RowLayout {
            property string keys: ""
            property string action: ""

            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 6
            Layout.bottomMargin: 6
            spacing: 12

            Rectangle {
                implicitWidth: keyLabel.implicitWidth + 10
                implicitHeight: 20
                radius: 3
                color: "transparent"
                border.color: theme.border
                border.width: 1
                Label {
                    id: keyLabel
                    anchors.centerIn: parent
                    text: parent.parent.keys
                    color: theme.textSecondary
                    font.family: "monospace"
                    font.pixelSize: 11
                }
            }
            Label {
                text: parent.action
                color: theme.textSecondary
                font.pixelSize: 12
                Layout.fillWidth: true
            }
        }

        Row { keys: "↑ / ↓";        action: "Navigate files or commits" }
        Row { keys: "Space";         action: "Stage / unstage file" }
        Row { keys: "Tab";           action: "Next pane" }
        Row { keys: "Ctrl+Enter";    action: "Commit" }
        Row { keys: content.tabKeys; action: "Changes / History / Graph tab" }
        Row { keys: "Ctrl+R";        action: "Refresh" }
        Row { keys: "?";             action: "Show / hide this panel" }

        Item { Layout.fillHeight: true }
    }
}
