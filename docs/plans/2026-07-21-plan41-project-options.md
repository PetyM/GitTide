# Plan 41 — Project Options dialog (per-project & per-repo identity)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans`.

| | |
|--|--|
| **Date** | 2026-07-21 |
| **Status** | `planned` |
| **Spec** | [`spec/product/2026-07-21-project-options-design.md`](../spec/product/2026-07-21-project-options-design.md); [`spec/product/product.md` §Identity & credentials](../spec/product/product.md) |
| **Depends on** | Plan 36 (identity), Plan 40 (tabbed Options) |

**Goal:** Add a **Project Options** dialog — reached from the Sidebar project
switcher — where the user picks a project-wide identity and overrides identity
per repository, and move project/repo assignment out of the global Options →
Identity tab (which keeps global identity only).

**Architecture:** The core resolution + git-config write path already exist
(`CredentialsStore`, `CredentialManager::applyIdentityToRepo`). This plan is
UI + two additive `CredentialManager` helpers and one additive
`ProjectController` helper that feed a new `ProjectOptionsDialog.qml`. No change
to the identity write model.

**Tech stack:** Qt Quick / QML (Controls.Basic), C++23 ViewModel layer, QtTest
(`gittide_ui_tests` aggregate target).

## Global constraints

- Invariants in [`spec/engineering/engineering.md`](../spec/engineering/engineering.md):
  no Qt in `core/`; Qt types only at the ViewModel boundary; paths via
  `generic_u8string()`, never `.string()`.
- New `ui/` sources → none (helpers land in existing `.cpp`); new `.qml` → add
  to `ui/qml/qml.qrc`. No new test files — extend existing lists already in
  `tests/CMakeLists.txt` (`test_credential_manager.cpp`,
  `test_project_controller.cpp`, `test_qml_shell.cpp`).
- Must keep passing: existing `objectName`s `identityList`, `identityName`,
  `identityEmail`, `identityAdd`, `optionsTabBar`, `projectSwitcher`,
  `projectMenu`, `newProjectItem`, `deleteProjectItem`.
- QML must guard nullable context props (`credentialManager`,
  `projectController`, `identityModel`) with the existing
  `typeof x !== "undefined" && x` idiom so `Main.qml` still loads in shell tests
  that pass `nullptr`.
- Identity model role names (from `IdentityListModel`): `identityId`, `name`,
  `email`, `isGlobal`.

Build / test commands:
```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R gittide_ui_tests
```

---

## Task 1: `CredentialManager` helpers — `identityChoices` + `inheritedIdentityId`

**Files:**
- Modify: `ui/include/gittide/ui/credentialmanager.hpp` (declare 2 methods; add `#include <QVariantList>`)
- Modify: `ui/src/credentialmanager.cpp` (define 2 methods)
- Test: `tests/ui/test_credential_manager.cpp` (add 2 slots)

**Interfaces produced:**
- `Q_INVOKABLE QVariantList CredentialManager::identityChoices() const` — one
  `QVariantMap{ "id", "name", "email" }` per catalogue identity, store order.
- `Q_INVOKABLE QString CredentialManager::inheritedIdentityId(const QString& projectId) const`
  — `projectDefault(projectId)` if non-empty, else `globalIdentity()`; empty
  `projectId` ⇒ global id.

- [ ] **Step 1: Write the failing tests**

Add to `tests/ui/test_credential_manager.cpp` (inside the `private slots:` block):

