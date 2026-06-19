import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: branchBar
    objectName: "branchBar"
    property alias text: branchLabel.text

    implicitHeight: 56
    color: theme.surfaceRaised

    Rectangle { // bottom hairline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Rectangle { // accent-tinted current-branch chip
            Layout.preferredHeight: 36
            Layout.preferredWidth: branchCol.implicitWidth + 28
            radius: 6
            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.14)
            border.color: theme.accent
            border.width: 1

            ColumnLayout {
                id: branchCol
                anchors.centerIn: parent
                spacing: 0
                Label {
                    id: branchLabel
                    text: repoVm ? repoVm.currentBranch : ""
                    color: theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Label {
                    text: "Current branch"
                    color: theme.textMuted
                    font.pixelSize: 11
                }
            }
        }

        Item { Layout.fillWidth: true }
    }
}
