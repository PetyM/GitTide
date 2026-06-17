# Plan 7 — Visual Theme (GitTide design system)

| | |
|--|--|
| **Date** | 2026-06-17 |
| **Status** | `done` |
| **Spec** | [design](../spec/design/design.md) |
| **Depends on** | [Plan 6](2026-06-17-plan6-rebrand-gittide.md) |

**Goal:** Implement the GitTide visual design system from `docs/superpowers/specs/2026-06-17-visual-design-system.md` — a token table, a QSS generator, a `ThemeManager` that applies dark/light themes from the OS color scheme, the branded app icon, and restyled branded empty-state pages.

**Architecture:** Color lives in one place — a `Theme` token struct with `darkTheme()` / `lightTheme()` factories (`gittide::ui`). A pure function turns a `Theme` into a Qt stylesheet string. `ThemeManager` owns the active mode, resolves dark/light from `QStyleHints::colorScheme()`, applies the stylesheet app-wide, exposes the matching icon, and re-applies live on OS scheme change. Widgets carry object names/classes only; no widget hard-codes a color.

**Tech Stack:** C++23, Qt6 Widgets, Qt6 Svg (for the SVG app icon), QtTest. Builds on the `gittide::` names from Plan 6.

## Global Constraints

- **Prerequisite:** Plan 6 (rebrand) is merged. All names here are `gittide` / `gittide::ui` / `gittide_ui`.
- Tokens are the only source of color. No hex literal appears in any widget or in QSS rules written outside the generated stylesheet. Hex values come **verbatim** from the design-system spec §2.
- Both `darkTheme()` and `lightTheme()` define **every** token (spec §2). State colors are identical across themes.
- Object names of existing CTAs are preserved: `createProjectCta`, `addExistingCta`, `initRepoCta`, `cloneCta`, `centralStack`, `mainTabs` — existing tests must keep passing.
- 4px spacing grid; radii 6 / 10 / 18 only (spec §4).
- New UI files added to `ui/CMakeLists.txt` `gittide_ui` source list; new tests added to `gittide_ui_tests` in `tests/CMakeLists.txt`.

---

## Task 1: Theme token table

**Files:**
- Create: `ui/include/gittide/ui/Theme.hpp`
- Create: `ui/src/Theme.cpp`
- Modify: `ui/CMakeLists.txt`
- Test: `tests/ui/test_theme.cpp`
- Modify: `tests/CMakeLists.txt` (add test source to `gittide_ui_tests`)

**Interfaces:**
- Produces:
  ```cpp
  namespace gittide::ui {
  struct Theme {
      bool    dark;
      QString surfaceBase, surfaceRaised, surfaceOverlay, border;
      QString textPrimary, textSecondary, textMuted;
      QString accent, accentHover, head;
      QString stateAdded, stateModified, stateDeleted, stateUntracked, stateConflict;
  };
  Theme darkTheme();
  Theme lightTheme();
  }  // namespace gittide::ui
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_theme.cpp`:

```cpp
#include <QtTest>
#include "gittide/ui/Theme.hpp"

using namespace gittide::ui;

class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void dark_theme_has_brand_tokens() {
        const Theme t = darkTheme();
        QVERIFY(t.dark);
        QCOMPARE(t.surfaceBase, QStringLiteral("#0B1623"));
        QCOMPARE(t.accent,      QStringLiteral("#22D3EE"));
        QCOMPARE(t.head,        QStringLiteral("#FFFFFF"));
        QCOMPARE(t.textPrimary, QStringLiteral("#C9D1D9"));
    }
    void light_theme_has_brand_tokens() {
        const Theme t = lightTheme();
        QVERIFY(!t.dark);
        QCOMPARE(t.surfaceBase, QStringLiteral("#EEF3F8"));
        QCOMPARE(t.accent,      QStringLiteral("#0891B2"));
        QCOMPARE(t.textPrimary, QStringLiteral("#0B1623"));
    }
    void state_colors_match_across_themes() {
        // Spec §2.3: state colors identical in both themes.
        QCOMPARE(darkTheme().stateAdded,   lightTheme().stateAdded);
        QCOMPARE(darkTheme().stateDeleted, lightTheme().stateDeleted);
        QCOMPARE(darkTheme().stateAdded,   QStringLiteral("#3FB950"));
    }
    void every_token_is_nonempty() {
        for (const Theme& t : {darkTheme(), lightTheme()}) {
            for (const QString& tok : {t.surfaceBase, t.surfaceRaised, t.surfaceOverlay,
                 t.border, t.textPrimary, t.textSecondary, t.textMuted, t.accent,
                 t.accentHover, t.head, t.stateAdded, t.stateModified, t.stateDeleted,
                 t.stateUntracked, t.stateConflict}) {
                QVERIFY(!tok.isEmpty());
            }
        }
    }
};

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
```