```cpp
    void identity_choices_lists_all_identities()
    {
        ProjectStore     projects;
        CredentialsStore creds;
        auto& a = creds.addIdentity("Alice", "alice@x.com");
        auto& b = creds.addIdentity("Bob", "bob@x.com");

        CredentialManager cm(&creds, tempCredPath(), &projects);
        const QVariantList rows = cm.identityChoices();

        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value("id").toString(), QString::fromStdString(a.id));
        QCOMPARE(rows.at(0).toMap().value("name").toString(), QStringLiteral("Alice"));
        QCOMPARE(rows.at(0).toMap().value("email").toString(), QStringLiteral("alice@x.com"));
        QCOMPARE(rows.at(1).toMap().value("id").toString(), QString::fromStdString(b.id));
    }

    void inherited_identity_prefers_project_default_then_global()
    {
        ProjectStore     projects;
        CredentialsStore creds;
        auto& g = creds.addIdentity("Global Gwen", "gwen@x.com");
        auto& p = creds.addIdentity("Project Pat", "pat@x.com");
        creds.setGlobalIdentity(g.id);
        creds.setProjectDefault("proj-1", p.id);

        CredentialManager cm(&creds, tempCredPath(), &projects);

        // Project with a default → its default id.
        QCOMPARE(cm.inheritedIdentityId(QStringLiteral("proj-1")), QString::fromStdString(p.id));
        // Project without a default → the global id.
        QCOMPARE(cm.inheritedIdentityId(QStringLiteral("proj-none")), QString::fromStdString(g.id));
        // Empty project id → the global id.
        QCOMPARE(cm.inheritedIdentityId(QString()), QString::fromStdString(g.id));
    }
```

> Note: the `CredentialManager` constructor seeds an identity from the global git
> config only when the store is empty (Plan 40). Both tests add identities first,
> so no seeding occurs and the counts above are exact.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — compile error, `identityChoices` / `inheritedIdentityId` not members of `CredentialManager`.

- [ ] **Step 3: Declare the helpers**

In `ui/include/gittide/ui/credentialmanager.hpp`, add near the top with the
other Qt includes:

```cpp
#include <QVariantList>
```

Then in the `// --- Display helpers for the settings UI ---` block (just after
`projectDefaultId`), add:

```cpp
    /// The catalogue as a plain array for QML combo models: one
    /// { id, name, email } map per identity, in store order.
    Q_INVOKABLE QVariantList identityChoices() const;

    /// The identity id a repo inherits with no override: the project's default
    /// if set, else the global identity. Empty projectId ⇒ the global id.
    Q_INVOKABLE QString inheritedIdentityId(const QString& projectId) const;
```

- [ ] **Step 4: Define the helpers**

In `ui/src/credentialmanager.cpp`, add (near the other display helpers such as
`projectDefaultId`). Include `<QVariantMap>` at the top if not already present:

```cpp
QVariantList CredentialManager::identityChoices() const
{
    QVariantList out;
    for (const auto& id : m_store->identities())
    {
        QVariantMap m;
        m.insert(QStringLiteral("id"), QString::fromStdString(id.id));
        m.insert(QStringLiteral("name"), QString::fromStdString(id.name));
        m.insert(QStringLiteral("email"), QString::fromStdString(id.email));
        out.append(m);
    }
    return out;
}

QString CredentialManager::inheritedIdentityId(const QString& projectId) const
{
    if (!projectId.isEmpty())
    {
        const std::string pd = m_store->projectDefault(projectId.toStdString());
        if (!pd.empty())
            return QString::fromStdString(pd);
    }
    return QString::fromStdString(m_store->globalIdentity());
}
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS (new slots green, existing green).

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/credentialmanager.hpp ui/src/credentialmanager.cpp tests/ui/test_credential_manager.cpp
git commit -m "feat(ui): CredentialManager identityChoices + inheritedIdentityId helpers"
```

---

## Task 2: `ProjectController::activeProjectRepos`

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp` (declare method; add `#include <QVariantList>`)
- Modify: `ui/src/projectcontroller.cpp` (define method)
- Test: `tests/ui/test_project_controller.cpp` (add 1 slot)

**Interfaces produced:**
- `Q_INVOKABLE QVariantList ProjectController::activeProjectRepos() const` — one
  `QVariantMap{ "path", "name" }` per top-level repo of the active project;
  `name` = `RepoRef.alias` if non-empty, else the path's basename; empty list
  when no project is active.

