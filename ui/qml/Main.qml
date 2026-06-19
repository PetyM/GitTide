import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "appWindow"
    visible: true
    width: 1100
    height: 720
    title: "GitTide"
    color: theme.surfaceBase

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 272
        }

        // Placeholder main pane — branch bar / tabs / diff arrive in later plans.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.surfaceBase
            Label {
                anchors.centerIn: parent
                text: "Select a repository"
                color: theme.textMuted
                font.pixelSize: 13
            }
        }
    }
}
