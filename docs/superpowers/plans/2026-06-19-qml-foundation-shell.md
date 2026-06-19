# QML Foundation + Sidebar Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a runnable Qt Quick / QML application window that renders the GitTide sidebar (brand, project name, repo tree with submodules) from the existing `RepoListModel`, themed by the existing design tokens — proving the QML stack before committing the full UI migration.

**Architecture:** A *parallel* QML app target (`gittide_qml_app`) behind a CMake option. It reuses `core/`, `ProjectController`, `RepoListModel` and `ThemeManager` unchanged; a new `QmlTheme` `QObject` exposes design tokens to QML as bindable properties; QML files live in `ui/qml/` and are loaded via `QQmlApplicationEngine`. The existing QWidgets app and all its tests stay intact and are **not** modified or deleted in this plan.

**Tech Stack:** C++23, Qt 6 (Quick, Qml, QuickControls2, QuickTest), CMake ≥ 3.28, QCoro (existing), Qt Test (existing UI suite).

## Global Constraints

- **No Qt in `core/`** — this plan touches only `ui/`, `app/`, `tests/`. Do not add includes to `core/`.
- **libgit2 and nlohmann/json stay PRIVATE to `core/`** — unchanged here.
- **Colour comes from a theme token, never a hex literal in a component** — QML reads `theme.<token>`; the only hex literals allowed are inside `QmlTheme`'s lane palette and tests asserting token values.
- **Paths via `generic_u8string()`, never `.string()`** — not exercised in this plan, but keep it if any path crosses the boundary.
- **TDD** — write the failing test first. New `ui/` sources → `ui/CMakeLists.txt`. New UI tests → `gittide_ui_test_sources` in `tests/CMakeLists.txt` **and** a `#include` + `QTest::qExec` block in `tests/ui/main.cpp` (miss either and it silently runs zero tests).
- **Qt 6 via `find_package`, never FetchContent.** Quick/Qml/QuickControls2/QuickTest are present in the installed Qt (verified: `/home/michal/Qt/6.8.3/gcc_64` and `6.10.1`).
- **Code style:** Allman braces, `m_` members, lowercase file names, `.clang-format`. Split renames from content changes.
- **Lane palette (graph) is the one documented exception to single-accent:** `#22D3EE`, `#A371F7`, `#3FB950`, `#D29922`, `#F778BA`. Defined once in `QmlTheme`.
- **Commands:** configure `cmake -S . -B build`; build `cmake --build build --parallel`; UI tests `ctest --test-dir build --output-on-failure -R gittide_ui_tests`. UI tests run headless via `QT_QPA_PLATFORM=offscreen` (already wired in `tests/CMakeLists.txt`).

---

## File Structure

**New files**
- `ui/include/gittide/ui/qmltheme.hpp` — `QmlTheme : QObject`; design tokens as `Q_PROPERTY`s for QML binding.
- `ui/src/qmltheme.cpp` — implementation; wraps a `ThemeManager*`, re-emits on theme change.
- `ui/qml/Main.qml` — root `ApplicationWindow`; window background token; hosts `Sidebar` + a placeholder main pane.
- `ui/qml/Sidebar.qml` — brand, project name, repo `TreeView` (submodule nesting), "Add repository" button.
- `ui/qml/qml.qrc` — bundles `Main.qml` + `Sidebar.qml` under prefix `/qml`.
- `ui/include/gittide/ui/qmlcontext.hpp` — `installQmlContext()` free function; sets the context properties both the app and the test use (single source of wiring).
- `ui/src/qmlcontext.cpp` — implementation.
- `app/qml_main.cpp` — `gittide_qml_app` entry point.
- `tests/ui/test_qml_theme.cpp` — `QmlTheme` property tests (pure, no QML runtime).
- `tests/ui/test_qml_shell.cpp` — loads `Main.qml` via `QQmlApplicationEngine`, asserts it loads and wires the model.

