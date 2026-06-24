import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Custom frameless title bar. Left side: macOS traffic lights (isMac) or app
// icon button (Win/Linux). Centre: drag area (startSystemMove). Right side:
// Win/Linux window controls (minimise/maximise/close). Platform flag: isMac.
Rectangle {
    id: titleBar
    objectName: "titleBar"
    height: 40
    color: theme.surfaceRaised

    signal optionsRequested()
    signal aboutRequested()
    signal rebaseRequested()

    readonly property bool isMac: Qt.platform.os === "osx"

    // Bottom border
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.border
    }

    // Native drag — gives Windows Snap and correct macOS move behaviour
    DragHandler {
        target: null
        onActiveChanged: if (active) window.startSystemMove()
    }

    // Double-click: toggle maximise / restore
    TapHandler {
        onTapped: {
            if (tapCount === 2) {
                if (window.visibility === Window.Maximized)
                    window.showNormal()
                else
                    window.showMaximized()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // macOS: traffic lights on the left
        RowLayout {
            visible: titleBar.isMac
            spacing: 0
            Layout.leftMargin: 8

            WindowButton {
                macOs: true
                circleColor: "#FF5F56"
                glyph: "✕"
                onClicked: window.close()
            }
            WindowButton {
                macOs: true
                circleColor: "#FFBD2E"
                glyph: "─"
                onClicked: window.showMinimized()
            }
            WindowButton {
                macOs: true
                circleColor: "#27C93F"
                glyph: window.visibility === Window.Maximized ? "❐" : "+"
                onClicked: window.visibility === Window.Maximized
                           ? window.showNormal() : window.showMaximized()
            }
        }

        // App icon button — opens the app menu
        Button {
            id: iconBtn
            objectName: "appIconButton"
            flat: true
            implicitWidth: 40
            implicitHeight: 40
            Layout.leftMargin: 4

            contentItem: Image {
                source: theme.iconSource
                sourceSize.width: 22
                sourceSize.height: 22
                anchors.centerIn: parent
            }
            background: Rectangle {
                color: iconBtn.hovered ? theme.surfaceOverlay : "transparent"
                radius: 4
            }
            onClicked: appMenuPopup.popup()

            AppMenu {
                id: appMenuPopup
                objectName: "appMenuPopup"

                AppMenuItem {
                    objectName: "optionsMenuItem"
                    text: "Options…"
                    onTriggered: titleBar.optionsRequested()
                }
                AppMenuItem {
                    objectName: "aboutMenuItem"
                    text: "About GitTide"
                    onTriggered: titleBar.aboutRequested()
                }
                AppMenuItem {
                    objectName: "rebaseMenuItem"
                    text: "Rebase current branch…"
                    onTriggered: titleBar.rebaseRequested()
                }
                MenuSeparator {
                    padding: 6
                    contentItem: Rectangle { implicitHeight: 1; color: theme.border }
                }
                AppMenuItem {
                    objectName: "quitMenuItem"
                    text: "Quit"
                    onTriggered: Qt.quit()
                }
            }
        }

        // Drag spacer fills the centre
        Item { Layout.fillWidth: true }

        // Win/Linux: window controls on the right
        RowLayout {
            visible: !titleBar.isMac
            spacing: 0

            WindowButton {
                glyph: "─"
                ToolTip.visible: hovered
                ToolTip.text: "Minimise"
                onClicked: window.showMinimized()
            }
            WindowButton {
                glyph: window.visibility === Window.Maximized ? "❐" : "⬜"
                ToolTip.visible: hovered
                ToolTip.text: window.visibility === Window.Maximized ? "Restore" : "Maximise"
                onClicked: window.visibility === Window.Maximized
                           ? window.showNormal() : window.showMaximized()
            }
            WindowButton {
                glyph: "✕"
                hoverColor: "#C42B1C"
                glyphColor: hovered ? "white" : theme.textSecondary
                ToolTip.visible: hovered
                ToolTip.text: "Close"
                onClicked: window.close()
            }
        }
    }
}
