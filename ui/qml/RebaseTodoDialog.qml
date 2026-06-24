import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Interactive-rebase todo editor (spec rebase-interactive.md §3.2).
// Rows are listed oldest at top (git order). Reorder via up/down buttons;
// per-row action dropdown. Start is disabled while the plan is invalid
// (first row squash/fixup, or all rows drop).
//
// API:
//   seed(base, entries)           — populate from [{oid, summary}] list
//   setActionForTest(i, action)   — test helper: set action for row i
//   property bool planValid       — false when first row is squash/fixup
//                                   or all rows are drop
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
    //  - first row squash/fixup → false
    //  - all rows drop → false
    //  - otherwise → true
    property bool planValid: {
        if (todoModel.count === 0)
            return false
        var first = todoModel.get(0).action
        if (first === "squash" || first === "fixup")
            return false
        for (var i = 0; i < todoModel.count; ++i)
            if (todoModel.get(i).action !== "drop")
                return true
        return false
    }

    // ----- Public API -----

    function seed(b, entries) {
        rebaseBase = b
        todoModel.clear()
        for (var i = 0; i < entries.length; ++i)
            todoModel.append({ oid: entries[i].oid, summary: entries[i].summary, action: "pick" })
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

            delegate: RowLayout {
                width: ListView.view.width
                height: 40
                spacing: 6

                ComboBox {
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

                Button {
                    text: "↑"
                    enabled: index > 0
                    onClicked: root.moveRow(index, index - 1)
                    Layout.preferredWidth: 28
                    Layout.alignment: Qt.AlignVCenter
                }
                Button {
                    text: "↓"
                    enabled: index < root.todoModel.count - 1
                    onClicked: root.moveRow(index, index + 1)
                    Layout.preferredWidth: 28
                    Layout.alignment: Qt.AlignVCenter
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
            text: "First entry can't be squash/fixup, and at least one commit must be kept."
        }

        Item { Layout.preferredHeight: 8 }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        Button {
            text: "Cancel"
            onClicked: root.close()
        }
        Button {
            objectName: "rebaseStartButton"
            text: "Start rebase"
            enabled: root.planValid
            onClicked: {
                repoVm.startInteractiveRebase(root.rebaseBase, root.collectActions(), root.collectOids())
                root.close()
            }
        }
    }
}