**Modified files**
- `cmake/Dependencies.cmake:47` — add `Qml Quick QuickControls2 QuickTest` to the `find_package(Qt6 …)` components.
- `CMakeLists.txt:23` — add `option(GITGUI_BUILD_QML …)`; `app/qml_main` target gated on it.
- `ui/CMakeLists.txt` — add `qmltheme.*`, `qmlcontext.*`, `qml.qrc`; link `Qt6::Qml Qt6::Quick Qt6::QuickControls2`.
- `app/CMakeLists.txt` — add the `gittide_qml_app` executable.
- `tests/CMakeLists.txt:41` — add the two new test files; link `Qt6::Qml Qt6::Quick` to `gittide_ui_tests`.
- `tests/ui/main.cpp` — `Q_INIT_RESOURCE(qml)`, `#include` the two new tests, add their `QTest::qExec` blocks.

**Explicitly deferred (NOT in this plan — named follow-ups):**
- Recursive submodule depth in `RepoListModel` (today it is two levels: `Row` → `SubRow`). The QML `TreeView` already renders arbitrary depth; the *model* change is a separate plan ("submodule tree model"). This plan renders whatever depth the model provides and **does not** claim ≥3 levels yet.
- Branch bar, Changes/History tabs, diff, overlays, empty states, light-theme polish.
- Multi-window + session restore for the QML path (the shell opens a single window).
- Deleting the QWidgets UI (happens only after full QML parity in a later cutover plan).

---

### Task 1: `QmlTheme` — design tokens for QML

**Files:**
- Create: `ui/include/gittide/ui/qmltheme.hpp`
- Create: `ui/src/qmltheme.cpp`
- Test: `tests/ui/test_qml_theme.cpp`
- Modify: `ui/CMakeLists.txt` (add the two sources), `tests/CMakeLists.txt` (add the test), `tests/ui/main.cpp` (include + exec)

**Interfaces:**
- Consumes: `gittide::ui::ThemeManager` (`currentTheme()`, `iconResource()`, `themeChanged()`), `gittide::ui::Theme` (token `QString`s).
- Produces: `class QmlTheme : public QObject` with constructor `QmlTheme(ThemeManager* manager, QObject* parent = nullptr)`; readable `Q_PROPERTY`s (all `QColor` except as noted): `dark` (bool), `surfaceBase`, `surfaceRaised`, `surfaceOverlay`, `border`, `textPrimary`, `textSecondary`, `textMuted`, `accent`, `accentHover`, `head`, `stateAdded`, `stateModified`, `stateDeleted`, `stateUntracked`, `stateConflict`, `laneColors` (`QVariantList`, CONSTANT), `iconSource` (`QString`); signal `changed()`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_qml_theme.cpp`:

```cpp
#include <QtTest>
#include <QColor>
#include <QSignalSpy>

#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;

class TestQmlTheme : public QObject
{
    Q_OBJECT
private slots:
    void dark_tokens_match_theme()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        QVERIFY(theme.property("dark").toBool());
        QCOMPARE(theme.property("accent").value<QColor>(), QColor("#22D3EE"));
        QCOMPARE(theme.property("surfaceBase").value<QColor>(), QColor("#0B1623"));
        QCOMPARE(theme.property("head").value<QColor>(), QColor("#FFFFFF"));
        QCOMPARE(theme.property("iconSource").toString(),
                 QStringLiteral("qrc:/icons/gittide-icon.svg"));
    }

    void lane_palette_is_five_hues_starting_cyan()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        const QVariantList lanes = theme.property("laneColors").toList();
        QCOMPARE(lanes.size(), 5);
        QCOMPARE(lanes.front().value<QColor>(), QColor("#22D3EE"));
        QCOMPARE(lanes.at(1).value<QColor>(), QColor("#A371F7"));
    }

    void theme_switch_emits_changed_and_updates_tokens()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QSignalSpy spy(&theme, &QmlTheme::changed);

        mgr.setMode(ThemeManager::Mode::Light);

        QCOMPARE(spy.count(), 1);
        QVERIFY(!theme.property("dark").toBool());
        QCOMPARE(theme.property("accent").value<QColor>(), QColor("#0891B2"));
    }
};