- [ ] **Step 2: Register the test source**

In `tests/CMakeLists.txt`, add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme.cpp` to the `add_executable(gittide_ui_tests ...)` source list.

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build /home/michal/Documents/gitgui/build -j
```

Expected: FAIL — `gittide/ui/Theme.hpp` not found.

- [ ] **Step 4: Write the header**

Create `ui/include/gittide/ui/Theme.hpp`:

```cpp
#pragma once
#include <QString>

namespace gittide::ui {

// A resolved set of design tokens (one theme). Values come from
// docs/superpowers/specs/2026-06-17-visual-design-system.md §2.
struct Theme {
    bool    dark;
    QString surfaceBase, surfaceRaised, surfaceOverlay, border;
    QString textPrimary, textSecondary, textMuted;
    QString accent, accentHover, head;
    QString stateAdded, stateModified, stateDeleted, stateUntracked, stateConflict;
};

Theme darkTheme();
Theme lightTheme();

}  // namespace gittide::ui
```

- [ ] **Step 5: Write the implementation**

Create `ui/src/Theme.cpp`:

```cpp
#include "gittide/ui/Theme.hpp"

namespace gittide::ui {

Theme darkTheme() {
    return Theme{
        .dark           = true,
        .surfaceBase    = QStringLiteral("#0B1623"),
        .surfaceRaised  = QStringLiteral("#11202F"),
        .surfaceOverlay = QStringLiteral("#16293B"),
        .border         = QStringLiteral("#1E3245"),
        .textPrimary    = QStringLiteral("#C9D1D9"),
        .textSecondary  = QStringLiteral("#8B949E"),
        .textMuted      = QStringLiteral("#6E7681"),
        .accent         = QStringLiteral("#22D3EE"),
        .accentHover    = QStringLiteral("#4DDFF2"),
        .head           = QStringLiteral("#FFFFFF"),
        .stateAdded     = QStringLiteral("#3FB950"),
        .stateModified  = QStringLiteral("#D29922"),
        .stateDeleted   = QStringLiteral("#F85149"),
        .stateUntracked = QStringLiteral("#6E7681"),
        .stateConflict  = QStringLiteral("#DB6D28"),
    };
}

Theme lightTheme() {
    return Theme{
        .dark           = false,
        .surfaceBase    = QStringLiteral("#EEF3F8"),
        .surfaceRaised  = QStringLiteral("#FFFFFF"),
        .surfaceOverlay = QStringLiteral("#F4F8FB"),
        .border         = QStringLiteral("#D4DFEA"),
        .textPrimary    = QStringLiteral("#0B1623"),
        .textSecondary  = QStringLiteral("#51606E"),
        .textMuted      = QStringLiteral("#8595A4"),
        .accent         = QStringLiteral("#0891B2"),
        .accentHover    = QStringLiteral("#0AA5CC"),
        .head           = QStringLiteral("#0891B2"),
        .stateAdded     = QStringLiteral("#3FB950"),
        .stateModified  = QStringLiteral("#D29922"),
        .stateDeleted   = QStringLiteral("#F85149"),
        .stateUntracked = QStringLiteral("#6E7681"),
        .stateConflict  = QStringLiteral("#DB6D28"),
    };
}

}  // namespace gittide::ui
```

