# Plan 6 — Rebrand: GitGUI → GitTide

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename the project from `gitgui`/`GitGUI` to `gittide`/`GitTide` everywhere — C++ namespace, include dirs, CMake targets, app/window strings, docs — and migrate the user's on-disk config from the old location to the new one without data loss.

**Architecture:** A single mechanical rename pass (git mv on include dirs + `sed` on source/CMake) gets the codebase building under the new names, verified by the full existing test suite staying green. A separate, real, test-driven piece of logic — a Qt-free `migrate_config` helper in Core — moves `projects.json` / `session.json` from `~/.config/gitgui/...` to `~/.config/gittide/...` on first launch.

**Tech Stack:** C++23, Qt6, libgit2, Catch2 (Core tests), QtTest (UI tests), CMake ≥ 3.28, `std::filesystem`.

## Global Constraints

- **Do NOT rename the working-tree directory** `/home/michal/Documents/gitgui/` — it stays. Only project-internal identifiers change. Absolute paths in build commands keep `/home/michal/Documents/gitgui/`.
- The blanket `sed` runs over **source + CMake only** (`core ui app tests CMakeLists.txt cmake/`), never over `docs/` (plan docs contain absolute `/home/michal/Documents/gitgui/build` paths that must not change) and never over `build/` or `.git/`.
- All Core (`core/`) stays Qt-free — the migration helper uses only `std::filesystem`, no Qt.
- `Expected<T>` = `std::expected<T, GitError>` convention is already in Core; the migration helper returns a plain `bool` (no GitError needed — failure is non-fatal, logged and ignored).
- After Task 1 the entire existing suite (51 core tests + UI tests) must pass unchanged under the new names.
- Migration is **copy-and-leave**: never delete or modify the old config dir (rollback-safe).

---

## Task 1: Mechanical rename (namespace, include dirs, CMake, strings, docs)

**Files:**
- Rename: `core/include/gitgui/` → `core/include/gittide/`
- Rename: `ui/include/gitgui/` → `ui/include/gittide/`
- Modify (via sed): all `*.cpp` / `*.hpp` under `core/ ui/ app/ tests/`, all `CMakeLists.txt`, `cmake/*.cmake`
- Modify (manual): `docs/superpowers/specs/2026-06-16-gitgui-design.md` (rename file + content)
- Modify: `ui/src/MainWindow.cpp:80` window title
- Modify: `app/main.cpp:13-14` application/organization name

**Interfaces:**
- Consumes: nothing.
- Produces: every namespace `gitgui` → `gittide` (incl. `gitgui::ui` → `gittide::ui`); every include path `gitgui/...` → `gittide/...`; CMake targets `gitgui_core` → `gittide_core`, `gitgui_ui` → `gittide_ui`, `gitgui_app` → `gittide_app`, test targets `gitgui_core_tests` → `gittide_core_tests`, `gitgui_ui_tests` → `gittide_ui_tests`, ctest name `gitgui_ui_tests` → `gittide_ui_tests`; project name `project(gittide ...)`. Later tasks/plans rely on `gittide_*` target names and the `gittide::` / `gittide::ui` namespaces.

- [ ] **Step 1: Rename the public include directories**

```bash
cd /home/michal/Documents/gitgui
git mv core/include/gitgui core/include/gittide
git mv ui/include/gitgui ui/include/gittide
```

Expected: the `gitgui/ui` subdir moves with its parent — verify `ls ui/include/gittide/ui/` lists `MainWindow.hpp` etc.

- [ ] **Step 2: Blanket-rename lowercase identifier `gitgui` → `gittide` across source + CMake**

This covers `#include "gitgui/..."`, `namespace gitgui`, `gitgui::`, CMake target names, `project(gitgui ...)`, and the lowercase app/org string literals — all are the token `gitgui`.

```bash
cd /home/michal/Documents/gitgui
grep -rlZ 'gitgui' \
  --include='*.cpp' --include='*.hpp' --include='*.txt' --include='*.cmake' \
  core ui app tests cmake CMakeLists.txt \
  | xargs -0 sed -i 's/gitgui/gittide/g'
```