#include "test_qml_theme.moc"
```

- [ ] **Step 2: Wire the test into the runner (both edits)**

In `tests/CMakeLists.txt`, add to the `gittide_ui_test_sources` list (after line 41's `test_project_list_model.cpp`):

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_theme.cpp
```

In `tests/ui/main.cpp`, add near the other includes:

```cpp
#include "test_qml_theme.cpp"
```

and in `main()`, alongside the other `QTest::qExec` calls:

```cpp
    { TestQmlTheme t; status |= QTest::qExec(&t, argc, argv); }
```

(Match the exact accumulator/return idiom already used in `tests/ui/main.cpp`.)

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel`
Expected: FAIL to compile — `gittide/ui/qmltheme.hpp` not found.

- [ ] **Step 4: Write the header**

Create `ui/include/gittide/ui/qmltheme.hpp`:

```cpp
#pragma once
#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>

#include "gittide/ui/thememanager.hpp"

namespace gittide::ui {

// Exposes the active Theme's tokens to QML as bindable properties. Wraps a
// ThemeManager and re-emits changed() when the manager's theme changes, so every
// QML binding refreshes on a live theme switch. Colours are QColor (QML-native).
// laneColors is the graph lane palette — the one place GitTide uses >1 hue — and
// is CONSTANT because it does not vary by theme.
class QmlTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QColor surfaceBase READ surfaceBase NOTIFY changed)
    Q_PROPERTY(QColor surfaceRaised READ surfaceRaised NOTIFY changed)
    Q_PROPERTY(QColor surfaceOverlay READ surfaceOverlay NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY changed)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY changed)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY changed)
    Q_PROPERTY(QColor head READ head NOTIFY changed)
    Q_PROPERTY(QColor stateAdded READ stateAdded NOTIFY changed)
    Q_PROPERTY(QColor stateModified READ stateModified NOTIFY changed)
    Q_PROPERTY(QColor stateDeleted READ stateDeleted NOTIFY changed)
    Q_PROPERTY(QColor stateUntracked READ stateUntracked NOTIFY changed)
    Q_PROPERTY(QColor stateConflict READ stateConflict NOTIFY changed)
    Q_PROPERTY(QVariantList laneColors READ laneColors CONSTANT)
    Q_PROPERTY(QString iconSource READ iconSource NOTIFY changed)

public:
    explicit QmlTheme(ThemeManager* manager, QObject* parent = nullptr);

    bool dark() const;
    QColor surfaceBase() const;
    QColor surfaceRaised() const;
    QColor surfaceOverlay() const;
    QColor border() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor textMuted() const;
    QColor accent() const;
    QColor accentHover() const;
    QColor head() const;
    QColor stateAdded() const;
    QColor stateModified() const;
    QColor stateDeleted() const;
    QColor stateUntracked() const;
    QColor stateConflict() const;
    QVariantList laneColors() const;
    QString iconSource() const;

signals:
    void changed();

private:
    Theme theme() const;
    ThemeManager* m_manager;
};

} // namespace gittide::ui
```

- [ ] **Step 5: Write the implementation**

Create `ui/src/qmltheme.cpp`:

```cpp
#include "gittide/ui/qmltheme.hpp"