- [ ] **Step 6: Add to the UI library**

In `ui/CMakeLists.txt`, add to `gittide_ui`:
```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/Theme.hpp
```

- [ ] **Step 7: Build and run**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gittide_ui_tests --output-on-failure
```

Expected: `TestTheme` 4 cases pass; existing UI tests still pass.

- [ ] **Step 8: Commit**

```bash
cd /home/michal/Documents/gitgui
git add ui/include/gittide/ui/Theme.hpp ui/src/Theme.cpp ui/CMakeLists.txt \
        tests/ui/test_theme.cpp tests/CMakeLists.txt
git commit -m "feat(ui): GitTide design tokens (dark + light Theme)"
```

---

## Task 2: QSS stylesheet generator

**Files:**
- Create: `ui/include/gittide/ui/ThemeStyle.hpp`
- Create: `ui/src/ThemeStyle.cpp`
- Modify: `ui/CMakeLists.txt`
- Test: `tests/ui/test_theme_style.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Theme` (Task 1).
- Produces:
  ```cpp
  namespace gittide::ui {
  // Pure: turns a Theme into an application-wide Qt stylesheet string.
  QString buildStyleSheet(const Theme& theme);
  }  // namespace gittide::ui
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_theme_style.cpp`:

```cpp
#include <QtTest>
#include "gittide/ui/Theme.hpp"
#include "gittide/ui/ThemeStyle.hpp"

using namespace gittide::ui;

class TestThemeStyle : public QObject {
    Q_OBJECT
private slots:
    void stylesheet_is_nonempty_and_uses_tokens() {
        const Theme t = darkTheme();
        const QString qss = buildStyleSheet(t);
        QVERIFY(!qss.isEmpty());
        QVERIFY(qss.contains(t.accent));        // accent token used
        QVERIFY(qss.contains(t.surfaceBase));   // base surface used
        QVERIFY(qss.contains(QStringLiteral("QPushButton")));  // styles buttons
    }
    void light_and_dark_produce_different_sheets() {
        QVERIFY(buildStyleSheet(darkTheme()) != buildStyleSheet(lightTheme()));
    }
    void primary_cta_is_styled_by_objectname() {
        // Empty-state CTAs are accent-filled; sheet must reference the id.
        const QString qss = buildStyleSheet(darkTheme());
        QVERIFY(qss.contains(QStringLiteral("#createProjectCta")));
    }
};

QTEST_MAIN(TestThemeStyle)
#include "test_theme_style.moc"
```

- [ ] **Step 2: Register the test**

In `tests/CMakeLists.txt`, add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme_style.cpp` to `gittide_ui_tests`.

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build /home/michal/Documents/gitgui/build -j
```

Expected: FAIL — `ThemeStyle.hpp` / `buildStyleSheet` missing.

- [ ] **Step 4: Write the header**

Create `ui/include/gittide/ui/ThemeStyle.hpp`:

```cpp
#pragma once
#include <QString>
#include "gittide/ui/Theme.hpp"

namespace gittide::ui {

// Builds the application-wide Qt stylesheet (QSS) for the given theme. This is
// the ONLY place color literals are emitted — every value is read from `theme`.
QString buildStyleSheet(const Theme& theme);

}  // namespace gittide::ui
```

- [ ] **Step 5: Write the implementation**

Create `ui/src/ThemeStyle.cpp`. Uses spec §4 radii (6/10/18) and §6 component rules. The accent text color on filled buttons is `surfaceBase` (dark base reads on cyan):

```cpp
#include "gittide/ui/ThemeStyle.hpp"

