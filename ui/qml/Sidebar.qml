import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: sidebar
    objectName: "sidebar"
    color: theme.surfaceRaised

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.margins: 16
            spacing: 10
            Image {
                source: theme.iconSource
                sourceSize.width: 26
                sourceSize.height: 26
            }
            Label {
                text: "GitTide"
                color: theme.textPrimary
                font.pixelSize: 16
                font.weight: Font.Bold
            }
        }

        Item { Layout.fillHeight: true } // tree placeholder (Task 3)
    }
}