Expected: no errors. (If `cmake/` has no matches, that's fine.)

- [ ] **Step 3: Rename the display name `GitGUI` → `GitTide`**

Only the window title literal uses the CamelCase form.

```bash
cd /home/michal/Documents/gitgui
grep -rlZ 'GitGUI' --include='*.cpp' --include='*.hpp' core ui app tests \
  | xargs -0 sed -i 's/GitGUI/GitTide/g'
```

Expected: `ui/src/MainWindow.cpp` now reads `setWindowTitle(QStringLiteral("GitTide"));`.

- [ ] **Step 4: Rename the master design spec file + its content**

```bash
cd /home/michal/Documents/gitgui
git mv docs/superpowers/specs/2026-06-16-gitgui-design.md \
       docs/superpowers/specs/2026-06-16-gittide-design.md
sed -i 's/GitGUI/GitTide/g; s|gitgui/projects.json|gittide/projects.json|g; s/`gitgui`/`gittide`/g; s/namespace gitgui/namespace gittide/g; s|~/.config/gitgui|~/.config/gittide|g' \
       docs/superpowers/specs/2026-06-16-gittide-design.md
```

Note: this `sed` targets only the master spec file, by full path — it does NOT touch other docs. Leave the existing plan files (plan3/4/5) historical.

- [ ] **Step 5: Configure + build under the new names**

```bash
cmake -S /home/michal/Documents/gitgui -B /home/michal/Documents/gitgui/build
cmake --build /home/michal/Documents/gitgui/build -j
```

Expected: clean configure (target `gittide_*` created) and a clean build. A compile error naming `gitgui` means a literal was missed — grep `grep -rn 'gitgui' core ui app tests` and fix.

- [ ] **Step 6: Run the full test suite (this is the rename's test)**

```bash
ctest --test-dir /home/michal/Documents/gitgui/build --output-on-failure
```

Expected: all previously-passing tests pass — same count as before the rename. The ctest target is now `gittide_ui_tests`.

- [ ] **Step 7: Verify no stray lowercase identifier remains in source**

```bash
grep -rn 'gitgui' core ui app tests CMakeLists.txt cmake 2>/dev/null || echo "clean"
```

Expected: `clean` (or only matches that are genuinely unrelated — there should be none in source).

- [ ] **Step 8: Commit**

```bash
cd /home/michal/Documents/gitgui
git add -A
git commit -m "refactor: rename gitgui -> gittide (namespace, includes, CMake, strings, master spec)"
```

---

## Task 2: One-time config migration (gitgui dir → gittide dir)

**Files:**
- Create: `core/include/gittide/ConfigMigration.hpp`
- Create: `core/src/ConfigMigration.cpp`
- Modify: `core/CMakeLists.txt` (add the new source + header)
- Test: `tests/test_config_migration.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test source to `gittide_core_tests`)
- Modify: `app/main.cpp` (call migration before loading the store)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces:
  ```cpp
  namespace gittide {
  // Copies the contents of oldDir into newDir IFF newDir does not yet exist
  // and oldDir does. Never deletes or alters oldDir (rollback-safe). Returns
  // true iff a migration was performed.
  bool migrate_config(const std::filesystem::path& oldDir,
                      const std::filesystem::path& newDir);
  }  // namespace gittide
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/test_config_migration.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "gittide/ConfigMigration.hpp"

namespace fs = std::filesystem;

namespace {
fs::path uniqueTmp(const std::string& tag) {
    // Deterministic-enough unique dir under temp; cleaned by each test.
    static int counter = 0;
    return fs::temp_directory_path() /
           ("gittide-mig-" + tag + "-" + std::to_string(++counter));
}
std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}
}  // namespace

TEST_CASE("migrate_config copies files when new dir absent", "[migration]") {
    const fs::path oldDir = uniqueTmp("old");
    const fs::path newDir = uniqueTmp("new");
    fs::create_directories(oldDir);
    { std::ofstream(oldDir / "projects.json") << R"({"version":1})"; }
    { std::ofstream(oldDir / "session.json")  << R"({"version":1})"; }

    const bool migrated = gittide::migrate_config(oldDir, newDir);

    REQUIRE(migrated);
    REQUIRE(fs::exists(newDir / "projects.json"));
    REQUIRE(fs::exists(newDir / "session.json"));
    REQUIRE(readFile(newDir / "projects.json") == R"({"version":1})");
    // Old dir untouched (rollback-safe).
    REQUIRE(fs::exists(oldDir / "projects.json"));

    fs::remove_all(oldDir);
    fs::remove_all(newDir);
}

TEST_CASE("migrate_config is a no-op when new dir already exists", "[migration]") {
    const fs::path oldDir = uniqueTmp("old");
    const fs::path newDir = uniqueTmp("new");
    fs::create_directories(oldDir);
    fs::create_directories(newDir);  // already present → must not overwrite
    { std::ofstream(oldDir / "projects.json") << R"({"version":1,"from":"old"})"; }
    { std::ofstream(newDir / "projects.json") << R"({"version":1,"from":"new"})"; }

    const bool migrated = gittide::migrate_config(oldDir, newDir);

    REQUIRE_FALSE(migrated);
    REQUIRE(readFile(newDir / "projects.json") == R"({"version":1,"from":"new"})");

    fs::remove_all(oldDir);
    fs::remove_all(newDir);
}

TEST_CASE("migrate_config is a no-op when old dir absent", "[migration]") {
    const fs::path oldDir = uniqueTmp("old-absent");
    const fs::path newDir = uniqueTmp("new");
    // neither created
    REQUIRE_FALSE(gittide::migrate_config(oldDir, newDir));
    REQUIRE_FALSE(fs::exists(newDir));
}
```

- [ ] **Step 2: Add the test to the Core test target**

In `tests/CMakeLists.txt`, add `${CMAKE_CURRENT_SOURCE_DIR}/test_config_migration.cpp` to the `add_executable(gittide_core_tests ...)` source list.

- [ ] **Step 3: Run the test to verify it fails**

```bash
cmake --build /home/michal/Documents/gitgui/build -j
```

Expected: compile/link FAIL — `gittide/ConfigMigration.hpp` not found / `migrate_config` undefined.

- [ ] **Step 4: Write the header**

Create `core/include/gittide/ConfigMigration.hpp`:

```cpp
#pragma once
#include <filesystem>

namespace gittide {

// One-time config migration. Copies the entire contents of oldDir into newDir
// only when newDir does not exist yet and oldDir does. Never mutates oldDir, so
// it is safe to roll back to a prior GitGUI build. Returns true iff files were
// copied. All filesystem errors are swallowed (migration is best-effort).
bool migrate_config(const std::filesystem::path& oldDir,
                    const std::filesystem::path& newDir);

}  // namespace gittide
```

- [ ] **Step 5: Write the minimal implementation**

Create `core/src/ConfigMigration.cpp`:

```cpp
#include "gittide/ConfigMigration.hpp"

#include <system_error>

namespace fs = std::filesystem;

namespace gittide {

bool migrate_config(const fs::path& oldDir, const fs::path& newDir) {
    std::error_code ec;
    if (fs::exists(newDir, ec)) return false;       // already migrated / fresh install
    if (!fs::exists(oldDir, ec)) return false;      // nothing to migrate
    if (!fs::is_directory(oldDir, ec)) return false;

    fs::create_directories(newDir, ec);
    if (ec) return false;

    fs::copy(oldDir, newDir,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    return !ec;
}

}  // namespace gittide
```

- [ ] **Step 6: Add the source to the Core library**

In `core/CMakeLists.txt`, add to `gittide_core`'s sources:
```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ConfigMigration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ConfigMigration.hpp
```
(Match the existing list style in that file.)

- [ ] **Step 7: Build and run the migration tests**

```bash
cmake --build /home/michal/Documents/gitgui/build -j && \
ctest --test-dir /home/michal/Documents/gitgui/build -R gittide_core_tests --output-on-failure
```

Expected: all 3 new `[migration]` tests pass plus all existing core tests.

- [ ] **Step 8: Wire migration into app startup**

In `app/main.cpp`, after `QApplication::setOrganizationName(...)` (line ~14) and before computing `configDir`, add the migration call. Add `#include "gittide/ConfigMigration.hpp"` and `#include <QDir>` (already present). Replace the config-loading block:

```cpp
    QApplication::setApplicationName(QStringLiteral("gittide"));
    QApplication::setOrganizationName(QStringLiteral("gittide"));

    // One-time migration from the old GitGUI config location. The new location
    // is whatever QStandardPaths yields under the "gittide" app/org names; the
    // old one is the same path with "gittide" segments replaced by "gitgui".
    {
        const QString newCfg =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QString oldCfg = newCfg;
        oldCfg.replace(QStringLiteral("gittide"), QStringLiteral("gitgui"));
        if (oldCfg != newCfg) {
            gittide::migrate_config(
                std::filesystem::path(oldCfg.toStdString()),
                std::filesystem::path(newCfg.toStdString()));
        }
    }
```

Keep the existing `configDir` / `projectsFile` / `ProjectStore::load` block exactly as-is below this — it now reads from the (possibly just-migrated) new location.

- [ ] **Step 9: Build the app to confirm wiring compiles**

```bash
cmake --build /home/michal/Documents/gitgui/build -j --target gittide_app
```

Expected: clean build.

- [ ] **Step 10: Commit**

```bash
cd /home/michal/Documents/gitgui
git add core/include/gittide/ConfigMigration.hpp core/src/ConfigMigration.cpp \
        core/CMakeLists.txt tests/test_config_migration.cpp tests/CMakeLists.txt \
        app/main.cpp
git commit -m "feat: one-time config migration from gitgui to gittide config dir"
```

---

## Self-review notes

- **Spec coverage:** Rebrand spec (Section A of the design) — namespace ✓ (Task 1.2), include paths ✓ (1.1–1.2), CMake ✓ (1.2), window/app name ✓ (1.2–1.3), config path + migration ✓ (Task 2), docs ✓ (1.4). Packaging metadata (NSIS/dmg/AppImage names) is **not** yet present in the repo (no packaging files exist), so there is nothing to rename — it lands branded when packaging is added.
- **Type consistency:** `migrate_config(old, new) -> bool` used identically in header, impl, tests, and `main.cpp`.
- **Migration assumption:** deriving `oldCfg` by replacing `"gittide"`→`"gitgui"` in the config path is correct because both org and app names are `gittide`; documented inline in main.cpp.