**Interfaces consumed (Task 1):** none.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_project_controller.cpp` (inside `private slots:`):

```cpp
    void active_project_repos_lists_path_and_name()
    {
        ProjectStore store;
        store.projects().push_back(
            Project{.id    = "id-a",
                    .name  = "Work",
                    .repos = {RepoRef{.path = "/home/u/api", .alias = "api"},
                              RepoRef{.path = "/home/u/web-client", .alias = ""}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        const QVariantList rows = controller.activeProjectRepos();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value("path").toString(), QStringLiteral("/home/u/api"));
        QCOMPARE(rows.at(0).toMap().value("name").toString(), QStringLiteral("api"));       // alias
        QCOMPARE(rows.at(1).toMap().value("path").toString(), QStringLiteral("/home/u/web-client"));
        QCOMPARE(rows.at(1).toMap().value("name").toString(), QStringLiteral("web-client")); // basename
    }

    void active_project_repos_empty_without_active_project()
    {
        ProjectStore      store;
        ProjectController controller(&store);
        QCOMPARE(controller.activeProjectRepos().size(), 0);
    }
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — `activeProjectRepos` not a member of `ProjectController`.

- [ ] **Step 3: Declare the method**

In `ui/include/gittide/ui/projectcontroller.hpp`, add `#include <QVariantList>`
near the other Qt includes, then in the `public slots:` section (near
`lastActiveRepo`) add:

```cpp
    // The active project's top-level repos as { path, name } maps for the
    // Project Options dialog. name = alias if set, else the path basename.
    // Empty when no project is active.
    Q_INVOKABLE QVariantList activeProjectRepos() const;
```

- [ ] **Step 4: Define the method**

In `ui/src/projectcontroller.cpp` (ensure `<QVariantMap>` and `<filesystem>` are
included — `<filesystem>` already is via the header):

```cpp
QVariantList ProjectController::activeProjectRepos() const
{
    QVariantList out;
    for (const auto& r : activeRepos())
    {
        QVariantMap m;
        m.insert(QStringLiteral("path"), QString::fromStdString(r.path));
        QString name = QString::fromStdString(r.alias);
        if (name.isEmpty())
        {
            const auto u8 = std::filesystem::path(r.path).filename().generic_u8string();
            name = QString::fromStdString(std::string(u8.begin(), u8.end()));
        }
        m.insert(QStringLiteral("name"), name);
        out.append(m);
    }
    return out;
}
```

> `activeRepos()` returns the active project's `std::vector<RepoRef>` (empty when
> none active), so the empty-project case falls out for free.

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): ProjectController::activeProjectRepos for Project Options"
```

---

## Task 3: `ProjectOptionsDialog.qml` + Sidebar entry + Main wiring

**Files:**
- Create: `ui/qml/ProjectOptionsDialog.qml`
- Modify: `ui/qml/qml.qrc` (register the new file)
- Modify: `ui/qml/Sidebar.qml` (add menu item + `projectOptionsRequested()` signal)
- Modify: `ui/qml/Main.qml` (instantiate dialog + wire the signal)
- Test: `tests/ui/test_qml_shell.cpp` (add 1 slot)

**Interfaces consumed:**
- Task 1: `credentialManager.identityChoices()`, `credentialManager.inheritedIdentityId(pid)`,
  `credentialManager.projectDefaultId(pid)`, `credentialManager.repoOverrideId(path)`,
  `credentialManager.setProjectDefault(pid, id)`, `credentialManager.setRepoOverride(path, id)`.
- Task 2: `projectController.activeProjectRepos()`, `projectController.activeProjectId`,
  `projectController.activeProjectName`.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_qml_shell.cpp` (inside `private slots:`). It loads
`Main.qml` (context props null, like the sibling tests) and asserts the new
surface is present in the object tree:

```cpp
    void project_options_dialog_and_menu_entry_exist()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

        QCOMPARE(engine.rootObjects().size(), 1);
        QObject* root = engine.rootObjects().first();

        // Entry point in the project switcher menu.
        QVERIFY(root->findChild<QObject*>(QStringLiteral("projectOptionsItem")) != nullptr);

        // The dialog and its two pickers are instantiated (hidden until opened).
        QObject* dlg = root->findChild<QObject*>(QStringLiteral("projectOptionsDialog"));
        QVERIFY(dlg != nullptr);
        QVERIFY(dlg->findChild<QObject*>(QStringLiteral("projectIdentityCombo")) != nullptr);
        QVERIFY(dlg->findChild<QObject*>(QStringLiteral("projectRepoList")) != nullptr);
    }
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — `projectOptionsItem` / `projectOptionsDialog` not found.

- [ ] **Step 3: Create `ProjectOptionsDialog.qml`**

```qml
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
            property var rows: dialog.comboModel(dialog.ready ? credentialManager.inheritedIdentityId(dialog.pid) : "")
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
```

> The inherit row for a repo shows the project-default-else-global name
> (`inheritedIdentityId(pid)`), matching the resolution `resolveLocalIdentity`
> uses. Setting `setProjectDefault` from the project combo re-materializes the
> open repo immediately; the per-repo combos still show their own overrides.

- [ ] **Step 4: Register the file in `qml.qrc`**

In `ui/qml/qml.qrc`, add a line next to the other Options entries (after
`OptionsAccountsTab.qml`):

```xml
    <file>ProjectOptionsDialog.qml</file>
```

- [ ] **Step 5: Add the Sidebar menu item + signal**

In `ui/qml/Sidebar.qml`, add a signal near the existing ones (line ~15):

```qml
    signal projectOptionsRequested()
```

Then in `projectMenu` (after the `deleteProjectItem` `AppMenuItem`, keeping it
inside the menu), add:

```qml
            AppMenuItem {
                objectName: "projectOptionsItem"
                text: "Project options…"
                enabled: projectController && projectController.activeProjectId.length > 0
                onTriggered: sidebar.projectOptionsRequested()
            }
```

- [ ] **Step 6: Wire it in `Main.qml`**

In `ui/qml/Main.qml`, on the `Sidebar { id: sidebar … }` instance (near
`onDeleteProjectRequested`), add:

```qml
                onProjectOptionsRequested: projectOptionsDialog.openDialog()
```

Then, next to the other dialog instances (e.g. after the `OptionsDialog { … }`
block near line 272), add:

```qml
    ProjectOptionsDialog {
        id: projectOptionsDialog
    }
```

- [ ] **Step 7: Run to verify pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — the new shell slot green, `Main.qml` loads with no QML warnings.

- [ ] **Step 8: Commit**

```bash
git add ui/qml/ProjectOptionsDialog.qml ui/qml/qml.qrc ui/qml/Sidebar.qml ui/qml/Main.qml tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): Project Options dialog (per-project + per-repo identity)"
```

---

## Task 4: Strip project/repo assignment from Options → Identity tab

**Files:**
- Modify: `ui/qml/OptionsIdentityTab.qml`
- Test: `tests/ui/test_qml_shell.cpp` (extend the existing identity-tab assertion)

**Interfaces consumed:** none new. Removes the tab's dependence on `repoVm` /
`projectController` for assignment.

- [ ] **Step 1: Write the failing assertion**

Extend the existing shell test that reaches into the Options dialog (the one
asserting `identityList` / `hostList` / `sshKeyList` around line 663). Add, in
that same slot, assertions that the removed assignment controls are gone. If a
`objectName` does not exist today, add it in Step 3 so the assertion is precise —
otherwise assert on structural absence via a new objectName we introduce:

Append to that slot's body:

```cpp
        // Project/Repo assignment moved to Project Options — the Identity tab no
        // longer carries those per-row buttons.
        QVERIFY(opts->findChild<QObject*>(QStringLiteral("identityAssignProject")) == nullptr);
        QVERIFY(opts->findChild<QObject*>(QStringLiteral("identityAssignRepo")) == nullptr);
