import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Horizontal text menu bar for the title bar (spec §7). Hosts File / Edit / View
// / Repository. Each button opens its AppMenu. Per-repo items are enabled only
// when a repo is open; destructive items use AppMenuItem.destructive.
RowLayout {
    id: bar
    objectName: "appMenuBar"
    spacing: 2

    // App settings store (themeMode) + repo VM are injected by the host.
    property var appSettings
    property var repo

    // Per-repo action requests — bound by the host (Main.qml via TitleBar).
    signal openRepoFolderRequested()
    signal undoLastCommitRequested()
    signal discardAllRequested()
    signal mergeRequested()
    signal rebaseRequested()
    signal stashRequested()
    signal popStashRequested()

    readonly property bool repoReady: !!repo && repo.repoOpen
    readonly property bool busy: !!repo && (repo.rebaseInProgress || repo.mergeInProgress)

    MenuBarButton {
        objectName: "menuBtnFile"
        label: "File"
        menu: AppMenu {
            objectName: "menuFile"
            AppMenuItem {
                objectName: "openRepoFolderItem"
                text: "Open repository folder"
                enabled: bar.repoReady
                onTriggered: bar.openRepoFolderRequested()
            }
        }
    }

    MenuBarButton {
        objectName: "menuBtnEdit"
        label: "Edit"
        menu: AppMenu {
            objectName: "menuEdit"
            AppMenuItem {
                objectName: "undoLastCommitItem"
                text: "Undo last commit"
                enabled: bar.repoReady && !bar.busy
                onTriggered: bar.undoLastCommitRequested()
            }
            AppMenuItem {
                objectName: "discardAllItem"
                text: "Discard all changes"
                destructive: true
                enabled: bar.repoReady && !!bar.repo && bar.repo.dirty
                onTriggered: bar.discardAllRequested()
            }
        }
    }

    MenuBarButton {
        objectName: "menuBtnView"
        label: "View"
        menu: AppMenu {
            objectName: "menuView"
            Menu {
                title: "Theme"
                AppMenuItem {
                    objectName: "themeSystemItem"
                    text: "System"
                    onTriggered: { theme.setMode(0); if (bar.appSettings) bar.appSettings.themeMode = 0 }
                }
                AppMenuItem {
                    objectName: "themeDarkItem"
                    text: "Dark"
                    onTriggered: { theme.setMode(1); if (bar.appSettings) bar.appSettings.themeMode = 1 }
                }
                AppMenuItem {
                    objectName: "themeLightItem"
                    text: "Light"
                    onTriggered: { theme.setMode(2); if (bar.appSettings) bar.appSettings.themeMode = 2 }
                }
            }
        }
    }

    MenuBarButton {
        objectName: "menuBtnRepository"
        label: "Repository"
        menu: AppMenu {
            objectName: "menuRepository"
            AppMenuItem {
                objectName: "mergeItem"
                text: "Merge into current branch…"
                enabled: bar.repoReady && !bar.busy
                onTriggered: bar.mergeRequested()
            }
            AppMenuItem {
                objectName: "rebaseItem"
                text: "Rebase current branch…"
                enabled: bar.repoReady && !bar.busy
                onTriggered: bar.rebaseRequested()
            }
            MenuSeparator {
                padding: 6
                contentItem: Rectangle { implicitHeight: 1; color: theme.border }
            }
            AppMenuItem {
                objectName: "stashItem"
                text: "Stash all changes"
                enabled: bar.repoReady && !!bar.repo && bar.repo.dirty
                onTriggered: bar.stashRequested()
            }
            AppMenuItem {
                objectName: "popStashItem"
                text: "Pop latest stash"
                enabled: bar.repoReady && !!bar.repo && bar.repo.stashAvailable
                onTriggered: bar.popStashRequested()
            }
        }
    }
}
