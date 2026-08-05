import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Scan a folder for git repositories and add the chosen ones to the active
// project in one action; optionally keep the folder registered as a source so
// repositories appearing there later are added automatically (design §9).
AppDialog {
    id: dialog
    objectName: "addFromFolderDialog"
    title: "Add repositories from folder"
    width: 520
    padding: 20

    property string folder: ""
    property bool scanning: false
    // [{ path, name, alreadyAdded, checked }] — the scan result plus tick state.
    property var candidates: []
    property string errorText: ""

    // Monotonic guard against stale scanFinished/scanFailed responses.
    // ProjectController::scanFolder carries no request id — every call
    // independently emits on the same shared signal — so a response for an
    // abandoned request can otherwise land after a newer one has already
    // updated the checklist, or after the dialog was closed and reopened.
    // scanToken is bumped on every fresh request AND on every openDialog();
    // pendingScanToken captures which request is the one currently awaited.
    // Because startScan() is single-flight (see below) there is at most one
    // outstanding request per open session, so a response is current iff its
    // captured token still matches scanToken — anything else means either a
    // later request superseded it or the dialog moved on to a new session.
    property int scanToken: 0
    property int pendingScanToken: -1

    readonly property int checkedCount: {
        var n = 0
        for (var i = 0; i < candidates.length; ++i)
            if (candidates[i].checked && !candidates[i].alreadyAdded)
                ++n
        return n
    }

    function openDialog() {
        folder = ""
        candidates = []
        errorText = ""
        scanning = false
        // Invalidate any request still in flight from a previous session —
        // its eventual response must not populate this fresh checklist.
        scanToken++
        keepSource.checked = false
        open()
    }

    // Re-assign the whole array: QML does not track in-place element mutation.
    function setChecked(index, value) {
        var next = candidates.slice()
        next[index].checked = value
        candidates = next
    }

    function setAllChecked(value) {
        var next = candidates.slice()
        for (var i = 0; i < next.length; ++i)
            if (!next[i].alreadyAdded)
                next[i].checked = value
        candidates = next
    }

    function startScan() {
        // Single-flight: never issue a second request while one is still
        // outstanding. The depth SpinBox and "Choose…" are also disabled
        // while scanning so this should never trigger through the UI; it is
        // the code-level backstop the token check above relies on — without
        // it, two live requests could each satisfy the token compare and the
        // last response to arrive would win regardless of which is current.
        if (folder.length === 0 || !projectController || scanning)
            return
        errorText = ""
        candidates = []
        scanning = true
        pendingScanToken = ++scanToken
        projectController.scanFolder(folder, depthBox.value)
    }

    Connections {
        target: projectController
        function onScanFinished(found) {
            if (!dialog.visible || dialog.pendingScanToken !== dialog.scanToken)
                return
            var rows = []
            for (var i = 0; i < found.length; ++i)
                rows.push({ path: found[i].path, name: found[i].name,
                            alreadyAdded: found[i].alreadyAdded,
                            checked: !found[i].alreadyAdded })
            dialog.candidates = rows
            dialog.scanning = false
        }
        function onScanFailed(message) {
            if (!dialog.visible || dialog.pendingScanToken !== dialog.scanToken)
                return
            dialog.candidates = []
            dialog.scanning = false
            dialog.errorText = message
        }
    }

    FolderDialog {
        id: folderPicker
        title: "Choose a folder of repositories"
        onAccepted: {
            // toString() is percent-encoded and (on Windows) leaves a stray
            // leading slash; toLocalFile() semantics via the controller avoid
            // both, matching the plain paths stored/compared elsewhere.
            dialog.folder = projectController ? projectController.localPathFromUrl(selectedFolder) : ""
            dialog.startScan()
        }
    }

    contentItem: DialogColumn {
        spacing: 12

        Label {
            text: "Folder"
            color: theme.textMuted
            font.pixelSize: 11
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: dialog.folder.length > 0 ? dialog.folder : "No folder chosen"
                color: dialog.folder.length > 0 ? theme.textPrimary : theme.textMuted
                elide: Text.ElideMiddle
                font.pixelSize: 12
            }
            AppButton {
                objectName: "addFromFolderChoose"
                variant: "secondary"
                text: "Choose…"
                enabled: !dialog.scanning
                onClicked: folderPicker.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "Scan depth"
                color: theme.textMuted
                font.pixelSize: 11
            }
            SpinBox {
                id: depthBox
                objectName: "addFromFolderDepth"
                from: 1
                to: 5
                value: 2
                editable: false
                enabled: !dialog.scanning
                onValueChanged: if (dialog.folder.length > 0) dialog.startScan()
                contentItem: Label {
                    text: depthBox.value
                    color: theme.textPrimary
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 96
                    radius: 6
                    color: theme.surfaceBase
                    border.color: depthBox.activeFocus ? theme.accent : theme.border
                    border.width: 1
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: dialog.candidates.length === 1 ? "1 repository found"
                                                     : dialog.candidates.length + " repositories found"
                visible: !dialog.scanning && dialog.candidates.length > 0
                color: theme.textMuted
                font.pixelSize: 11
            }
            AppButton {
                variant: "secondary"
                text: "Select all"
                visible: dialog.candidates.length > 0
                onClicked: dialog.setAllChecked(true)
            }
            AppButton {
                variant: "secondary"
                text: "Select none"
                visible: dialog.candidates.length > 0
                onClicked: dialog.setAllChecked(false)
            }
        }

        // ---- Result area: busy / message / list ----
        Label {
            Layout.fillWidth: true
            visible: dialog.scanning
            text: "Scanning…"
            color: theme.textMuted
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }
        Label {
            Layout.fillWidth: true
            visible: !dialog.scanning && dialog.errorText.length > 0
            text: dialog.errorText
            color: theme.stateDeleted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            visible: !dialog.scanning && dialog.errorText.length === 0
                     && dialog.folder.length > 0 && dialog.candidates.length === 0
            text: "No git repositories found in " + dialog.folder
            color: theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        ListView {
            id: repoList
            objectName: "addFromFolderList"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            visible: !dialog.scanning && dialog.candidates.length > 0
            clip: true
            model: dialog.candidates
            ScrollBar.vertical: AppScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property int index
                required property var modelData
                width: repoList.width
                height: 46
                enabled: !row.modelData.alreadyAdded
                onClicked: dialog.setChecked(row.index, !row.modelData.checked)

                background: Rectangle {
                    radius: 4
                    color: row.hovered && row.enabled ? theme.surfaceRaised : "transparent"
                }

                contentItem: RowLayout {
                    spacing: 8
                    AppCheckBox {
                        checked: row.modelData.checked
                        enabled: row.enabled
                        onClicked: dialog.setChecked(row.index, checked)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: row.modelData.name
                            color: row.enabled ? theme.textPrimary : theme.textMuted
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: row.modelData.path
                            color: theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        visible: row.modelData.alreadyAdded
                        text: "already added"
                        color: theme.textMuted
                        font.pixelSize: 11
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppCheckBox {
                id: keepSource
                objectName: "addFromFolderKeepSource"
            }
            Label {
                Layout.fillWidth: true
                text: "Keep this folder as a source — add new repositories automatically"
                color: theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    footer: DialogButtons {
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "addFromFolderConfirm"
            variant: "primary"
            text: "Add"
            enabled: dialog.checkedCount > 0 && !dialog.scanning
            onClicked: {
                var chosen = []
                var skipped = []
                for (var i = 0; i < dialog.candidates.length; ++i) {
                    var c = dialog.candidates[i]
                    if (c.alreadyAdded)
                        continue
                    if (c.checked)
                        chosen.push(c.path)
                    else
                        skipped.push(c.path)
                }
                if (projectController)
                    projectController.addRepos(chosen, skipped,
                                               keepSource.checked ? dialog.folder : "",
                                               depthBox.value)
                dialog.close()
            }
        }
    }
}
