# Plan 40 — Tabbed Options dialog + git-config identity seed

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-21 |
| **Status** | `done` |
| **Spec** | [`spec/product/2026-07-21-tabbed-options-dialog-design.md`](../spec/product/2026-07-21-tabbed-options-dialog-design.md) |
| **Depends on** | Plan 36 (identity), Plan 38 (forge central UI) |

**Goal:** Turn the Options dialog into a 4-tab settings surface (Appearance /
Git / Identity / Accounts), delete the secondary Credentials dialog and the
View app menu, and seed the Identity tab from global git config on first run.

**Architecture:** Pure `ui/` relocation plus one additive `core/` reader. A new
shared `AppTabButton.qml` (extracted from `WorkingPane`) drives both tab strips.
`OptionsDialog` becomes a `TabBar` + `StackLayout` shell over four focused tab
components; `IdentityDialog.qml` is deleted, its content split across the
Identity and Accounts tabs. Core gains `GitRepo::globalIdentity()`, a repo-less
reader of `user.name`/`user.email`; `CredentialManager` uses it to seed one
Global identity when its store is empty.

**Tech stack:** C++23 + libgit2 (`git_config_open_default`) in `core/`; Qt
Quick / QML in `ui/`; Catch2 for core tests, QtTest for ui tests.

## Global constraints

- **No Qt in `core/`.** `GitRepo::globalIdentity()` speaks `std` only
  (`ConfigIdentity{std::string name, email}`), returns `Expected<T>`, no
  exceptions ([`spec/engineering/engineering.md`](../spec/engineering/engineering.md)).
- New `ui/` sources → `ui/qml/qml.qrc` (QML files are loaded via `qrc:/qml/...`);
  no `ui/CMakeLists.txt` change needed for pure QML additions. New/renamed test
  cases live in the existing test files already in `tests/CMakeLists.txt`.
- **Preserve object names** so existing interaction tests keep resolving controls:
  `identityList`, `identityName`, `identityEmail`, `identityAdd`, `hostList`,
  `hostName`, `hostToken`, `hostAdd`, `sshKeyList`, `sshAdd`, `optionsDialog`,
  `optionsCloseButton`.
- **Colour from theme tokens only**; Allman braces via `.clang-format`; `m_`
  members; lowercase file names for C++.
- Keep passing: all existing tests in `test_qml_shell.cpp`, `test_qml_menu_bar.cpp`,
  `test_credential_manager.cpp`, `test_git_repo_identity.cpp`.

---

## Task 1: Core — `GitRepo::globalIdentity()` repo-less reader

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare next to `effectiveIdentity`)
- Modify: `core/src/gitrepo.cpp` (implement near `effectiveIdentity`, ~line 2751)
- Test: `tests/test_git_repo_identity.cpp`

**Interfaces:**
- Produces: `static Expected<ConfigIdentity> GitRepo::globalIdentity();`
  Reads merged default config (global/system/xdg) with no open repo. Missing
  `user.name`/`user.email` → empty strings, **not** an error. On failure to open
  the default config, returns `std::unexpected(GitError)`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_git_repo_identity.cpp`. It writes a `.gitconfig` into a temp
dir, points libgit2's GLOBAL search path at it (the core suite otherwise blanks
that path — see `tests/support/git_config_isolation.cpp`), reads it back, then
restores isolation.

```cpp
TEST_CASE("globalIdentity reads user.name/user.email from global config", "[identity]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path()
        / ("gittide_gid_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    {
        std::ofstream cfg(dir / ".gitconfig");
        cfg << "[user]\n\tname = Global Gwen\n\temail = gwen@global.com\n";
    }

    // Point libgit2's GLOBAL search path at our temp dir for this case only.
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL,
                     dir.generic_string().c_str());

    auto id = GitRepo::globalIdentity();
    REQUIRE(id.has_value());
    REQUIRE(id->name == "Global Gwen");
    REQUIRE(id->email == "gwen@global.com");

    // Restore suite isolation so later cases don't see this config.
    git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, "");
    fs::remove_all(dir);
}

TEST_CASE("globalIdentity returns empty (not error) when keys are unset", "[identity]")
{
    // Suite isolation already blanks the GLOBAL search path, so no keys exist.
    auto id = GitRepo::globalIdentity();
    REQUIRE(id.has_value());
    REQUIRE(id->name.empty());
    REQUIRE(id->email.empty());
}
```

Add includes at the top of the test file if missing: `#include <fstream>`.

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build -R test_git_repo_identity --output-on-failure`
Expected: FAIL — `globalIdentity` is not a member of `GitRepo`.

- [ ] **Step 3: Declare the method**

In `core/include/gittide/gitrepo.hpp`, directly after the `effectiveIdentity()`
declaration (~line 227):

```cpp
    // The identity from the merged DEFAULT config (global/system/xdg) with NO
    // open repo — what a fresh checkout would inherit. Missing keys yield empty
    // strings (not an error). Static counterpart to setGlobalIdentity().
    static Expected<ConfigIdentity> globalIdentity();
