import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Interactive-rebase todo editor (spec rebase-interactive.md §3.2).
// Rows are listed oldest at top (git order). Reorder via up/down buttons;
// per-row action dropdown. Start is disabled while the plan is invalid
// (first kept row squash/fixup, or all rows drop).
//
// API:
//   seed(base, entries)           — populate from [{oid, summary}] list
//   setActionForTest(i, action)   — test helper: set action for row i
//   property bool planValid       — false when the first kept (non-drop) row is
//                                   squash/fixup, or all rows are drop
//
// On Start → repoVm.startInteractiveRebase(base, actions[], oids[])
// Listens to repoVm.rebaseTodoReady(base, entries) and opens itself.
Dialog {
    id: root
    objectName: "rebaseTodoDialog"
    modal: true
    title: "Edit history"
    anchors.centerIn: parent
    width: 540
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: OverlayCard {}

    // ----- State -----

    property string rebaseBase: ""
    property ListModel todoModel: ListModel {}
    readonly property var actions: ["pick", "reword", "squash", "fixup", "drop"]

    // planValid mirrors the core guards:
    //  - empty → false
    //  - all rows drop → false
    //  - first KEPT (non-drop) row squash/fixup → false (leading drops included)
    //  - otherwise → true
    property bool planValid: {
        if (todoModel.count === 0)
            return false
        for (var i = 0; i < todoModel.count; ++i) {
            var action = todoModel.get(i).action
            if (action === "drop")
                continue
            // First non-drop row found → valid unless it is squash/fixup.
            return action !== "squash" && action !== "fixup"
        }
        return false // all rows drop
    }

    // ----- Public API -----

    function seed(b, entries) {
        rebaseBase = b
        todoModel.clear()
        for (var i = 0; i < entries.length; ++i)
            todoModel.append({ oid: entries[i].oid, summary: entries[i].summary,
                               // Honour a per-entry default action (squash seeding);
                               // plain "edit history" seeds leave it undefined → pick.
                               action: entries[i].action !== undefined ? entries[i].action : "pick" })
    }

    function setActionForTest(i, a) {
        todoModel.setProperty(i, "action", a)
    }

    function moveRow(from, to) {
        if (to < 0 || to >= todoModel.count)
            return
        todoModel.move(from, to, 1)
    }

    function collectActions() {
        var out = []
        for (var i = 0; i < todoModel.count; ++i)
            out.push(todoModel.get(i).action)
        return out
    }

    function collectOids() {
        var out = []
        for (var i = 0; i < todoModel.count; ++i)
            out.push(todoModel.get(i).oid)
        return out
    }

    // ----- Auto-open on signal -----

    Connections {
        target: repoVm
        function onRebaseTodoReady(b, entries) {
            root.seed(b, entries)
            root.open()
        }
    }

    // ----- Content -----

    contentItem: ColumnLayout {
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 8

            Label {
                text: "Edit history"
                color: theme.textPrimary
                font.pixelSize: 15
                font.bold: true
                Layout.fillWidth: true
            }
        }

        // Commit list
        ListView {
            id: todoList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(todoModel.count * 44, 330)
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            clip: true
            model: root.todoModel
            spacing: 2

            // Each row is a drag target (DropArea) wrapping a draggable content
            // strip. Dragging the grip reorders live via moveRow (the same verb the
            // ↑/↓ buttons use, so both paths stay keyboard-reachable and testable).
            delegate: Item {
                id: rowItem
                width: ListView.view.width
                height: 40
                property int visualIndex: index

                DropArea {
                    anchors.fill: parent
                    onEntered: function(drag) {
                        const from = drag.source.visualIndex
                        const to   = rowItem.visualIndex
                        if (from !== to)
                            root.moveRow(from, to)
                    }
                }

                Rectangle {
                    id: rowContent
                    width: rowItem.width
                    height: rowItem.height
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 6
                    color: dragHandler.active ? theme.surfaceOverlay : "transparent"

                    Drag.active: dragHandler.active
                    Drag.source: rowItem
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: height / 2

                    RowLayout {
                        anchors.fill: parent
                        spacing: 6

                        // Drag grip — an affordance (not a colour-only state, D19).
                        Label {
                            objectName: "dragHandle"
                            text: "⠿"
                            color: theme.textMuted
                            font.pixelSize: 16
                            horizontalAlignment: Text.AlignHCenter
                            Layout.preferredWidth: 22
                            Layout.alignment: Qt.AlignVCenter
                            ToolTip.text: "Drag to reorder"
                            ToolTip.visible: gripHover.hovered
                            HoverHandler { id: gripHover }
                            DragHandler {
                                id: dragHandler
                                target: rowContent
                                // Vertical-only drag from the grip; release snaps back.
                                xAxis.enabled: false
                                onActiveChanged: if (!active) rowContent.Drag.drop()
                            }
                        }

                        AppComboBox {
                            objectName: "actionCombo"
                            model: root.actions
                            currentIndex: root.actions.indexOf(action)
                            onActivated: root.todoModel.setProperty(index, "action", root.actions[currentIndex])
                            Layout.preferredWidth: 96
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            // Dropped rows: dimmed text + strikeout (D18/D19: not colour-only)
                            color: action === "drop" ? theme.textMuted : theme.textPrimary
                            font.strikeout: action === "drop"
                            font.pixelSize: 12
                            font.family: "monospace"
                            text: oid.substring(0, 7) + "  " + summary
                            Layout.alignment: Qt.AlignVCenter
                        }

                        AppButton {
                            variant: "secondary"
                            compact: true
                            text: "↑"
                            enabled: index > 0
                            onClicked: root.moveRow(index, index - 1)
                            Layout.preferredWidth: 28
                            Layout.alignment: Qt.AlignVCenter
                        }
                        AppButton {
                            variant: "secondary"
                            compact: true
                            text: "↓"
                            enabled: index < root.todoModel.count - 1
                            onClicked: root.moveRow(index, index + 1)
                            Layout.preferredWidth: 28
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    // While dragging, lift the strip out of the row so it follows
                    // the cursor; on release it returns to its (new) slot.
                    states: State {
                        when: dragHandler.active
                        ParentChange { target: rowContent; parent: todoList }
                        AnchorChanges {
                            target: rowContent
                            anchors.horizontalCenter: undefined
                            anchors.verticalCenter: undefined
                        }
                    }
                }
            }
        }

        // Validation hint — visible + text (not colour alone, D19)
        Label {
            visible: todoModel.count > 0 && !root.planValid
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 6
            wrapMode: Text.Wrap
            color: theme.stateConflict
            font.pixelSize: 11
            text: "The first kept commit can't be squash/fixup, and at least one commit must be kept."
        }

        Item { Layout.preferredHeight: 8 }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: root.close()
        }
        AppButton {
            objectName: "rebaseStartButton"
            variant: "primary"
            text: "Start rebase"
            enabled: root.planValid
            onClicked: {
                repoVm.startInteractiveRebase(root.rebaseBase, root.collectActions(), root.collectOids())
                root.close()
            }
        }
    }
}
