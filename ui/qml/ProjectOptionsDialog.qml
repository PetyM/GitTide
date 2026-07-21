import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Project Options — per-project + per-repo identity for the ACTIVE project.
// Backed by credentialManager + projectController context properties. Opened
// from the Sidebar project switcher (Main.qml wires openDialog()).
AppDialog {
    id: dialog
    objectName: "projectOptionsDialog"
    title: (typeof projectController !== "undefined" && projectController
            && projectController.activeProjectName.length > 0)
           ? ("Project options — " + projectController.activeProjectName)
           : "Project options"

    readonly property string pid:
        (typeof projectController !== "undefined" && projectController)
        ? projectController.activeProjectId : ""
    readonly property bool ready:
        (typeof credentialManager !== "undefined") && credentialManager && pid.length > 0

    // Snapshot of the identity catalogue: [{id,name,email}]. Rebuilt on open.
    property var choices: []
    // Snapshot of the active project's repos: [{path,name}]. Rebuilt on open.
    property var repos: []

    function labelForId(id) {
        for (var i = 0; i < choices.length; ++i)
            if (choices[i].id === id)
                return choices[i].name
        return ""
    }

    // Build a combo model: row 0 is the inherit row, then one row per identity.
    // inheritedId drives the "(Inherit — X)" text; each row carries its id.
    function comboModel(inheritedId) {
        var rows = [{ id: "", label: "(Inherit — " + labelForId(inheritedId) + ")" }]
        for (var i = 0; i < choices.length; ++i)
            rows.push({ id: choices[i].id,
                        label: choices[i].name + " <" + choices[i].email + ">" })
        return rows
    }

    // Index of the row whose id matches currentId (0 = inherit).
    function indexForId(model, currentId) {
        for (var i = 0; i < model.length; ++i)
            if (model[i].id === currentId)
                return i
        return 0
    }

    // Deliberately a snapshot, not a live binding: choices/repos and the
    // "(Inherit — X)" labels they feed are captured once on open and do not
    // react to identity/store changes while the dialog is up. That's sound
    // because this dialog is modal — nothing else can mutate identities or
    // project repos concurrently — so don't "fix" this into reactive bindings.
    function refresh() {
        choices = ready ? credentialManager.identityChoices() : []
        repos = (typeof projectController !== "undefined" && projectController)
                ? projectController.activeProjectRepos() : []
    }

    function openDialog() {
        refresh()
        open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 18

        // ---- Project identity ----
        Label { text: "Project identity"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

        AppComboBox {
            id: projectCombo
            objectName: "projectIdentityCombo"
            Layout.fillWidth: true
            textRole: "label"
            enabled: dialog.ready
            // Project-level "inherit" falls back to the GLOBAL identity (not the
            // project default — that's what this combo itself sets). Pass "" so
            // inheritedIdentityId resolves straight to the global id/name.
            property var rows: dialog.comboModel(dialog.ready ? credentialManager.inheritedIdentityId("") : "")
            model: rows
            currentIndex: dialog.indexForId(rows, dialog.ready ? credentialManager.projectDefaultId(dialog.pid) : "")
            onActivated: (index) => {
                if (dialog.ready)
                    credentialManager.setProjectDefault(dialog.pid, rows[index].id)
            }
        }

        // ---- Repositories ----
        Label { text: "Repositories"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

        ListView {
            id: repoList
            objectName: "projectRepoList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 260)
            clip: true
            interactive: true
            model: dialog.repos
            spacing: 6
            ScrollBar.vertical: AppScrollBar {}
            delegate: RowLayout {
                width: ListView.view.width
                spacing: 8
                required property var modelData
                Label {
                    text: modelData.name
                    color: theme.textPrimary
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                AppComboBox {
                    Layout.preferredWidth: 220
                    textRole: "label"
                    enabled: dialog.ready
                    // Inherited resolution for a repo with no project id override
                    // is the project default else global — inheritedIdentityId(pid).
                    property var rows: dialog.comboModel(dialog.ready ? credentialManager.inheritedIdentityId(dialog.pid) : "")
                    model: rows
                    currentIndex: dialog.indexForId(rows, dialog.ready ? credentialManager.repoOverrideId(modelData.path) : "")
                    onActivated: (index) => {
                        if (dialog.ready)
                            credentialManager.setRepoOverride(modelData.path, rows[index].id)
                    }
                }
            }
        }
    }
}