```

- [ ] **Step 4: Implement**

In `core/src/gitrepo.cpp`, immediately after `effectiveIdentity()` (~line 2769):

```cpp
Expected<ConfigIdentity> GitRepo::globalIdentity()
{
    git_config* cfg = nullptr;
    int         rc  = git_config_open_default(&cfg);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    ConfigIdentity id;
    auto           readKey = [&](const char* key, std::string& out)
    {
        git_buf buf = GIT_BUF_INIT;
        if (git_config_get_string_buf(&buf, cfg, key) == 0)
            out.assign(buf.ptr, buf.size);
        git_buf_dispose(&buf);
    };
    readKey("user.name", id.name);
    readKey("user.email", id.email);
    return id;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `ctest --test-dir build -R test_git_repo_identity --output-on-failure`
Expected: PASS (both new cases + all existing identity cases).

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_identity.cpp
git commit -m "feat(core): add GitRepo::globalIdentity() repo-less config reader"
```

## Task 2: Extract shared `AppTabButton.qml`

**Files:**
- Create: `ui/qml/AppTabButton.qml`
- Modify: `ui/qml/WorkingPane.qml` (drop local `component MainTab`, use `AppTabButton`)
- Modify: `ui/qml/qml.qrc` (register the new file)
- Test: covered by existing `test_qml_shell.cpp` (WorkingPane tab behaviour) — no
  new test; this is a no-visual-change refactor.

**Interfaces:**
- Produces: `AppTabButton` — a themed `TabButton` (active = `theme.textPrimary`
  demibold over a 2px `theme.accent` bottom underline; inactive =
  `theme.textSecondary`; hover on inactive tints with `theme.surfaceOverlay`).
  `implicitHeight: 36`, `implicitWidth: 96` defaults, overridable by the caller.

- [ ] **Step 1: Create the component**

`ui/qml/AppTabButton.qml` — lift verbatim from `WorkingPane.qml`'s current
`component MainTab` body (lines ~99–119):

```qml
import QtQuick
import QtQuick.Controls.Basic

// Flat tab button shared by the working-pane tab strip and the Options dialog.
// Active = text.primary (demibold) over a 2px accent underline; inactive =
// text.secondary; hover tints an inactive row.
TabButton {
    id: tabBtn
    implicitHeight: 36
    implicitWidth: 96
    contentItem: Label {
        text: tabBtn.text
        color: tabBtn.checked ? theme.textPrimary : theme.textSecondary
        font.pixelSize: 13
        font.weight: tabBtn.checked ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: (tabBtn.hovered && !tabBtn.checked) ? theme.surfaceOverlay : "transparent"
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 2
            color: theme.accent
            visible: tabBtn.checked
        }
    }
}
```

- [ ] **Step 2: Register in qrc**

In `ui/qml/qml.qrc`, add next to the other `App*.qml` primitives:

```xml
    <file>AppTabButton.qml</file>
```

- [ ] **Step 3: Switch WorkingPane to the shared component**

In `ui/qml/WorkingPane.qml`: delete the `component MainTab: TabButton { … }`
block (lines ~99–120) and replace the three usages (lines ~126–128):

```qml
                AppTabButton { text: "Changes" }
                AppTabButton { text: "History" }
                AppTabButton { text: "Graph" }
```

- [ ] **Step 4: Build + run the shell suite**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_qml_shell --output-on-failure`
Expected: PASS — WorkingPane tabs unchanged (Changes/History/Graph switch, graph
refresh still fires).

- [ ] **Step 5: Commit**

```bash
git add ui/qml/AppTabButton.qml ui/qml/WorkingPane.qml ui/qml/qml.qrc
git commit -m "refactor(ui): extract shared AppTabButton from WorkingPane"
```

## Task 3: Options dialog → tabbed shell + Appearance/Git tabs

**Files:**
- Create: `ui/qml/OptionsAppearanceTab.qml`, `ui/qml/OptionsGitTab.qml`
- Modify: `ui/qml/OptionsDialog.qml` (becomes shell)
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp`

**Interfaces:**
- Consumes: `AppTabButton` (Task 2), `appSettings` store.
- Produces: `OptionsDialog` with `objectName: "optionsTabBar"` present; tab index
  0 = Appearance, 1 = Git (Identity/Accounts added in Task 4). Each tab component
  takes `required property var appSettings`.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_qml_shell.cpp` (new slot; register nowhere else needed —
QtTest auto-runs private slots):

```cpp
    void options_dialog_has_tab_bar()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

        QObject* root = engine.rootObjects().first();
        QObject* opts = root->findChild<QObject*>(QStringLiteral("optionsDialog"));
        QVERIFY(opts != nullptr);
        QVERIFY(opts->findChild<QObject*>(QStringLiteral("optionsTabBar")) != nullptr);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build -R test_qml_shell --output-on-failure`
