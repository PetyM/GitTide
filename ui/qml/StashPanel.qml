import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Collapsible "Stashes (n)" section for the Changes tab's left column. Lists the
// stash stack; selecting a row previews its diff in the right pane; per-row Apply/
// Pop/Drop and a header Clear button. Hidden entirely when the stack is empty.
//
// Implementation uses a Repeater (not ListView) so all delegate items are always
// in the QObject hierarchy — important for test findChild calls and also correct
// for the small stash lists expected in practice (rarely more than ~8 entries).
//
// Stale-preview mitigation: before any stack mutation (Apply/Pop/Drop/Clear), call
// exitStashPreview() first so the right pane does not display a diff for an entry
// whose index has shifted or that no longer exists.
ColumnLayout {
    id: stashPanel
    objectName: "stashPanel"
    spacing: 0

    // Hidden when the stack is empty (stashAvailable is the reactive Q_PROPERTY).
    visible: repoVm && repoVm.stashAvailable

    property bool expanded: true

    // ---- Header row: disclosure toggle + title + Clear button ----
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        Layout.topMargin: 6
        Layout.bottomMargin: 6
        spacing: 6

        Label {
            text: stashPanel.expanded ? "▾" : "▸"
            color: theme.textSecondary
            font.pixelSize: 12
            MouseArea {
                anchors.fill: parent
                onClicked: stashPanel.expanded = !stashPanel.expanded
            }
        }

        Label {
            Layout.fillWidth: true
            // stashList (Repeater) count is always up to date with the model.
            text: "Stashes (" + stashList.count + ")"
            color: theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.DemiBold
            MouseArea {
                anchors.fill: parent
                onClicked: stashPanel.expanded = !stashPanel.expanded
            }
        }

        AppButton {
            objectName: "stashClearButton"
            variant: "secondary"
            compact: true
            text: "Clear"
            onClicked: {
                if (repoVm) repoVm.exitStashPreview()
                if (repoVm) repoVm.clearStashes()
            }
        }
    }

    // ---- Entry list (clipped, max 180 px) ----
    // A Repeater eagerly creates all delegates so findChild always works and the
    // layout does not depend on a running rendering loop. An outer Item clips
    // entries that exceed the height cap; an inner Column stacks them.
    Item {
        id: stashEntriesContainer
        Layout.fillWidth: true
        // Clamp height to 180 px; Column grows freely so overflow is clipped.
        Layout.preferredHeight: Math.min(stashEntriesColumn.height, 180)
        visible: stashPanel.expanded
        clip: true

        Column {
            id: stashEntriesColumn
            width: parent.width
            spacing: 0

            // The Repeater creates one delegate per stash entry. Its count
            // property (Q_PROPERTY with NOTIFY) tracks the model row count.
            Repeater {
                id: stashList
                objectName: "stashList"
                model: repoVm ? repoVm.stashes : null

                delegate: Rectangle {
                    // `index` and model role names (label, message, oid) are
                    // available implicitly in the delegate context from the
                    // QAbstractListModel without needing `required property`.
                    width: stashEntriesColumn.width
                    height: 46
                    color: "transparent"

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (repoVm) repoVm.previewStash(index)
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 6

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: model.label
                                color: theme.textSecondary
                                font.pixelSize: 11
                            }
                            Label {
                                Layout.fillWidth: true
                                text: model.message
                                color: theme.textPrimary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }

                        AppButton {
                            objectName: "stashApplyButton"
                            variant: "secondary"
                            compact: true
                            text: "Apply"
                            onClicked: {
                                if (repoVm) repoVm.exitStashPreview()
                                if (repoVm) repoVm.applyStash(index)
                            }
                        }

                        AppButton {
                            objectName: "stashPopButton"
                            variant: "secondary"
                            compact: true
                            text: "Pop"
                            onClicked: {
                                if (repoVm) repoVm.exitStashPreview()
                                if (repoVm) repoVm.popStashAt(index)
                            }
                        }

                        AppButton {
                            objectName: "stashDropButton"
                            variant: "danger"
                            compact: true
                            text: "Drop"
                            onClicked: {
                                if (repoVm) repoVm.exitStashPreview()
                                if (repoVm) repoVm.dropStash(index)
                            }
                        }
                    }
                }
            }
        }
    }

    // Hairline separator below the panel body.
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: theme.border
        visible: stashPanel.expanded
    }
}