namespace gittide::ui {

QmlTheme::QmlTheme(ThemeManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
{
    connect(m_manager, &ThemeManager::themeChanged, this, &QmlTheme::changed);
}

Theme QmlTheme::theme() const
{
    return m_manager->currentTheme();
}

bool QmlTheme::dark() const
{
    return theme().dark;
}

QColor QmlTheme::surfaceBase() const
{
    return QColor(theme().surfaceBase);
}
QColor QmlTheme::surfaceRaised() const
{
    return QColor(theme().surfaceRaised);
}
QColor QmlTheme::surfaceOverlay() const
{
    return QColor(theme().surfaceOverlay);
}
QColor QmlTheme::border() const
{
    return QColor(theme().border);
}
QColor QmlTheme::textPrimary() const
{
    return QColor(theme().textPrimary);
}
QColor QmlTheme::textSecondary() const
{
    return QColor(theme().textSecondary);
}
QColor QmlTheme::textMuted() const
{
    return QColor(theme().textMuted);
}
QColor QmlTheme::accent() const
{
    return QColor(theme().accent);
}
QColor QmlTheme::accentHover() const
{
    return QColor(theme().accentHover);
}
QColor QmlTheme::head() const
{
    return QColor(theme().head);
}
QColor QmlTheme::stateAdded() const
{
    return QColor(theme().stateAdded);
}
QColor QmlTheme::stateModified() const
{
    return QColor(theme().stateModified);
}
QColor QmlTheme::stateDeleted() const
{
    return QColor(theme().stateDeleted);
}
QColor QmlTheme::stateUntracked() const
{
    return QColor(theme().stateUntracked);
}
QColor QmlTheme::stateConflict() const
{
    return QColor(theme().stateConflict);
}

QVariantList QmlTheme::laneColors() const
{
    // Graph lane palette — the one documented exception to single-accent.
    return {QColor("#22D3EE"), QColor("#A371F7"), QColor("#3FB950"), QColor("#D29922"), QColor("#F778BA")};
}

QString QmlTheme::iconSource() const
{
    // ThemeManager returns a ":/…" resource path; QML wants the "qrc:/…" form.
    return QStringLiteral("qrc") + m_manager->iconResource();
}

} // namespace gittide::ui
```

- [ ] **Step 6: Add the sources to `gittide_ui`**

In `ui/CMakeLists.txt`, add inside the `add_library(gittide_ui STATIC …)` list (e.g. after the `thememanager` entries):

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qmltheme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/qmltheme.hpp
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `TestQmlTheme` cases green.

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/qmltheme.hpp ui/src/qmltheme.cpp ui/CMakeLists.txt \
        tests/ui/test_qml_theme.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): QmlTheme — design tokens as QML-bindable properties"
```

---

### Task 2: Enable Qt Quick + minimal `Main.qml` that loads

**Files:**
- Create: `ui/qml/Main.qml`, `ui/qml/qml.qrc`
- Create: `ui/include/gittide/ui/qmlcontext.hpp`, `ui/src/qmlcontext.cpp`
- Test: `tests/ui/test_qml_shell.cpp`
- Modify: `cmake/Dependencies.cmake`, `ui/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `QmlTheme` (Task 1), `RepoListModel`, `ProjectController`.
- Produces: free function `void gittide::ui::installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController)` setting context properties `theme`, `repoModel`, `projectController`. QML resource `qrc:/qml/Main.qml` with a root `ApplicationWindow { objectName: "appWindow" … }`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_qml_shell.cpp`:

```cpp
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

class TestQmlShell : public QObject
{
    Q_OBJECT
private slots:
    void main_qml_loads_without_errors()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));

        QCOMPARE(engine.rootObjects().size(), 1);
        QCOMPARE(engine.rootObjects().first()->objectName(), QStringLiteral("appWindow"));
    }
};

#include "test_qml_shell.moc"
```

- [ ] **Step 2: Wire the test into the runner (both edits)**

`tests/CMakeLists.txt`: add to `gittide_ui_test_sources`:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_shell.cpp
```

and extend the `gittide_ui_tests` link line (currently `tests/CMakeLists.txt:69-70`) to add Qml/Quick:

```cmake
  target_link_libraries(gittide_ui_tests PRIVATE
    gittide_ui Qt6::Widgets Qt6::Test Qt6::Concurrent Qt6::Qml Qt6::Quick QCoro6::Core libgit2package)
```

`tests/ui/main.cpp`: at the very top of `main()` add the resource init, then include + exec:

```cpp
    Q_INIT_RESOURCE(qml);
```
```cpp
#include "test_qml_shell.cpp"
```
```cpp
    { TestQmlShell t; status |= QTest::qExec(&t, argc, argv); }
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel`
Expected: FAIL — `gittide/ui/qmlcontext.hpp` not found / `qrc:/qml/Main.qml` missing.

- [ ] **Step 4: Add Qt Quick to the dependency find**

In `cmake/Dependencies.cmake`, change line 47:

```cmake
  find_package(Qt6 REQUIRED COMPONENTS Widgets Test Concurrent Svg Qml Quick QuickControls2 QuickTest)
