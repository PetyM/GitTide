import QtQuick
import Qt.labs.platform as Native

// Native macOS system menu bar. On macOS the menu belongs in the top-of-screen
// system bar, not inside the window — so instead of the in-window AppMenuBar
// (which Windows/Linux keep in the custom TitleBar), Main.qml instantiates this
// via a Loader when Qt.platform.os === "osx".
//
// It mirrors the AppMenuBar action set plus the app-icon popup (Options / About /
// Quit) and emits the same signals TitleBar exposes, so Main.qml can bind both
// the custom title bar and this native bar to identical handlers. Enable rules
// match AppMenuBar §7.3 (see docs/spec/product/app-menu.md).
//
// Injected by the host: appSettings (live QSettings store) and repo (RepoViewModel).
Native.MenuBar {
    id: bar
    objectName: "nativeMenuBar"

    property var appSettings
    property var repo

    // App-level requests (mirror the TitleBar app-icon popup).
    signal optionsRequested()
    signal aboutRequested()
    // Per-repo requests (mirror AppMenuBar).
    signal openRepoFolderRequested()
    signal undoLastCommitRequested()
    signal discardAllRequested()
    signal mergeRequested()
    signal rebaseRequested()
    signal stashRequested()
    signal popStashRequested()

    readonly property bool repoReady: !!repo && repo.repoOpen
    readonly property bool busy: !!repo && (repo.rebaseInProgress || repo.mergeInProgress)

    // Application menu. macOS relocates role-tagged items into the bold app menu
    // named after the application, giving About / Preferences (⌘,) / Quit (⌘Q)
    // their conventional home.
    Native.Menu {
        title: "GitTide"
        Native.MenuItem {
            text: "About GitTide"
            role: Native.MenuItem.AboutRole
            onTriggered: bar.aboutRequested()
        }
        Native.MenuItem {
            text: "Preferences…"
            role: Native.MenuItem.PreferencesRole
            shortcut: "Ctrl+,"   // Qt maps Ctrl → ⌘ on macOS
            onTriggered: bar.optionsRequested()
        }
        Native.MenuItem {
            role: Native.MenuItem.QuitRole   // macOS supplies text + ⌘Q
            onTriggered: Qt.quit()
        }
    }

    Native.Menu {
        title: "File"
        Native.MenuItem {
            objectName: "nativeOpenRepoFolderItem"
            text: "Open repository folder"
            enabled: bar.repoReady
            onTriggered: bar.openRepoFolderRequested()
        }
    }

    Native.Menu {
        title: "Edit"
        Native.MenuItem {
            objectName: "nativeUndoLastCommitItem"
            text: "Undo last commit"
            enabled: bar.repoReady && !bar.busy
            onTriggered: bar.undoLastCommitRequested()
        }
        Native.MenuItem {
            objectName: "nativeDiscardAllItem"
            text: "Discard all changes"
            enabled: bar.repoReady && !!bar.repo && bar.repo.dirty
            onTriggered: bar.discardAllRequested()
        }
    }

    Native.Menu {
        title: "Repository"
        Native.MenuItem {
            objectName: "nativeMergeItem"
            text: "Merge into current branch…"
            enabled: bar.repoReady && !bar.busy
            onTriggered: bar.mergeRequested()
        }
        Native.MenuItem {
            objectName: "nativeRebaseItem"
            text: "Rebase current branch…"
            enabled: bar.repoReady && !bar.busy
            onTriggered: bar.rebaseRequested()
        }
        Native.MenuSeparator {}
        Native.MenuItem {
            objectName: "nativeStashItem"
            text: "Stash all changes"
            enabled: bar.repoReady && !!bar.repo && bar.repo.dirty
            onTriggered: bar.stashRequested()
        }
        Native.MenuItem {
            objectName: "nativePopStashItem"
            text: "Pop latest stash"
            enabled: bar.repoReady && !!bar.repo && bar.repo.stashAvailable
            onTriggered: bar.popStashRequested()
        }
    }
}
