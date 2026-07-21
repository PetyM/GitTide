import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Git: pull reconciliation default (merge vs rebase).
ColumnLayout {
    id: tab
    required property var appSettings
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
            objectName: "pullMergeRadio"; text: "Merge"
            ButtonGroup.group: pullGroup
            checked: !tab.appSettings.pullRebase
            onClicked: tab.appSettings.pullRebase = false
        }
        AppRadioButton {
            objectName: "pullRebaseRadio"; text: "Rebase"
            ButtonGroup.group: pullGroup
            checked: tab.appSettings.pullRebase
            onClicked: tab.appSettings.pullRebase = true
        }
    }
}