```

- [ ] **Step 5: Create the QML files**

Create `ui/qml/Main.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "appWindow"
    visible: true
    width: 1100
    height: 720
    title: "GitTide"
    color: theme.surfaceBase

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 272
        }

        // Placeholder main pane — branch bar / tabs / diff arrive in later plans.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme.surfaceBase
            Label {
                anchors.centerIn: parent
                text: "Select a repository"
                color: theme.textMuted
                font.pixelSize: 13
            }
        }
    }
}
```

Create `ui/qml/Sidebar.qml` (minimal for this task — the tree arrives in Task 3):

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: sidebar
    objectName: "sidebar"
    color: theme.surfaceRaised

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.margins: 16
            spacing: 10
            Image {
                source: theme.iconSource
                sourceSize.width: 26
                sourceSize.height: 26
            }
            Label {
                text: "GitTide"
                color: theme.textPrimary
                font.pixelSize: 16
                font.weight: Font.Bold
            }
        }

        Item { Layout.fillHeight: true } // tree placeholder (Task 3)
    }
}
```

Create `ui/qml/qml.qrc`:

```xml
<RCC>
  <qresource prefix="/qml">
    <file>Main.qml</file>
    <file>Sidebar.qml</file>
  </qresource>
</RCC>
```

- [ ] **Step 6: Create the context-wiring unit**

Create `ui/include/gittide/ui/qmlcontext.hpp`:

```cpp
#pragma once

class QQmlContext;

namespace gittide::ui {

class QmlTheme;
class RepoListModel;
class ProjectController;

// Single source of the QML context wiring used by both the app entry point and
// the shell test. Sets the context properties Main.qml binds to: `theme`,
// `repoModel`, `projectController`. projectController may be null in tests.
void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController);

} // namespace gittide::ui
```

Create `ui/src/qmlcontext.cpp`:

```cpp
#include "gittide/ui/qmlcontext.hpp"

#include <QQmlContext>

#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"

namespace gittide::ui {

void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController)
{
    ctx->setContextProperty(QStringLiteral("theme"), theme);
    ctx->setContextProperty(QStringLiteral("repoModel"), repoModel);
    ctx->setContextProperty(QStringLiteral("projectController"), projectController);
}

} // namespace gittide::ui
```

- [ ] **Step 7: Add sources + qrc + links to `gittide_ui`**

In `ui/CMakeLists.txt`, add to the library source list:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/qmlcontext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/qmlcontext.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/qml/qml.qrc
```

and extend the link line (currently `ui/CMakeLists.txt:48-49`):

```cmake
target_link_libraries(gittide_ui PUBLIC
  gittide_core Qt6::Widgets Qt6::Concurrent Qt6::Svg Qt6::Qml Qt6::Quick Qt6::QuickControls2 QCoro6::Core)
```

(`AUTORCC` is already ON for `gittide_ui`, so `qml.qrc` compiles in. Resource base name is `qml` → `Q_INIT_RESOURCE(qml)`.)

- [ ] **Step 8: Run to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `TestQmlShell::main_qml_loads_without_errors` green (one root object named `appWindow`, zero QML errors).

- [ ] **Step 9: Commit**

```bash
git add cmake/Dependencies.cmake ui/CMakeLists.txt ui/qml/ \
        ui/include/gittide/ui/qmlcontext.hpp ui/src/qmlcontext.cpp \
        tests/ui/test_qml_shell.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): load a minimal QML ApplicationWindow shell"