```

> These `objectName`s do not exist on the current buttons. Step 3 removes those
> buttons entirely, so the assertions stay true; they exist to lock the removal
> against a future re-introduction under those names.

- [ ] **Step 2: Run to verify current state**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS already (names absent). This asserts the invariant; the real work
is the QML simplification below, which must keep this slot green **and** keep
`identityList` present.

- [ ] **Step 3: Simplify `OptionsIdentityTab.qml`**

Replace the whole file with the assignment-free version (keeps identities list,
add row, per-row **Global** button + badge, delete):

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Options → Identity: named git identities + the GLOBAL assignment only.
// Per-project / per-repo assignment lives in the Project Options dialog.
// Backed by credentialManager + identityModel context properties.
ColumnLayout {
    id: tab
    spacing: 18

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
            color: model.isGlobal ? theme.surfaceOverlay : "transparent"
            RowLayout {
                id: idRow
                anchors.fill: parent; anchors.margins: 6; spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Label { text: model.name; color: theme.textPrimary; font.pixelSize: 13 }
                    Label { text: model.email; color: theme.textMuted; font.pixelSize: 11 }
                }
                Label { text: "Global"; visible: model.isGlobal; color: theme.accent; font.pixelSize: 10; font.weight: Font.DemiBold }
                AppButton { variant: "secondary"; compact: true; text: "Global"; enabled: !model.isGlobal; onClicked: credentialManager.setGlobalIdentity(model.identityId) }
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

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `identityList` / `identityName` / `identityEmail` / `identityAdd`
still resolve; the two `identityAssign*` absence assertions hold; `Main.qml`
loads with no QML warnings about missing `repoVm` bindings from this tab.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/OptionsIdentityTab.qml tests/ui/test_qml_shell.cpp
git commit -m "refactor(ui): Identity tab keeps global assignment only (project/repo moved to Project Options)"
```

---

## Task 5: Documentation close-out

**Files:**
- Modify: `docs/spec/product/product.md` (Identity & credentials section)
- Modify: `docs/plans/2026-07-21-plan41-project-options.md` (this file — Status + Outcome)
- Modify: `docs/plans/index.md` (add the Plan 41 row)
- Modify: `docs/spec/product/2026-07-21-project-options-design.md` (Status → shipped)

- [ ] **Step 1: Update `product.md`**

In the **Identity & credentials** section (around lines 165–200), reflect the
new split: the Options → Identity tab manages identities + the **global**
assignment; **project and per-repo** identity are set in the **Project Options**
dialog reached from the project switcher. Replace the sentence that says the
Identity tab handles "global / per-project / per-repo" assignment with wording
that points per-project/per-repo assignment to Project Options. Keep the
resolution-order description (repo override → project default → global) intact.

- [ ] **Step 2: Fill this plan's Outcome + flip Status**

Set **Status** to `done` and complete the Outcome section below.

- [ ] **Step 3: Add the index row**

In `docs/plans/index.md`, append after the Plan 40 row:

```markdown
| [Plan 41 — Project Options dialog (per-project/per-repo identity)](2026-07-21-plan41-project-options.md) | 2026-07-21 | done | product · ui |
```

- [ ] **Step 4: Flip the design spec Status**

In `docs/spec/product/2026-07-21-project-options-design.md`, change
`**Status:** planned` → `**Status:** shipped` and add a `**Shipped:**` line.

- [ ] **Step 5: Commit**

```bash
git add docs/
git commit -m "docs: close out Plan 41 (Project Options dialog)"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: <sections>.
> - Code: <files/types>.