Expected: FAIL — no `optionsTabBar`.

- [ ] **Step 3: Create the Appearance tab**

`ui/qml/OptionsAppearanceTab.qml` — theme radios lifted from current
`OptionsDialog.qml` lines 24–70:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Appearance: theme mode (applies instantly, auto-persists via appSettings).
ColumnLayout {
    id: tab
    required property var appSettings
    spacing: 8

    Label {
        text: "Theme"
        color: theme.textMuted
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }

    ButtonGroup { id: themeGroup }

    RowLayout {
        spacing: 16
        AppRadioButton {
            objectName: "themeSystemRadio"; text: "System"
            ButtonGroup.group: themeGroup
            checked: tab.appSettings.themeMode === 0
            onClicked: { tab.appSettings.themeMode = 0; theme.setMode(0) }
        }
        AppRadioButton {
            objectName: "themeDarkRadio"; text: "Dark"
            ButtonGroup.group: themeGroup
            checked: tab.appSettings.themeMode === 1
            onClicked: { tab.appSettings.themeMode = 1; theme.setMode(1) }
        }
        AppRadioButton {
            objectName: "themeLightRadio"; text: "Light"
            ButtonGroup.group: themeGroup
            checked: tab.appSettings.themeMode === 2
            onClicked: { tab.appSettings.themeMode = 2; theme.setMode(2) }
        }
    }
}
```

- [ ] **Step 4: Create the Git tab**

`ui/qml/OptionsGitTab.qml` — pull-default radios lifted from `OptionsDialog.qml`
lines 79–109:

```qml
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
```

- [ ] **Step 5: Rewrite OptionsDialog as the shell**

Replace the whole body of `ui/qml/OptionsDialog.qml`. Width grows to 560. The
`StackLayout` here will gain Identity/Accounts children in Task 4; for now it
holds the two tabs and two placeholders are NOT added (tab bar shows all four but
Identity/Accounts wire in Task 4 — to keep this task self-contained, list only
the two working tabs and their two buttons now, then Task 4 inserts the rest).

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// App settings, tabbed. Appearance/Git write to appSettings (instant, auto-persist);
// Identity/Accounts manage credentials (added in Task 4). No OK/Cancel — Close only.
AppDialog {
    id: dialog
    objectName: "optionsDialog"
    title: "Options"
    width: 560
    padding: 20

    required property var appSettings

    contentItem: ColumnLayout {
        spacing: 16

        TabBar {
            id: tabBar
            objectName: "optionsTabBar"
            Layout.fillWidth: true
            background: null
            spacing: 0
            AppTabButton { text: "Appearance"; implicitWidth: 110 }
            AppTabButton { text: "Git"; implicitWidth: 110 }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: tabBar.currentIndex

            OptionsAppearanceTab { appSettings: dialog.appSettings }
            OptionsGitTab { appSettings: dialog.appSettings }
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            objectName: "optionsCloseButton"
            variant: "secondary"
            text: "Close"
            onClicked: dialog.close()
        }
    }
}
```

- [ ] **Step 6: Register new files in qrc**

```xml
    <file>OptionsAppearanceTab.qml</file>
    <file>OptionsGitTab.qml</file>
```

- [ ] **Step 7: Build + run**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_qml_shell --output-on-failure`
Expected: PASS — `options_dialog_has_tab_bar` green; `options_and_about_dialogs_exist`
still green (`optionsDialog` found).

Note: `Main.qml` still has `onIdentityRequested: identityDialog.openDialog()` on
the OptionsDialog instance — OptionsDialog no longer declares that signal, so QML
will warn about an unknown signal handler. That handler is removed in Task 4;
tolerate the warning until then (it does not fail the load).

- [ ] **Step 8: Commit**

```bash
git add ui/qml/OptionsDialog.qml ui/qml/OptionsAppearanceTab.qml ui/qml/OptionsGitTab.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): make Options dialog tabbed with Appearance/Git tabs"
```

## Task 4: Identity + Accounts tabs; delete IdentityDialog; rewire Main

**Files:**
- Create: `ui/qml/OptionsIdentityTab.qml`, `ui/qml/OptionsAccountsTab.qml`
- Delete: `ui/qml/IdentityDialog.qml`
- Modify: `ui/qml/OptionsDialog.qml` (add the two tabs), `ui/qml/qml.qrc`,
  `ui/qml/Main.qml`
- Test: `tests/ui/test_qml_shell.cpp`

**Interfaces:**
- Consumes: global context props `credentialManager`, `identityModel`,
  `hostModel`, `sshKeyModel`, `repoVm`, `projectController` (already installed).
- Produces: `identityList`/`hostList`/`sshKeyList` reachable **inside**
  `optionsDialog`; `identityDialog` and `manageIdentitiesButton` no longer exist.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_qml_shell.cpp`:

```cpp
    void identity_moved_into_options_tabs()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

        QObject* root = engine.rootObjects().first();
        QObject* opts = root->findChild<QObject*>(QStringLiteral("optionsDialog"));
        QVERIFY(opts != nullptr);

        // Credential controls now live inside the Options dialog.
        QVERIFY(opts->findChild<QObject*>(QStringLiteral("identityList")) != nullptr);
        QVERIFY(opts->findChild<QObject*>(QStringLiteral("hostList")) != nullptr);
        QVERIFY(opts->findChild<QObject*>(QStringLiteral("sshKeyList")) != nullptr);

        // The old secondary dialog and its launch button are gone.
        QVERIFY(root->findChild<QObject*>(QStringLiteral("identityDialog")) == nullptr);
        QVERIFY(root->findChild<QObject*>(QStringLiteral("manageIdentitiesButton")) == nullptr);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build -R test_qml_shell --output-on-failure`
Expected: FAIL — `identityList` not under `optionsDialog`; `identityDialog` still
present.

- [ ] **Step 3: Create the Identity tab**

`ui/qml/OptionsIdentityTab.qml` — move the "Identities" section + its assignment
logic out of `IdentityDialog.qml`. Carries the `repoOverrideId`/`projectDefaultId`
state, `refreshAssignments()`, and the two `Connections` (lines 19–52, 66–116 of
the current `IdentityDialog.qml`) so badges and Global/Project/Repo buttons work
unchanged:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Identity: named git identities (+ global / project / repo assignment).
// Backed by credentialManager + identityModel context properties.
ColumnLayout {
    id: tab
    spacing: 18

    property string repoOverrideId: ""
    property string projectDefaultId: ""
    readonly property bool hasRepo: (typeof repoVm !== "undefined") && repoVm && repoVm.repoOpen
    readonly property string activeProjectId:
        (typeof projectController !== "undefined" && projectController) ? projectController.activeProjectId : ""

    function refreshAssignments() {
        tab.repoOverrideId = (tab.hasRepo && typeof credentialManager !== "undefined" && credentialManager)
            ? credentialManager.repoOverrideId(repoVm.repoPath) : ""
        tab.projectDefaultId = (tab.activeProjectId.length > 0 && typeof credentialManager !== "undefined" && credentialManager)
            ? credentialManager.projectDefaultId(tab.activeProjectId) : ""
    }

    Component.onCompleted: refreshAssignments()

    Connections {
        target: (typeof credentialManager !== "undefined") ? credentialManager : null
        function onChanged() { tab.refreshAssignments() }
    }
    Connections {
        target: (typeof repoVm !== "undefined") ? repoVm : null
        function onChanged() { tab.refreshAssignments() }
    }

    Label { text: "Identities"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

    ListView {
        objectName: "identityList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 200)
        clip: true
        interactive: false
        model: (typeof identityModel !== "undefined") ? identityModel : null
        spacing: 4
        delegate: Rectangle {
            width: ListView.view.width
            implicitHeight: idRow.implicitHeight + 12
            radius: 6
            color: (model.identityId === tab.repoOverrideId || model.identityId === tab.projectDefaultId)
                   ? theme.surfaceOverlay : "transparent"
            RowLayout {
                id: idRow
                anchors.fill: parent; anchors.margins: 6; spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Label { text: model.name; color: theme.textPrimary; font.pixelSize: 13 }
                    Label { text: model.email; color: theme.textMuted; font.pixelSize: 11 }
                }
                Label { text: "Global"; visible: model.isGlobal; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { text: "Project"; visible: model.identityId === tab.projectDefaultId && tab.activeProjectId.length > 0; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                Label { text: "Repo"; visible: model.identityId === tab.repoOverrideId && tab.hasRepo; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                AppButton { variant: "secondary"; compact: true; text: "Global"; enabled: !model.isGlobal; onClicked: credentialManager.setGlobalIdentity(model.identityId) }
                AppButton { variant: "secondary"; compact: true; text: "Project"; visible: tab.activeProjectId.length > 0; enabled: model.identityId !== tab.projectDefaultId; onClicked: credentialManager.setProjectDefault(tab.activeProjectId, model.identityId) }
                AppButton { variant: "secondary"; compact: true; text: "Repo"; visible: tab.hasRepo; enabled: model.identityId !== tab.repoOverrideId; onClicked: credentialManager.setRepoOverride(repoVm.repoPath, model.identityId) }
                AppButton { variant: "danger"; compact: true; text: "✕"; onClicked: credentialManager.removeIdentity(model.identityId) }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 8
        TextField {
            id: nameField; objectName: "identityName"; Layout.fillWidth: true; placeholderText: "Name"; color: theme.textPrimary
            background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: nameField.activeFocus ? theme.accent : theme.border }
        }
        TextField {
            id: emailField; objectName: "identityEmail"; Layout.fillWidth: true; placeholderText: "email@example.com"; color: theme.textPrimary
            background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: emailField.activeFocus ? theme.accent : theme.border }
        }
        AppButton {
            objectName: "identityAdd"; variant: "primary"; compact: true; text: "Add"
            enabled: nameField.text.length > 0 && emailField.text.length > 0
            onClicked: { credentialManager.addIdentity(nameField.text, emailField.text); nameField.text = ""; emailField.text = "" }
        }
    }
}
```

- [ ] **Step 4: Create the Accounts tab**

`ui/qml/OptionsAccountsTab.qml` — move the "Host accounts" + "SSH keys" sections
(lines 120–225 of the current `IdentityDialog.qml`) plus the `statusText` state
and the `hostValidated` handling:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Accounts: forge host tokens (validated + keychain-stored) and SSH keys.
ColumnLayout {
    id: tab
    spacing: 18
    property string statusText: ""

    Connections {
        target: (typeof credentialManager !== "undefined") ? credentialManager : null
        function onHostValidated(ok, message) {
            tab.statusText = message
            if (ok) { hostField.text = ""; hostTokenField.text = ""; hostApiBaseField.text = "" }
        }
    }

    // ---- Host accounts ----
    Label { text: "Host accounts (HTTPS / forge tokens)"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

    ListView {
        objectName: "hostList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 120)
        clip: true
        interactive: false
        model: (typeof hostModel !== "undefined") ? hostModel : null
        spacing: 4
        delegate: RowLayout {
            width: ListView.view.width; spacing: 8
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: model.host + (model.username ? "  ·  " + model.username : ""); color: theme.textPrimary; font.pixelSize: 13 }
                Label { text: model.kind; color: theme.textMuted; font.pixelSize: 11 }
            }
            AppButton { variant: "danger"; compact: true; text: "✕"; onClicked: credentialManager.removeHost(model.hostId) }
        }
    }
    ColumnLayout {
        Layout.fillWidth: true; spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            AppComboBox { id: hostKind; Layout.preferredWidth: 110; model: ["github", "gitlab", "generic"] }
            TextField {
                id: hostField; objectName: "hostName"; Layout.fillWidth: true; placeholderText: "github.com"; color: theme.textPrimary
                background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: hostField.activeFocus ? theme.accent : theme.border }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            TextField {
                id: hostApiBaseField; Layout.fillWidth: true; placeholderText: "API base (optional)"; color: theme.textPrimary
                background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: hostApiBaseField.activeFocus ? theme.accent : theme.border }
            }
            TextField {
                id: hostTokenField; objectName: "hostToken"; Layout.fillWidth: true; placeholderText: "Personal access token"; echoMode: TextInput.Password; color: theme.textPrimary
                background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: hostTokenField.activeFocus ? theme.accent : theme.border }
            }
            AppButton {
                objectName: "hostAdd"; variant: "primary"; compact: true; text: "Validate & add"
                enabled: hostField.text.length > 0 && hostTokenField.text.length > 0
                onClicked: { tab.statusText = "Validating…"; credentialManager.validateAndAddHost(hostField.text, hostKind.currentText, hostApiBaseField.text, hostTokenField.text) }
            }
        }
        Label { visible: tab.statusText.length > 0; text: tab.statusText; color: theme.textMuted; font.pixelSize: 11 }
    }

    Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

    // ---- SSH keys ----
    Label { text: "SSH keys"; color: theme.textMuted; font.pixelSize: 11; font.weight: Font.DemiBold }

    ListView {
        objectName: "sshKeyList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(contentHeight, 0), 120)
        clip: true
        interactive: false
        model: (typeof sshKeyModel !== "undefined") ? sshKeyModel : null
        spacing: 4
        delegate: RowLayout {
            width: ListView.view.width; spacing: 8
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: model.label + (model.hasPassphrase ? "  🔒" : ""); color: theme.textPrimary; font.pixelSize: 13 }
                Label { text: model.privateKeyPath; color: theme.textMuted; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
            }
            AppButton { variant: "danger"; compact: true; text: "✕"; onClicked: credentialManager.removeSshKey(model.sshKeyId) }
        }
    }
    ColumnLayout {
        Layout.fillWidth: true; spacing: 6
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            TextField {
                id: sshLabel; Layout.preferredWidth: 120; placeholderText: "Label"; color: theme.textPrimary
                background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshLabel.activeFocus ? theme.accent : theme.border }
            }
            TextField {
                id: sshPriv; Layout.fillWidth: true; placeholderText: "Private key path"; color: theme.textPrimary
                background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshPriv.activeFocus ? theme.accent : theme.border }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            TextField {
                id: sshPub; Layout.fillWidth: true; placeholderText: "Public key path (optional)"; color: theme.textPrimary
                background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshPub.activeFocus ? theme.accent : theme.border }
            }
            TextField {
                id: sshPass; Layout.preferredWidth: 140; placeholderText: "Passphrase"; echoMode: TextInput.Password; color: theme.textPrimary
                background: Rectangle { radius: 6; color: theme.surfaceBase; border.width: 1; border.color: sshPass.activeFocus ? theme.accent : theme.border }
            }
            AppButton {
                objectName: "sshAdd"; variant: "primary"; compact: true; text: "Add"
                enabled: sshLabel.text.length > 0 && sshPriv.text.length > 0
                onClicked: { credentialManager.addSshKey(sshLabel.text, sshPub.text, sshPriv.text, sshPass.text); sshLabel.text = ""; sshPriv.text = ""; sshPub.text = ""; sshPass.text = "" }
            }
        }
    }
}
```

- [ ] **Step 5: Wire the two tabs into OptionsDialog**

In `ui/qml/OptionsDialog.qml`: add two `AppTabButton`s to the `TabBar` and two
children to the `StackLayout`. Because Identity/Accounts can be tall, wrap each
in a `Flickable` so the dialog height stays bounded.

Add to the `TabBar` (after the Git button):

```qml
            AppTabButton { text: "Identity"; implicitWidth: 110 }
            AppTabButton { text: "Accounts"; implicitWidth: 110 }
```

Add to the `StackLayout` (after `OptionsGitTab`):

```qml
            Flickable {
                implicitHeight: Math.min(identityTab.implicitHeight, 480)
                contentHeight: identityTab.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                OptionsIdentityTab { id: identityTab; width: parent.width }
            }
            Flickable {
                implicitHeight: Math.min(accountsTab.implicitHeight, 480)
                contentHeight: accountsTab.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                OptionsAccountsTab { id: accountsTab; width: parent.width }
            }
```

- [ ] **Step 6: Delete IdentityDialog + rewire Main.qml**

Delete the file and its qrc line; add the two new qrc lines:

```bash
git rm ui/qml/IdentityDialog.qml
```

In `ui/qml/qml.qrc`: remove `<file>IdentityDialog.qml</file>`; add:

```xml
    <file>OptionsIdentityTab.qml</file>
    <file>OptionsAccountsTab.qml</file>
```

In `ui/qml/Main.qml`: remove the `onIdentityRequested: identityDialog.openDialog()`
line from the `OptionsDialog { … }` block (line ~275) and delete the
`IdentityDialog { id: identityDialog }` instance (line ~277).

- [ ] **Step 7: Build + run**

Run: `cmake --build build --parallel && ctest --test-dir build -R test_qml_shell --output-on-failure`
Expected: PASS — `identity_moved_into_options_tabs` green; no unknown-signal
warning for `onIdentityRequested` (handler removed).

- [ ] **Step 8: Commit**

```bash
git add ui/qml/OptionsIdentityTab.qml ui/qml/OptionsAccountsTab.qml ui/qml/OptionsDialog.qml ui/qml/qml.qrc ui/qml/Main.qml tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): fold Credentials into Options Identity/Accounts tabs; drop IdentityDialog"
```

## Task 5: Remove the View app menu

**Files:**
- Modify: `ui/qml/AppMenuBar.qml`
- Test: `tests/ui/test_qml_menu_bar.cpp`

**Interfaces:**
- Produces: menu bar with three buttons — `menuBtnFile`, `menuBtnEdit`,
  `menuBtnRepository`. `menuBtnView` removed.

- [ ] **Step 1: Update the test to expect three buttons**

In `tests/ui/test_qml_menu_bar.cpp`, rename
`app_menu_bar_exposes_four_buttons_each_with_a_menu` →
`app_menu_bar_exposes_three_buttons_each_with_a_menu` and drop `menuBtnView`
from the loop; add an explicit assertion it is gone:

```cpp
    void app_menu_bar_exposes_three_buttons_each_with_a_menu()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        MenuBarRepoStub repo;

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/AppMenuBar.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> bar(comp.create());
        QVERIFY2(bar != nullptr, qPrintable(comp.errorString()));

        bar->setProperty("repo", QVariant::fromValue(static_cast<QObject*>(&repo)));
        bar->setProperty("appSettings", QVariant());

        for (const QString& name : {QStringLiteral("menuBtnFile"),
                                    QStringLiteral("menuBtnEdit"),
                                    QStringLiteral("menuBtnRepository")})
        {
            QObject* btn = bar->findChild<QObject*>(name);
            QVERIFY2(btn != nullptr, qPrintable(name + " button not found"));
            QObject* menu = btn->property("menu").value<QObject*>();
            QVERIFY2(menu != nullptr, qPrintable(name + " has no menu"));
        }
        QVERIFY(bar->findChild<QObject*>(QStringLiteral("menuBtnView")) == nullptr);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build -R test_qml_menu_bar --output-on-failure`
Expected: FAIL — `menuBtnView` still present.

- [ ] **Step 3: Remove the View menu**

In `ui/qml/AppMenuBar.qml`, delete the entire `MenuBarButton { objectName:
"menuBtnView" … }` block (lines 64–88) and update the header comment (line 6–7)
to read `File / Edit / Repository`.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R test_qml_menu_bar --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/AppMenuBar.qml tests/ui/test_qml_menu_bar.cpp
git commit -m "feat(ui): remove View app menu (theme now lives only in Options)"
```

## Task 6: First-run identity seed in CredentialManager

**Files:**
- Modify: `ui/src/credentialmanager.cpp` (constructor), `ui/include/gittide/ui/credentialmanager.hpp`
  (only if a private helper is declared)
- Test: `tests/ui/test_credential_manager.cpp`

**Interfaces:**
- Consumes: `GitRepo::globalIdentity()` (Task 1), `CredentialsStore::identities()`,
  `CredentialManager::addIdentity` / `setGlobalIdentity`.
- Produces: after construction against an empty store with a global git identity,
  exactly one identity exists and it is the global one.

- [ ] **Step 1: Write the failing tests**

Add to `tests/ui/test_credential_manager.cpp`. Both drive the seed through
libgit2's GLOBAL search path (the ui runner blanks it in `tests/ui/main.cpp`, so
each test sets it, then restores). Needs `#include <git2.h>` and `<fstream>` at
the top of the file if not present.

```cpp
    void seeds_identity_from_global_config_when_store_empty()
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path()
            / ("gittide_seed_" + std::to_string(::QRandomGenerator::global()->generate()));
        fs::create_directories(dir);
        { std::ofstream cfg(dir / ".gitconfig");
          cfg << "[user]\n\tname = Seed Sam\n\temail = sam@seed.com\n"; }
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL,
                         dir.generic_string().c_str());

        ProjectStore     projects;
        CredentialsStore creds; // empty
        CredentialManager cm(&creds, tempCredPath(), &projects);

        QCOMPARE(int(creds.identities().size()), 1);
        QCOMPARE(QString::fromStdString(creds.identities().front().name), QStringLiteral("Seed Sam"));
        QCOMPARE(QString::fromStdString(cm.globalIdentityId()),
                 QString::fromStdString(creds.identities().front().id));

        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, "");
        fs::remove_all(dir);
    }

    void does_not_seed_when_store_already_has_identity()
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path()
            / ("gittide_noseed_" + std::to_string(::QRandomGenerator::global()->generate()));
        fs::create_directories(dir);
        { std::ofstream cfg(dir / ".gitconfig");
          cfg << "[user]\n\tname = Seed Sam\n\temail = sam@seed.com\n"; }
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL,
                         dir.generic_string().c_str());

        ProjectStore     projects;
        CredentialsStore creds;
        creds.addIdentity("Existing Ed", "ed@x.com"); // store not empty
        CredentialManager cm(&creds, tempCredPath(), &projects);

        QCOMPARE(int(creds.identities().size()), 1); // no extra seeded identity
        QCOMPARE(QString::fromStdString(creds.identities().front().name), QStringLiteral("Existing Ed"));

        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, "");
        fs::remove_all(dir);
    }
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ctest --test-dir build -R test_credential_manager --output-on-failure`
Expected: FAIL — `seeds_identity_from_global_config_when_store_empty` finds 0
identities.

- [ ] **Step 3: Seed in the constructor**

In `ui/src/credentialmanager.cpp`, at the end of the constructor body (after the
secrets block, ~line 65), add:

```cpp
    // First-run convenience: if we hold no identities yet but the user already
    // has a global git identity, adopt it as the Global identity so the Identity
    // tab is not empty. Guarded on emptiness ⇒ genuinely one-time (never
    // resurrects a deleted identity, never runs once the user has any).
    if (m_store->identities().empty())
    {
        if (auto gid = gittide::GitRepo::globalIdentity();
            gid.has_value() && !gid->name.empty() && !gid->email.empty())
        {
            const auto& id = m_store->addIdentity(gid->name, gid->email);
            setGlobalIdentity(QString::fromStdString(id.id));
        }
    }
```

`GitRepo` is already included (`#include <gittide/gitrepo.hpp>`, line 4). Verify
`CredentialsStore::addIdentity` returns a reference exposing `.id` (it does — see
`test_credential_manager.cpp` `creds.addIdentity(...).id` usage). If `setGlobalIdentity`
emits `changed()` during construction that is fine — no listeners are connected yet.

- [ ] **Step 4: Run tests to verify they pass**

Run: `ctest --test-dir build -R test_credential_manager --output-on-failure`
Expected: PASS — both new cases green; existing cases unaffected.

- [ ] **Step 5: Commit**

```bash
git add ui/src/credentialmanager.cpp tests/ui/test_credential_manager.cpp
git commit -m "feat(ui): seed Global identity from git config on first run"
```

## Task 7: Docs close-out

**Files:**
- Modify: `docs/spec/product/…` app-menu + options-flow sections (drop View;
  describe the tabbed dialog), and any `docs/spec/product/spec.md`-referenced
  menu list.
- Modify: this plan's front-matter `Status` → `done` and fill **Outcome**.
- Modify: the design spec `Status` → `shipped`, add `Shipped: 2026-07-21`.

- [ ] **Step 1: Update the spec**

Grep for the menu list and the options/credentials description:

```bash
grep -rn "View" docs/spec/product docs/spec/spec.md | grep -i menu
grep -rln "Credentials\|Manage identities\|OptionsDialog" docs/spec
```

Edit the menu list to `File / Edit / Repository`; replace any "opens the
Credentials dialog" wording with the single tabbed Options dialog (Appearance /
Git / Identity / Accounts), and note the first-run git-config seed.

- [ ] **Step 2: Full suite green**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: PASS — whole suite.

- [ ] **Step 3: Flip statuses + fill Outcome**

Set this plan's `Status: done`, fill the **Outcome** block; set the design spec's
`Status: shipped` + `Shipped: 2026-07-21`.

- [ ] **Step 4: Commit**

```bash
git add docs/
git commit -m "docs: close out Plan 40 (tabbed Options dialog + identity seed)"
```

---

## Outcome

Shipped 2026-07-21 on branch `plan40-tabbed-options` (commits `de10b3c..18e4ead`).

- **Tabbed Options dialog.** `ui/qml/OptionsDialog.qml` is now a `TabBar`
  (`optionsTabBar`, built from the new shared `ui/qml/AppTabButton.qml`) over a
  `StackLayout` with four tab components: `OptionsAppearanceTab.qml` (theme),
  `OptionsGitTab.qml` (pull default), `OptionsIdentityTab.qml` (git identities +
  Global/Project/Repo assignment), `OptionsAccountsTab.qml` (host tokens + SSH
  keys). `AppTabButton` was extracted from `WorkingPane.qml`'s former inline
  `MainTab` and is reused there.
- **Credentials dialog removed.** `ui/qml/IdentityDialog.qml` deleted; its content
  lives in the Identity/Accounts tabs. `Main.qml` no longer instantiates it and
  the "Manage identities…" button is gone.
- **View menu removed on every platform.** `AppMenuBar.qml` (Windows/Linux) and
  `NativeMenuBar.qml` (macOS) both dropped View ▸ Theme; theme is reached only via
  Options → Appearance.
- **First-run identity seed.** New static `GitRepo::globalIdentity()`
  (`core`, `git_config_open_default`) reads the user's global `user.name`/`user.email`
  with no open repo; `CredentialManager`'s constructor seeds one Global identity
  from it when its store is empty, so the Identity tab isn't blank for
  already-configured users.
- **Spec updated:** `docs/spec/product/app-menu.md` (§4 tabbed dialog, §7/§8.2
  menu bar → File · Edit · Repository, file tables), `docs/spec/engineering/engineering.md`
  (D51 credential surface → Options Identity/Accounts tabs + the seed),
  `docs/spec/product/product.md` (Options → Identity/Accounts flow). Design spec
  `docs/spec/product/2026-07-21-tabbed-options-dialog-design.md` marked shipped.
- **Tests:** core `globalIdentity` reader (2 cases), UI `options_dialog_has_tab_bar`,
  `identity_moved_into_options_tabs`, menu bar three-buttons, and CredentialManager
  seed/no-seed (2 cases). Full suite 203/203 green.
- **Known minors (deferred, non-blocking — from the final whole-branch review):**
  - `GitRepo::globalIdentity()` and `effectiveIdentity()` share a near-identical
    `readKey` body — candidate for a shared `readIdentity(git_config*)` helper.
  - Both `NativeMenuBar.qml` and `AppMenuBar.qml` still declare an unused
    `appSettings` property (only the removed theme items read it). Removing it
    means also dropping the injection at each bind site (Main.qml / TitleBar /
    the menu-bar test's `setProperty` stub) — deferred to avoid unrelated churn.
  - Assignment-badge refresh in the Identity tab now keys off
    `credentialManager.changed()` / `repoVm.changed()` (plus one
    `Component.onCompleted`) instead of the old on-open `refreshAssignments()`.
    Covered for the realistic triggers; the residual gap is an *active-project*
    change with no accompanying repo/credential change leaving `projectDefaultId`
    stale — fix would be a `Connections` on the project controller's
    active-project signal, or re-running `refreshAssignments()` on
    `optionsDialog.opened`.