```

---

### Task 3: Render the repo tree (with submodules) in the sidebar

**Files:**
- Modify: `ui/qml/Sidebar.qml`
- Test: `tests/ui/test_qml_shell.cpp` (add a case)

**Interfaces:**
- Consumes: `RepoListModel` context property `repoModel` (tree model; roles via `roleNames()` — `path`, `missing`, plus `Qt::DisplayName`/`display`). `TreeView` from `QtQuick`, `TreeViewDelegate` from `QtQuick.Controls`.
- Produces: a `TreeView { objectName: "repoTree"; model: repoModel }` inside `Sidebar.qml`.

> **Role names note:** `RepoListModel::roleNames()` defines the QML-visible role keys. Before writing the delegate, open `ui/src/repolistmodel.cpp` and read the exact `roleNames()` map (the `display` text role plus `path`/`missing` from the `Roles` enum: `PathRole`, `MissingRole`). Use those exact keys in the delegate bindings. If `roleNames()` does not expose a display role, bind the delegate label to `model.path` (always present) — do not invent a role name.

- [ ] **Step 1: Write the failing test (add a case to `TestQmlShell`)**

Add this slot to `tests/ui/test_qml_shell.cpp`'s `TestQmlShell`:

```cpp
    void repo_tree_is_bound_to_the_model()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        RepoListModel repoModel;
        std::vector<gittide::RepoRef> repos;
        gittide::RepoRef r;
        r.alias = "gittide";
        r.path  = "/tmp/gittide";
        repos.push_back(r);
        repoModel.setRepos(repos);

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* tree = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("repoTree"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->property("model").value<QAbstractItemModel*>(), &repoModel);
    }
```

Add the include needed for the field names at the top of the file:

```cpp
#include "gittide/projectstore.hpp"   // gittide::RepoRef
#include <QAbstractItemModel>
```

> Before writing this, confirm `gittide::RepoRef`'s exact field names by reading `core/include/gittide/projectstore.hpp` (the test uses `alias` and `path`). Adjust the two assignments to the real field names if they differ.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — no child named `repoTree`.

- [ ] **Step 3: Add the TreeView to `Sidebar.qml`**

Replace the `Item { Layout.fillHeight: true }` placeholder in `ui/qml/Sidebar.qml` with:

```qml
        TreeView {
            id: repoTree
            objectName: "repoTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            model: repoModel

            delegate: TreeViewDelegate {
                id: row
                implicitHeight: 34
                indentation: 16

                contentItem: RowLayout {
                    spacing: 8
                    Label {
                        text: model.path ? model.path.toString().split("/").pop() : ""
                        color: model.missing ? theme.textMuted : theme.textPrimary
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: model.missing === true
                        text: "⚠" // warning sign
                        color: theme.stateModified
                    }
                }

                background: Rectangle {
                    color: row.current ? theme.surfaceBase : "transparent"
                    radius: 10
                    Rectangle { // accent left border on the selected row
                        visible: row.current
                        width: 2
                        height: parent.height
                        color: theme.accent
                    }
                }
            }
        }
```

Add an "Add repository" button below the tree (before the closing `ColumnLayout`):

```qml
        Button {
            objectName: "addRepoButton"
            Layout.fillWidth: true
            Layout.margins: 8
            text: "Add repository"
            flat: true
            contentItem: Label {
                text: parent.text
                color: theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                radius: 10
                color: "transparent"
                border.color: theme.border
                border.width: 1
            }
        }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `repo_tree_is_bound_to_the_model` green; existing cases stay green.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/Sidebar.qml tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): render the repo tree with submodules in the QML sidebar"
```

---

### Task 4: Runnable `gittide_qml_app` with real data

**Files:**
- Create: `app/qml_main.cpp`
- Modify: `CMakeLists.txt`, `app/CMakeLists.txt`

**Interfaces:**
- Consumes: `LibGit2Context`, `ProjectStore`, `ThemeManager`, `ProjectController`, `QmlTheme`, `installQmlContext`, `QQmlApplicationEngine`.
- Produces: executable `gittide_qml_app` (built only when `GITGUI_BUILD_QML=ON`).

- [ ] **Step 1: Add the build option and target**

In `CMakeLists.txt`, after line 23 (`option(GITGUI_BUILD_UI …)`):

```cmake
option(GITGUI_BUILD_QML "Build the experimental QML app shell" ON)
```

In `app/CMakeLists.txt`, append:

```cmake
if(GITGUI_BUILD_QML)
  add_executable(gittide_qml_app ${CMAKE_CURRENT_SOURCE_DIR}/qml_main.cpp)
  set_target_properties(gittide_qml_app PROPERTIES AUTOMOC ON)
  target_link_libraries(gittide_qml_app PRIVATE gittide_ui Qt6::Gui Qt6::Qml Qt6::Quick)