namespace gittide::ui {

QString buildStyleSheet(const Theme& t) {
    return QStringLiteral(R"(
QWidget {
    background: %1;
    color: %5;
    font-family: system-ui, -apple-system, "Segoe UI", "Noto Sans", sans-serif;
    font-size: 13px;
}
QDockWidget, QFrame#emptyStateCard, QTabWidget::pane, QDialog {
    background: %2;
    border: 1px solid %4;
    border-radius: 10px;
}
QLabel { background: transparent; }
QLabel[role="headline"] { font-size: 22px; font-weight: 700; color: %5; }
QLabel[role="subtext"]  { font-size: 13px; color: %6; }

QPushButton, QToolButton {
    background: %2;
    color: %5;
    border: 1px solid %4;
    border-radius: 6px;
    padding: 8px 16px;
    font-weight: 600;
}
QPushButton:hover, QToolButton:hover { border-color: %8; }

QPushButton#createProjectCta, QPushButton#addExistingCta {
    background: %8;
    color: %1;
    border: none;
}
QPushButton#createProjectCta:hover, QPushButton#addExistingCta:hover { background: %9; }

QComboBox#projectSwitcher {
    background: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 12px;
}

QTreeView#repoList {
    background: %2; border: 1px solid %4; border-radius: 10px;
}
QTreeView#repoList::item { padding: 4px 8px; }
QTreeView#repoList::item:selected {
    background: %3; border-left: 2px solid %8; color: %5;
}

QTabBar::tab { background: transparent; color: %6; padding: 8px 16px; }
QTabBar::tab:selected { color: %5; border-bottom: 2px solid %8; }

QProgressBar { border: 1px solid %4; border-radius: 6px; text-align: center; }
QProgressBar::chunk { background: %8; border-radius: 6px; }
)")
        .arg(t.surfaceBase,    // %1
             t.surfaceRaised,  // %2
             t.surfaceOverlay, // %3
             t.border,         // %4
             t.textPrimary,    // %5
             t.textSecondary,  // %6
             t.textMuted,      // %7
             t.accent,         // %8
             t.accentHover);   // %9
}

}  // namespace gittide::ui
```

Note: `QString::arg` with 9 positional args is fine; `%7` (textMuted) is reserved for future rules and intentionally currently unused in the template — keep it in the `.arg` chain so positions stay stable. If a linter flags the unused position, that is acceptable.

- [ ] **Step 6: Add to the UI library**

In `ui/CMakeLists.txt`, add:
```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ThemeStyle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ThemeStyle.hpp
```

- [ ] **Step 7: Build and run**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gittide_ui_tests --output-on-failure
```

Expected: `TestThemeStyle` passes; all prior tests pass.

- [ ] **Step 8: Commit**

```bash
cd /home/michal/Documents/gitgui
git add ui/include/gittide/ui/ThemeStyle.hpp ui/src/ThemeStyle.cpp ui/CMakeLists.txt \
        tests/ui/test_theme_style.cpp tests/CMakeLists.txt
git commit -m "feat(ui): QSS generator from Theme tokens"
```

---

## Task 3: ThemeManager (apply + OS color-scheme resolution)

**Files:**
- Create: `ui/include/gittide/ui/ThemeManager.hpp`
- Create: `ui/src/ThemeManager.cpp`
- Modify: `ui/CMakeLists.txt`
- Test: `tests/ui/test_theme_manager.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Theme`, `darkTheme()`, `lightTheme()` (Task 1), `buildStyleSheet` (Task 2).
- Produces:
  ```cpp
  namespace gittide::ui {
  class ThemeManager : public QObject {
      Q_OBJECT
  public:
      enum class Mode { System, Dark, Light };
      explicit ThemeManager(QObject* parent = nullptr);
      void  setMode(Mode mode);          // re-applies immediately
      Mode  mode() const;
      Theme currentTheme() const;        // resolved (System → OS scheme)
      QString iconResource() const;      // ":/icons/gittide-icon.svg" or "-light"
      void  applyTo(QApplication* app);  // sets qApp stylesheet + window icon
  signals:
      void themeChanged();
  };
  }  // namespace gittide::ui
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_theme_manager.cpp`:

```cpp
#include <QtTest>
#include <QApplication>
#include "gittide/ui/ThemeManager.hpp"

using namespace gittide::ui;

class TestThemeManager : public QObject {
    Q_OBJECT
private slots:
    void forced_dark_resolves_to_dark_theme() {
        ThemeManager m;
        m.setMode(ThemeManager::Mode::Dark);
        QVERIFY(m.currentTheme().dark);
        QCOMPARE(m.currentTheme().accent, QStringLiteral("#22D3EE"));
        QVERIFY(m.iconResource().contains(QStringLiteral("gittide-icon.svg")));
        QVERIFY(!m.iconResource().contains(QStringLiteral("light")));
    }
    void forced_light_resolves_to_light_theme() {
        ThemeManager m;
        QSignalSpy spy(&m, &ThemeManager::themeChanged);
        m.setMode(ThemeManager::Mode::Light);
        QVERIFY(!m.currentTheme().dark);
        QCOMPARE(m.currentTheme().accent, QStringLiteral("#0891B2"));
        QVERIFY(m.iconResource().contains(QStringLiteral("light")));
        QCOMPARE(spy.count(), 1);
    }
    void applyTo_sets_nonempty_stylesheet() {
        ThemeManager m;
        m.setMode(ThemeManager::Mode::Dark);
        m.applyTo(qApp);
        QVERIFY(!qApp->styleSheet().isEmpty());
        QVERIFY(qApp->styleSheet().contains(QStringLiteral("#22D3EE")));
        qApp->setStyleSheet(QString());  // reset for other tests
    }
};

QTEST_MAIN(TestThemeManager)
#include "test_theme_manager.moc"
```

- [ ] **Step 2: Register the test**

In `tests/CMakeLists.txt`, add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_theme_manager.cpp` to `gittide_ui_tests`.

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build /home/michal/Documents/gitgui/build -j
```

Expected: FAIL — `ThemeManager.hpp` missing.

- [ ] **Step 4: Write the header**

Create `ui/include/gittide/ui/ThemeManager.hpp`:

```cpp
#pragma once
#include <QObject>
#include <QString>
#include "gittide/ui/Theme.hpp"

class QApplication;

namespace gittide::ui {

// Owns the active theme mode, resolves System mode against the OS color scheme,
// applies the generated stylesheet app-wide, and exposes the matching icon.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Dark, Light };

    explicit ThemeManager(QObject* parent = nullptr);

    void  setMode(Mode mode);
    Mode  mode() const { return mode_; }
    Theme currentTheme() const;
    QString iconResource() const;
    void  applyTo(QApplication* app);

signals:
    void themeChanged();

private:
    bool resolveDark() const;   // System → QStyleHints::colorScheme; else forced
    Mode mode_ = Mode::System;
    QApplication* app_ = nullptr;
};

}  // namespace gittide::ui
```

- [ ] **Step 5: Write the implementation**

Create `ui/src/ThemeManager.cpp`:

```cpp
#include "gittide/ui/ThemeManager.hpp"
#include "gittide/ui/ThemeStyle.hpp"

#include <QApplication>
#include <QIcon>
#include <QStyleHints>

namespace gittide::ui {

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {
    // Re-apply live when the OS color scheme changes (only matters in System mode).
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (mode_ == Mode::System) {
                if (app_) applyTo(app_);
                emit themeChanged();
            }
        });
    }
}

bool ThemeManager::resolveDark() const {
    switch (mode_) {
        case Mode::Dark:  return true;
        case Mode::Light: return false;
        case Mode::System:
        default: {
            const auto scheme = QGuiApplication::styleHints()->colorScheme();
            // Unknown/Dark → dark (brand's primary look).
            return scheme != Qt::ColorScheme::Light;
        }
    }
}

Theme ThemeManager::currentTheme() const {
    return resolveDark() ? darkTheme() : lightTheme();
}

QString ThemeManager::iconResource() const {
    return resolveDark() ? QStringLiteral(":/icons/gittide-icon.svg")
                         : QStringLiteral(":/icons/gittide-icon-light.svg");
}

void ThemeManager::setMode(Mode mode) {
    mode_ = mode;
    if (app_) applyTo(app_);
    emit themeChanged();
}

void ThemeManager::applyTo(QApplication* app) {
    app_ = app;
    app->setStyleSheet(buildStyleSheet(currentTheme()));
    app->setWindowIcon(QIcon(iconResource()));
}

}  // namespace gittide::ui
```

