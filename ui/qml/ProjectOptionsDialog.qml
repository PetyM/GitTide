import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Project Options — per-project + per-repo git identity for the ACTIVE project.
// Backed by credentialManager + projectController context properties. Opened
// from the app menu (TitleBar app-icon popup / macOS Preferences group).
AppDialog {
    id: dialog
    objectName: "projectOptionsDialog"
    width: 480
    padding: 20
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
        var rows = [{ id: "", label: "Inherit — " + labelForId(inheritedId) }]
        for (var i = 0; i < choices.length; ++i)
            rows.push({ id: choices[i].id,
                        label: choices[i].name + " · " + choices[i].email })
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

    // No footer — assignments apply live (setProjectDefault/setRepoOverride are
    // immediate). The header ✕ and Escape dismiss.
    footer: null

    contentItem: ColumnLayout {
        spacing: 18

        // ---- Project identity ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: "Project identity"
                color: theme.textSecondary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
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
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Used to sign commits in every repository below, unless a repository overrides it."
                color: theme.textMuted
                font.pixelSize: 11
                lineHeight: 1.2
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

        // ---- Repositories ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "Repositories"
                color: theme.textSecondary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: dialog.repos.length > 0
                text: dialog.repos.length
                color: theme.textMuted
                font.pixelSize: 11
            }
        }

        // Empty state: a project with no repositories yet.
        Label {
            objectName: "projectRepoEmpty"
            visible: dialog.repos.length === 0
            Layout.fillWidth: true
            topPadding: 16
            bottomPadding: 16
            horizontalAlignment: Text.AlignHCenter
            text: "No repositories in this project yet."
            color: theme.textMuted
            font.pixelSize: 12
        }

        ListView {
            id: repoList
            objectName: "projectRepoList"
            visible: dialog.repos.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 320)
            clip: true
            interactive: true
            model: dialog.repos
            spacing: 6
            ScrollBar.vertical: AppScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                implicitHeight: 54
                radius: 8
                color: rowHover.hovered ? theme.surfaceOverlay : theme.surfaceBase
                border.width: 1
                border.color: theme.border
                HoverHandler { id: rowHover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            Layout.fillWidth: true
                            text: modelData.name
                            color: theme.textPrimary
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.path
                            color: theme.textMuted
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }
                    AppComboBox {
                        Layout.preferredWidth: 188
                        textRole: "label"
                        enabled: dialog.ready
                        // A repo inherits project-default-then-global — inheritedIdentityId(pid).
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
}