endif()
```

- [ ] **Step 2: Write the entry point**

Create `app/qml_main.cpp`:

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QStandardPaths>

#include "gittide/libgit2context.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

int main(int argc, char** argv)
{
    Q_INIT_RESOURCE(icons);
    Q_INIT_RESOURCE(qml);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("gittide"));
    QGuiApplication::setOrganizationName(QStringLiteral("gittide"));

    const gittide::LibGit2Context git_ctx;

    ThemeManager theme;
    theme.setMode(ThemeManager::Mode::System);

    // Load the project registry into a local store (single-window shell — multi
    // window/session restore is a later plan).
    gittide::ProjectStore store;
    const QString configDir    = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString projectsFile = QDir(configDir).filePath(QStringLiteral("projects.json"));
    if (auto loaded = gittide::ProjectStore::load(std::filesystem::path(projectsFile.toStdString())))
    {
        store = std::move(*loaded);
    }

    ProjectController controller(&store, std::filesystem::path(projectsFile.toStdString()));
    const QString active = QString::fromStdString(store.activeProject());
    if (!active.isEmpty())
        controller.activate(active);
    else if (!store.projects().empty())
        controller.activate(QString::fromStdString(store.projects().front().id));

    QmlTheme qmlTheme(&theme);

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &qmlTheme, controller.repos(), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
```

> Before finalizing, confirm `gittide::Project`'s id field name (`store.projects().front().id`) against `core/include/gittide/projectstore.hpp` — the QWidgets `main.cpp` uses `.id`, so this matches; adjust only if the header says otherwise.

- [ ] **Step 3: Configure and build the app**

Run: `cmake -S . -B build && cmake --build build --target gittide_qml_app --parallel`
Expected: builds; binary at `build/app/gittide_qml_app`.

- [ ] **Step 4: Run it and verify the shell renders**

Run: `./build/app/gittide_qml_app`
Expected: a dark window titled "GitTide" with the branded sidebar; if a `projects.json` with repos exists, the repo tree lists them (submodules nested where the model provides them); selected row shows the accent left border. Close the window to exit.

(If running headless/CI, instead verify load only: `QT_QPA_PLATFORM=offscreen ./build/app/gittide_qml_app` returns 0 only when a window event loop is available — for CI prefer the `TestQmlShell` coverage from Task 2/3 rather than launching the app.)

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt app/CMakeLists.txt app/qml_main.cpp
git commit -m "feat(app): runnable gittide_qml_app shell over real project data"
```

---

## Self-Review

**Spec coverage (this plan's slice — the foundation + sidebar):**
- QML hosting + theme-token bridge → Tasks 1–2. ✔
- Sidebar with repo tree + submodule nesting (model-provided depth) + Add-repository button (plain label, no `+`/chevron) → Task 3. ✔
- Token system reused verbatim; lane palette defined once → Task 1. ✔
- Parallel target, nothing deleted, runnable proof for the QML-vs-Electron decision gate → Task 4. ✔
- Deferred items (recursive submodule depth, branch bar, tabs, diff, History/graph, overlays, empty states, light-theme polish, multi-window/session, QWidgets deletion) are **named** in the File Structure "deferred" block — no silent caps. ✔

**Placeholder scan:** No "TBD"/"add error handling"/"similar to" — every code step shows full code. Two model-field confirmations (`RepoListModel::roleNames()` keys; `RepoRef`/`Project` field names) are called out as explicit reads against named headers, not guesses.

**Type consistency:** `installQmlContext(QQmlContext*, QmlTheme*, RepoListModel*, ProjectController*)` is used identically in Task 2 (decl), Task 2 test, Task 3 test, and Task 4 app. `QmlTheme(ThemeManager*, QObject*)` constructor consistent across Tasks 1, 2, 4. Context property names `theme` / `repoModel` / `projectController` match between `installQmlContext`, `Main.qml`, `Sidebar.qml`, and tests. Resource base name `qml` matches `Q_INIT_RESOURCE(qml)` in both the test runner and the app.