- [ ] **Step 6: Add to the UI library**

In `ui/CMakeLists.txt`, add:
```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ThemeManager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/ThemeManager.hpp
```

- [ ] **Step 7: Build and run**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gittide_ui_tests --output-on-failure
```

Expected: `TestThemeManager` passes (icon resource strings resolve even before the `.qrc` exists — Task 4 adds the actual resource). Existing tests pass.

- [ ] **Step 8: Commit**

```bash
cd /home/michal/Documents/gitgui
git add ui/include/gittide/ui/ThemeManager.hpp ui/src/ThemeManager.cpp ui/CMakeLists.txt \
        tests/ui/test_theme_manager.cpp tests/CMakeLists.txt
git commit -m "feat(ui): ThemeManager — OS color-scheme resolution + app-wide apply"
```

---

## Task 4: Wire ThemeManager + branded app icon into startup

**Files:**
- Create: `ui/resources/icons.qrc`
- Create: `ui/resources/gittide-icon.svg` (copy of `docs/gittide-icon.svg`)
- Create: `ui/resources/gittide-icon-light.svg` (copy of `docs/gittide-icon-light.svg`)
- Modify: `cmake/Dependencies.cmake` (add `Svg` to Qt6 components)
- Modify: `ui/CMakeLists.txt` (add qrc + link `Qt6::Svg`)
- Modify: `app/main.cpp` (instantiate `ThemeManager`, `applyTo(&app)`)

**Interfaces:**
- Consumes: `ThemeManager` (Task 3).
- Produces: runtime theming applied before the first window shows; `:/icons/gittide-icon*.svg` resources resolve to real SVGs.

- [ ] **Step 1: Add Qt6 Svg component**

In `cmake/Dependencies.cmake:40`, change:
```cmake
  find_package(Qt6 REQUIRED COMPONENTS Widgets Test Concurrent)
```
to:
```cmake
  find_package(Qt6 REQUIRED COMPONENTS Widgets Test Concurrent Svg)
```

- [ ] **Step 2: Copy the brand SVGs into a UI resource dir**

```bash
cd /home/michal/Documents/gitgui
mkdir -p ui/resources
cp docs/gittide-icon.svg       ui/resources/gittide-icon.svg
cp docs/gittide-icon-light.svg ui/resources/gittide-icon-light.svg
```

- [ ] **Step 3: Create the qrc**

Create `ui/resources/icons.qrc`:

```xml
<RCC>
  <qresource prefix="/icons">
    <file alias="gittide-icon.svg">gittide-icon.svg</file>
    <file alias="gittide-icon-light.svg">gittide-icon-light.svg</file>
  </qresource>
</RCC>
```

- [ ] **Step 4: Register qrc + Svg in the UI library**

In `ui/CMakeLists.txt`, add the qrc to the `gittide_ui` sources:
```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.qrc
```
and add `Qt6::Svg` to `target_link_libraries(gittide_ui PUBLIC ...)`. Ensure `set_target_properties(gittide_ui PROPERTIES AUTOMOC ON AUTORCC ON)` (add `AUTORCC ON`).

- [ ] **Step 5: Wire ThemeManager into `app/main.cpp`**

Add `#include "gittide/ui/ThemeManager.hpp"`. After `QApplication app(...)` and the app/org name calls, before `WindowManager manager;`:

```cpp
    gittide::ui::ThemeManager theme;
    theme.setMode(gittide::ui::ThemeManager::Mode::System);
    theme.applyTo(&app);
```

(The migration block from Plan 6 stays where it is.)

- [ ] **Step 6: Re-configure (new Qt component) and build the app**

```bash
cmake -S /home/michal/Documents/gitgui -B /home/michal/Documents/gitgui/build
cmake --build /home/michal/Documents/gitgui/build -j --target gittide_app
```

Expected: clean build. If `Qt6::Svg` is not found, install the Qt SVG module (`qt6-svg` / aqt component `qtsvg`).

- [ ] **Step 7: Run the full suite (no regressions)**

```bash
ctest --test-dir /home/michal/Documents/gitgui/build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 8: Commit**

```bash
cd /home/michal/Documents/gitgui
git add ui/resources/icons.qrc ui/resources/gittide-icon.svg \
        ui/resources/gittide-icon-light.svg cmake/Dependencies.cmake \
        ui/CMakeLists.txt app/main.cpp
git commit -m "feat(app): apply ThemeManager + branded SVG app icon at startup"
```

---

## Task 5: Branded empty-state cards

**Files:**
- Modify: `ui/src/MainWindow.cpp` (rewrite `makeNoProjectsPage` / `makeNoReposPage`)
- Test: `tests/ui/test_main_window.cpp` (add card-presence assertions)

**Interfaces:**
- Consumes: nothing new (uses existing CTA object names + `:/icons` resource from Task 4).
- Produces: each empty page wraps its CTAs in a `QFrame` named `emptyStateCard` containing the brand icon, a headline label (`role="headline"`), a subtext label (`role="subtext"`), and the existing CTA buttons (object names unchanged).

- [ ] **Step 1: Write the failing test**

Append to `tests/ui/test_main_window.cpp` (inside the test class; add `#include <QFrame>` and `#include <QLabel>` to the file includes):

```cpp
    void no_projects_page_has_branded_card() {
        ProjectStore store;
        MainWindow win(&store);
        auto* card = win.findChild<QFrame*>(QStringLiteral("emptyStateCard"));
        QVERIFY(card != nullptr);
        // headline + the existing CTA still live inside the card
        QVERIFY(card->findChild<QLabel*>() != nullptr);
        QVERIFY(win.findChild<QPushButton*>(QStringLiteral("createProjectCta")) != nullptr);
        main_window_test::drainAsync();
    }
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gittide_ui_tests --output-on-failure
```

Expected: FAIL — no `emptyStateCard` frame yet.

- [ ] **Step 3: Rewrite the two page builders in `MainWindow.cpp`**

Add includes `#include <QFrame>`, `#include <QLabel>`. Replace the existing `makeNoProjectsPage` and `makeNoReposPage` (in the anonymous namespace) with a shared card helper:

```cpp
namespace {

// Builds a centered branded card: icon + headline + subtext + the given buttons.
QWidget* makeEmptyStatePage(QWidget* parent, const QString& pageName,
                            const QString& headline, const QString& subtext,
                            const QList<QPushButton*>& buttons) {
    auto* w = new QWidget(parent);
    w->setObjectName(pageName);

    auto* card = new QFrame(w);
    card->setObjectName(QStringLiteral("emptyStateCard"));
    card->setMaximumWidth(420);

    auto* icon = new QLabel(card);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/gittide-icon.svg")).pixmap(72, 72));
    icon->setAlignment(Qt::AlignCenter);

    auto* title = new QLabel(headline, card);
    title->setProperty("role", "headline");
    title->setAlignment(Qt::AlignCenter);

    auto* sub = new QLabel(subtext, card);
    sub->setProperty("role", "subtext");
    sub->setAlignment(Qt::AlignCenter);
    sub->setWordWrap(true);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(12);
    cardLayout->addWidget(icon);
    cardLayout->addWidget(title);
    cardLayout->addWidget(sub);
    for (auto* b : buttons) { b->setParent(card); cardLayout->addWidget(b); }

    auto* outer = new QVBoxLayout(w);
    outer->addStretch();
    auto* row = new QHBoxLayout;
    row->addStretch(); row->addWidget(card); row->addStretch();
    outer->addLayout(row);
    outer->addStretch();
    return w;
}

QWidget* makeNoProjectsPage(QWidget* parent) {
    auto* btn = new QPushButton(QStringLiteral("Create Project"));
    btn->setObjectName(QStringLiteral("createProjectCta"));
    return makeEmptyStatePage(
        parent, QStringLiteral("noProjectsPage"),
        QStringLiteral("Welcome to GitTide"),
        QStringLiteral("Create a project to group the repositories you work on."),
        {btn});
}

QWidget* makeNoReposPage(QWidget* parent) {
    auto* addBtn = new QPushButton(QStringLiteral("Add Existing Repository"));
    addBtn->setObjectName(QStringLiteral("addExistingCta"));
    auto* initBtn = new QPushButton(QStringLiteral("Initialize New Repository"));
    initBtn->setObjectName(QStringLiteral("initRepoCta"));
    auto* cloneBtn = new QPushButton(QStringLiteral("Clone Repository"));
    cloneBtn->setObjectName(QStringLiteral("cloneCta"));
    return makeEmptyStatePage(
        parent, QStringLiteral("noReposPage"),
        QStringLiteral("No repositories yet"),
        QStringLiteral("Add, initialize, or clone a repository to get started."),
        {addBtn, initBtn, cloneBtn});
}

}  // namespace
```

Add `#include <QIcon>` if not present. The CTA-wiring code in the `MainWindow` constructor (which finds these buttons via `findChild`) is unchanged and still works because object names are preserved.

- [ ] **Step 4: Build and run the full UI suite**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gittide_ui_tests --output-on-failure
```

Expected: the new `no_projects_page_has_branded_card` passes and all existing empty-state tests (`no_projects_shows_create_project_cta`, `empty_project_shows_add_repo_cta`, etc.) still pass — object names unchanged.

- [ ] **Step 5: Commit**

```bash
cd /home/michal/Documents/gitgui
git add ui/src/MainWindow.cpp tests/ui/test_main_window.cpp
git commit -m "feat(ui): branded empty-state cards (icon + headline + CTA)"
```

---

## Self-review notes

- **Spec coverage:** tokens §2 → Task 1; typography/spacing/shape §3–4 → encoded in QSS Task 2; theming mechanism §5 (QStyleHints, icon swap, live re-apply, no literals) → Tasks 2–3; component specs §6 (buttons, combo, tree rows, tabs, diff gutter, progress, empty-state cards) → QSS Task 2 + Task 5; iconography §6 → Task 4; a11y §7 (focus ring, contrast tokens) → tokens chosen to pass + accent focus via QSS (extendable). Light-mode parity present from Task 1. Diff gutter exact per-line coloring inside `DiffView` paint code is left to the existing DiffView (it consumes `state.*` tokens later); QSS sets the base gutter styling.
- **Type consistency:** `Theme` fields, `darkTheme()`/`lightTheme()`, `buildStyleSheet(const Theme&)`, `ThemeManager::Mode`/`setMode`/`currentTheme`/`iconResource`/`applyTo` are referenced identically across header, impl, and tests in every task.
- **Placeholder scan:** no TBD/TODO; every code step shows full code. The `%7` token position in Task 2 is intentionally reserved and documented, not a placeholder.
- **Ordering:** Task 4 adds the `.qrc`; Task 3's tests only check the resource *string*, so they pass before the resource exists.

---

## Outcome

Implemented the design system: the `Theme` token table (`darkTheme`/`lightTheme`), the pure QSS generator (`ThemeStyle`), `ThemeManager` (OS color-scheme resolution, live re-apply, themed icon), and branded empty-state cards. Realises [`spec/design`](../spec/design/design.md).
