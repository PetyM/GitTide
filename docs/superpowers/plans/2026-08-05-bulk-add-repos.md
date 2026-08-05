# Bulk-add repositories — folder scan and repository sources

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-08-05 |
| **Status** | `done` |
| **Spec** | [`specs/2026-08-05-bulk-add-repos-design.md`](../specs/2026-08-05-bulk-add-repos-design.md); on close, `spec/product`, `spec/engineering`, `spec/design` |
| **Depends on** | — |

**Goal:** Add many existing repositories in one action by scanning a folder, and optionally keep that folder registered as a **repository source** that is rescanned on project activation so repos appearing later join the project automatically.

**Architecture:** `core/` gains a pure `scanForRepos(root, {maxDepth})` walker (std::filesystem + `GitRepo::open` validation) and a `RepoSource` value type persisted per project in `projects.json`. `ProjectController` runs scans off-thread with `QtConcurrent::run` under `co_await`, adds repos in one batch (single save, single model refresh), and rescans sources from `activate()`. QML gains `AddFromFolderDialog.qml` built entirely from the existing `AppDialog`/`DialogColumn`/`DialogButtons`/`AppButton`/`AppCheckBox` primitives, plus a Sources section in `ProjectOptionsDialog.qml`.

**Tech stack:** C++23, libgit2, nlohmann/json, Qt 6.8 Quick/QML, QCoro, Catch2 (core), QTest (ui).

## Global constraints

- **No Qt in `core/`.** `core/` compiles without Qt on the include path; speaks `std` only.
- **libgit2 and nlohmann/json are PRIVATE to `core/`** — no public header includes them.
- **Errors are values:** core returns `Expected<T> = std::expected<T, GitError>`; no exceptions across layers.
- **Paths via `toGitPath()` / `generic_u8string()`**, never `.string()`. Never build git command strings.
- **Colour from a `theme` token**, never a hex literal in QML.
- **Submodules are never added as repositories.** They already reach the user through the parent repo's submodule tree; the scan must stop at every repository boundary.
- **TDD:** failing test first, then the smallest implementation.
- New `core/` sources → `core/CMakeLists.txt`; new `ui/` sources → `ui/CMakeLists.txt`; new tests → the matching list in `tests/CMakeLists.txt`. A new `tests/ui/` class **also** needs an `#include` and a `RUN(...)` line in `tests/ui/main.cpp` — both edits, or the test silently never runs.
- **Code style:** Allman braces via `.clang-format`; `m_` members; lowercase file names; KISS/DRY/SOLID/YAGNI. Coroutine slots take args **by value**; guard `this` across every suspension with `QPointer`.
- **Build:** `cmake -S . -B build && cmake --build build --parallel`. Core tests run by tag: `./build/tests/gittide_core_tests "[scan]"`. UI tests: `./build/tests/gittide_ui_tests` (whole binary; per-class output is prefixed `[ui-test] running <Class>`).
- **Must keep passing:** the existing `[store]` cases, `TestProjectController`, `TestQmlShell`.

---

## File structure

**Core (new/modified):**
- Create `core/include/gittide/reposcan.hpp` — `ScanOptions`, `scanForRepos`.
- Create `core/src/reposcan.cpp` — the walker.
- Modify `core/include/gittide/projectstore.hpp` — `RepoSource`, `Project::sources`, four source mutators.
- Modify `core/src/projectstore.cpp` — JSON round-trip + mutators.
- Modify `core/CMakeLists.txt`.

**Core tests:**
- Create `tests/test_repo_scan.cpp`.
- Modify `tests/test_project_store.cpp`.
- Modify `tests/CMakeLists.txt`.

**UI (modified):**
- Modify `ui/include/gittide/ui/projectcontroller.hpp` + `ui/src/projectcontroller.cpp`.

**QML (new/modified):**
- Create `ui/qml/AddFromFolderDialog.qml`, `ui/qml/ToastNotice.qml`.
- Modify `ui/qml/qml.qrc`, `ui/qml/Main.qml`, `ui/qml/Sidebar.qml`, `ui/qml/EmptyState.qml`, `ui/qml/WorkingPane.qml`, `ui/qml/ProjectOptionsDialog.qml`.

**UI tests:**
- Modify `tests/ui/test_project_controller.cpp`.
- Create `tests/ui/test_qml_add_from_folder.cpp` + `tests/CMakeLists.txt` + `tests/ui/main.cpp`.

---

## Task 1: Core — `scanForRepos`

**Files:**
- Create: `core/include/gittide/reposcan.hpp`, `core/src/reposcan.cpp`
- Test: `tests/test_repo_scan.cpp`
- Modify: `core/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces produced:**
```cpp
namespace gittide {
struct ScanOptions { int maxDepth = 2; };
Expected<std::vector<std::string>> scanForRepos(const std::filesystem::path& root, ScanOptions opt = {});
}
```

- [x] **Step 1: Write the failing test** — create `tests/test_repo_scan.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <git2.h>
#include <random>
#include <string>

#include "gittide/libgit2context.hpp"
#include "gittide/pathutil.hpp"
#include "gittide/reposcan.hpp"
#include "support/temprepo.hpp"

namespace {

// A unique empty directory under the system temp dir, removed on destruction.
class TempDir
{
public:
    TempDir()
        : m_dir(std::filesystem::temp_directory_path() / ("gittide-scan-" + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_dir);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const
    {
        return m_dir;
    }

private:
    std::filesystem::path m_dir;
};

// git-init `p` (creating parents), leaving an empty repository behind.
void initRepoAt(const std::filesystem::path& p, bool bare = false)
{
    std::filesystem::create_directories(p);
    git_repository* raw = nullptr;
    REQUIRE(git_repository_init(&raw, gittide::toGitPath(p).c_str(), bare ? 1 : 0) == 0);
    git_repository_free(raw);
}

bool contains(const std::vector<std::string>& v, const std::filesystem::path& p)
{
    return std::find(v.begin(), v.end(), gittide::toGitPath(p)) != v.end();
}

} // namespace

TEST_CASE("scanForRepos finds repositories in direct children", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;
    initRepoAt(root.path() / "api");
    initRepoAt(root.path() / "web");
    std::filesystem::create_directories(root.path() / "notes"); // plain folder

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 1});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 2);
    REQUIRE(contains(*found, root.path() / "api"));
    REQUIRE(contains(*found, root.path() / "web"));
}

TEST_CASE("scanForRepos honours maxDepth", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;
    initRepoAt(root.path() / "acme" / "api");

    auto shallow = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 1});
    REQUIRE(shallow.has_value());
    REQUIRE(shallow->empty());

    auto deep = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 2});
    REQUIRE(deep.has_value());
    REQUIRE(deep->size() == 1);
    REQUIRE(contains(*deep, root.path() / "acme" / "api"));
}

TEST_CASE("scanForRepos stops descending at a repository", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;
    initRepoAt(root.path() / "api");
    initRepoAt(root.path() / "api" / "vendor" / "lib"); // nested checkout

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 4});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, root.path() / "api"));
}

TEST_CASE("scanForRepos never returns a repository's submodules", "[scan]")
{
    // Submodules reach the user through the parent repo's submodule tree.
    // Returning them here would duplicate every submodule as a top-level repo.
    gittide::test::TempRepo parent;
    gittide::test::TempRepo child;
    parent.setIdentity("Test", "test@example.com");
    parent.writeFile("a.txt", "one");
    parent.commitAll("c1");
    child.setIdentity("Test", "test@example.com");
    child.writeFile("b.txt", "two");
    child.commitAll("c1");
    parent.addSubmodule("vendor/lib", child.path());

    auto found = gittide::scanForRepos(parent.path(), gittide::ScanOptions{.maxDepth = 4});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, parent.path())); // the parent only — not vendor/lib
}

TEST_CASE("scanForRepos skips dot-directories", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;
    initRepoAt(root.path() / ".cache" / "api");

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 3});

    REQUIRE(found.has_value());
    REQUIRE(found->empty());
}

TEST_CASE("scanForRepos finds a bare repository", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;
    initRepoAt(root.path() / "mirror.git", /*bare=*/true);

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 1});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, root.path() / "mirror.git"));
}

TEST_CASE("scanForRepos returns the root itself when it is a repository", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;
    initRepoAt(root.path() / "api");

    auto found = gittide::scanForRepos(root.path() / "api", gittide::ScanOptions{.maxDepth = 2});

    REQUIRE(found.has_value());
    REQUIRE(found->size() == 1);
    REQUIRE(contains(*found, root.path() / "api"));
}

TEST_CASE("scanForRepos returns an empty list for a tree with no repositories", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;
    std::filesystem::create_directories(root.path() / "docs" / "images");

    auto found = gittide::scanForRepos(root.path(), gittide::ScanOptions{.maxDepth = 3});

    REQUIRE(found.has_value());
    REQUIRE(found->empty());
}

TEST_CASE("scanForRepos errors when the root is missing", "[scan]")
{
    gittide::LibGit2Context ctx;
    TempDir                 root;

    auto found = gittide::scanForRepos(root.path() / "nope");

    REQUIRE_FALSE(found.has_value());
}
```

Register the file: add `test_repo_scan.cpp` to the `gittide_core_tests` source list in `tests/CMakeLists.txt` (after `test_project_store.cpp`).

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --parallel` — expected: FAIL, `gittide/reposcan.hpp: No such file or directory`.

- [x] **Step 3: Write the header**

`core/include/gittide/reposcan.hpp`:

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "gittide/giterror.hpp"

namespace gittide {

/// Tuning for scanForRepos.
struct ScanOptions
{
    /// Directory levels below the root to search; 1 = its immediate children.
    /// Values below 1 are clamped to 1.
    int maxDepth = 2;
};

/// Find the git repositories under `root`.
///
/// Descent stops at a repository — a repository's interior is never searched,
/// so **submodules and nested checkouts are never returned**: submodules reach
/// the user through the parent repository's submodule tree, and returning them
/// here would duplicate each one as a top-level repository.
/// Directories whose name begins with '.' are skipped, as are directories that
/// cannot be read (a permission error is not a scan failure). When `root` is
/// itself a repository it is the sole result.
///
/// @returns repository paths as generic UTF-8 (forward slashes), sorted and
/// deduplicated; an empty vector when the tree holds none.
/// @returns an error only when `root` does not exist or is not a directory.
Expected<std::vector<std::string>> scanForRepos(const std::filesystem::path& root, ScanOptions opt = {});

} // namespace gittide
```

- [x] **Step 4: Write the implementation**

`core/src/reposcan.cpp`:

```cpp
#include "gittide/reposcan.hpp"

#include <algorithm>
#include <system_error>

#include "gittide/gitrepo.hpp"
#include "gittide/pathutil.hpp"

namespace gittide {
namespace {

/// Depth-first walk. `depth` counts levels already descended below the root.
void walk(const std::filesystem::path& dir, int depth, int maxDepth, std::vector<std::string>& out)
{
    if (depth >= maxDepth)
        return;

    std::error_code                     ec;
    std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
        return; // unreadable directory: skipped, not a scan failure

    for (const auto& entry : it)
    {
        if (!entry.is_directory(ec) || ec)
            continue;
        const std::string name = toGitPath(entry.path().filename());
        if (name.empty() || name.front() == '.')
            continue;

        if (GitRepo::open(entry.path()))
        {
            out.push_back(toGitPath(entry.path()));
            // A repository terminates the descent: its submodules and nested
            // checkouts must never be offered as repositories of their own.
            continue;
        }
        walk(entry.path(), depth + 1, maxDepth, out);
    }
}

} // namespace

Expected<std::vector<std::string>> scanForRepos(const std::filesystem::path& root, ScanOptions opt)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
        return std::unexpected(GitError{-1, "not a directory: " + toGitPath(root)});

    std::vector<std::string> out;
    if (GitRepo::open(root))
    {
        out.push_back(toGitPath(root));
        return out;
    }

    walk(root, 0, std::max(1, opt.maxDepth), out);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace gittide
```

Add `${CMAKE_CURRENT_SOURCE_DIR}/src/reposcan.cpp` to `target_sources(gittide_core ...)` in `core/CMakeLists.txt`, after `projectstore.cpp`.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ./build/tests/gittide_core_tests "[scan]"`
Expected: PASS, 9 test cases.

- [x] **Step 6: Commit**

```bash
git add core/include/gittide/reposcan.hpp core/src/reposcan.cpp core/CMakeLists.txt tests/test_repo_scan.cpp tests/CMakeLists.txt
git commit -m "feat(core): scan a folder for git repositories"
```

---

## Task 2: Core — `RepoSource` model and persistence

**Files:**
- Modify: `core/include/gittide/projectstore.hpp`, `core/src/projectstore.cpp`
- Test: `tests/test_project_store.cpp`

**Interfaces consumed:** none.

**Interfaces produced:**
```cpp
struct gittide::RepoSource { std::string path; int maxDepth = 2; std::vector<std::string> ignored; };
// gittide::Project gains: std::vector<RepoSource> sources;
```

- [x] **Step 1: Write the failing tests** — append to `tests/test_project_store.cpp`:

```cpp
TEST_CASE("sources round-trip through JSON", "[store][sources]")
{
    gittide::ProjectStore store;
    gittide::Project      p;
    p.id   = "uuid-1";
    p.name = "Work";
    p.sources.push_back(gittide::RepoSource{.path = "/home/u/projects", .maxDepth = 3, .ignored = {"/home/u/projects/scratch"}});
    store.projects().push_back(p);

    auto loaded = gittide::ProjectStore::from_json(store.to_json());

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects()[0].sources.size() == 1);
    REQUIRE(loaded->projects()[0].sources[0].path == "/home/u/projects");
    REQUIRE(loaded->projects()[0].sources[0].maxDepth == 3);
    REQUIRE(loaded->projects()[0].sources[0].ignored.size() == 1);
    REQUIRE(loaded->projects()[0].sources[0].ignored[0] == "/home/u/projects/scratch");
}

TEST_CASE("a document without \"sources\" loads with an empty source list", "[store][sources]")
{
    // The pre-sources on-disk schema: still version 1, no migration needed.
    const std::string legacy = R"({
      "version": 1,
      "activeProject": "uuid-1",
      "projects": [ { "id": "uuid-1", "name": "Work", "repos": [ { "path": "/home/u/api", "alias": "" } ] } ]
    })";

    auto loaded = gittide::ProjectStore::from_json(legacy);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].repos.size() == 1);
    REQUIRE(loaded->projects()[0].sources.empty());
    REQUIRE(loaded->loadedVersion() == gittide::ProjectStore::kVersion);
}

TEST_CASE("malformed source entries are skipped, not fatal", "[store][sources]")
{
    const std::string doc = R"({
      "version": 1,
      "projects": [ { "id": "uuid-1", "name": "Work", "sources": [ 42, { "path": "/home/u/projects" } ] } ]
    })";

    auto loaded = gittide::ProjectStore::from_json(doc);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects()[0].sources.size() == 1);
    REQUIRE(loaded->projects()[0].sources[0].path == "/home/u/projects");
    REQUIRE(loaded->projects()[0].sources[0].maxDepth == 2); // default
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel` — expected: FAIL, `no member named 'sources' in 'gittide::Project'`.

- [x] **Step 3: Add the type**

In `core/include/gittide/projectstore.hpp`, after `struct RepoRef`:

```cpp
/// A folder that is rescanned for repositories to add to a project.
struct RepoSource
{
    std::string path;                 ///< absolute, stored as UTF-8 generic path
    int         maxDepth = 2;         ///< see ScanOptions::maxDepth
    /// Repo paths this source must never add again — seeded from the repos the
    /// user left unchecked when registering, grown by removals from the project.
    std::vector<std::string> ignored;
};
```

and add to `struct Project`, after `repos`:

```cpp
    /// Folders rescanned on project activation; see RepoSource.
    std::vector<RepoSource> sources;
```

- [x] **Step 4: Serialize**

In `ProjectStore::to_json`, after the `jp["repos"] = std::move(repos);` line:

```cpp
        json sources = json::array();
        for (const auto& s : p.sources)
        {
            sources.push_back({{"path", s.path}, {"maxDepth", s.maxDepth}, {"ignored", s.ignored}});
        }
        jp["sources"] = std::move(sources);
```

In `ProjectStore::from_json`, after the `"repos"` block:

```cpp
                // "sources" is additive to the v1 schema: a document written
                // before sources existed simply has none.
                if (jp.contains("sources") && jp.at("sources").is_array())
                {
                    for (const auto& js : jp.at("sources"))
                    {
                        if (!js.is_object())
                            continue; // skip malformed source entries
                        RepoSource s;
                        s.path     = js.value("path", std::string{});
                        s.maxDepth = js.value("maxDepth", 2);
                        if (js.contains("ignored") && js.at("ignored").is_array())
                        {
                            for (const auto& ji : js.at("ignored"))
                            {
                                if (ji.is_string())
                                    s.ignored.push_back(ji.get<std::string>());
                            }
                        }
                        if (!s.path.empty())
                            p.sources.push_back(std::move(s));
                    }
                }
```

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ./build/tests/gittide_core_tests "[store]"`
Expected: PASS, including every pre-existing `[store]` case.

- [x] **Step 6: Commit**

```bash
git add core/include/gittide/projectstore.hpp core/src/projectstore.cpp tests/test_project_store.cpp
git commit -m "feat(core): persist repository sources per project"
```

---

## Task 3: Core — source mutators

**Files:**
- Modify: `core/include/gittide/projectstore.hpp`, `core/src/projectstore.cpp`
- Test: `tests/test_project_store.cpp`

**Interfaces consumed:** `RepoSource`, `Project::sources` (Task 2).

**Interfaces produced:**
```cpp
Expected<void> ProjectStore::addSource(const std::string& projectId, RepoSource src);
Expected<void> ProjectStore::removeSource(const std::string& projectId, const std::string& path);
void           ProjectStore::ignoreInSources(const std::string& projectId, const std::string& repoPath);
Expected<void> ProjectStore::clearIgnored(const std::string& projectId, const std::string& sourcePath);
```

- [x] **Step 1: Write the failing tests** — append to `tests/test_project_store.cpp`:

```cpp
TEST_CASE("addSource appends a source to the project", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");

    auto result = store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects", .maxDepth = 3});

    REQUIRE(result.has_value());
    REQUIRE(store.projects()[0].sources.size() == 1);
    REQUIRE(store.projects()[0].sources[0].maxDepth == 3);
}

TEST_CASE("addSource rejects a duplicate source path", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());

    auto again = store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"});

    REQUIRE_FALSE(again.has_value());
    REQUIRE(store.projects()[0].sources.size() == 1);
}

TEST_CASE("addSource returns an error for an unknown project id", "[store][sources]")
{
    gittide::ProjectStore store;
    REQUIRE_FALSE(store.addSource("nope", gittide::RepoSource{.path = "/home/u/projects"}).has_value());
}

TEST_CASE("removeSource drops the source but keeps the repos it added", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());
    REQUIRE(store.addRepo(p.id, gittide::RepoRef{.path = "/home/u/projects/api"}).has_value());

    auto result = store.removeSource(p.id, "/home/u/projects");

    REQUIRE(result.has_value());
    REQUIRE(store.projects()[0].sources.empty());
    REQUIRE(store.projects()[0].repos.size() == 1);
}

TEST_CASE("removeSource errors on an unknown source path", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");
    REQUIRE_FALSE(store.removeSource(p.id, "/home/u/projects").has_value());
}

TEST_CASE("ignoreInSources records the repo under the containing source", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/work"}).has_value());

    store.ignoreInSources(p.id, "/home/u/projects/api");

    REQUIRE(store.projects()[0].sources[0].ignored == std::vector<std::string>{"/home/u/projects/api"});
    REQUIRE(store.projects()[0].sources[1].ignored.empty());
}

TEST_CASE("ignoreInSources matches on directory boundaries only", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/proj"}).has_value());

    store.ignoreInSources(p.id, "/home/u/projects/api"); // "/home/u/proj" is NOT a parent
    store.ignoreInSources(p.id, "/home/u/proj");         // the source folder itself is not "under" it

    REQUIRE(store.projects()[0].sources[0].ignored.empty());
}

TEST_CASE("ignoreInSources never records the same path twice", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects"}).has_value());

    store.ignoreInSources(p.id, "/home/u/projects/api");
    store.ignoreInSources(p.id, "/home/u/projects/api");

    REQUIRE(store.projects()[0].sources[0].ignored.size() == 1);
}

TEST_CASE("ignoreInSources on an unknown project is a no-op", "[store][sources]")
{
    gittide::ProjectStore store;
    store.ignoreInSources("nope", "/home/u/projects/api"); // must not throw
    REQUIRE(store.projects().empty());
}

TEST_CASE("clearIgnored empties one source's ignore list", "[store][sources]")
{
    gittide::ProjectStore store;
    auto&                 p = store.createProject("Work");
    REQUIRE(store.addSource(p.id, gittide::RepoSource{.path = "/home/u/projects", .maxDepth = 2, .ignored = {"/home/u/projects/api"}})
                .has_value());

    REQUIRE(store.clearIgnored(p.id, "/home/u/projects").has_value());

    REQUIRE(store.projects()[0].sources[0].ignored.empty());
    REQUIRE_FALSE(store.clearIgnored(p.id, "/home/u/nowhere").has_value());
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel` — expected: FAIL, `no member named 'addSource' in 'gittide::ProjectStore'`.

- [x] **Step 3: Declare the mutators**

In `core/include/gittide/projectstore.hpp`, after `removeRepo`:

```cpp
    // Register a folder as a repository source of the named project. Returns an
    // error if projectId is not found, or if a source with the same path already
    // exists in that project. Call save() after mutating to persist the change.
    Expected<void> addSource(const std::string& projectId, RepoSource src);

    // Unregister a source by path. The repositories it already added stay in the
    // project. Returns an error if the project or the source is not found.
    Expected<void> removeSource(const std::string& projectId, const std::string& path);

    // Record repoPath as ignored in every source that contains it, so a rescan
    // never re-adds a repository the user removed. Matching is on directory
    // boundaries; a path already recorded is not duplicated. Unknown project,
    // or a path under no source: no-op.
    void ignoreInSources(const std::string& projectId, const std::string& repoPath);

    // Empty one source's ignore list, so its next scan offers everything again.
    // Returns an error if the project or the source is not found.
    Expected<void> clearIgnored(const std::string& projectId, const std::string& sourcePath);
```

- [x] **Step 4: Implement them**

In `core/src/projectstore.cpp`, after `removeRepo`. The file already has a project-lookup idiom (a loop over `m_projects`); add a file-local helper next to the other anonymous-namespace helpers if one exists, otherwise inline the loop as below.

```cpp
namespace {

// True when `child` lies inside directory `parent` — a plain prefix test is
// wrong ("/home/u/proj" would swallow "/home/u/projects/api"), so the prefix
// must end on a separator. Paths here are always generic (forward-slash) form.
bool isUnder(const std::string& parent, const std::string& child)
{
    if (parent.empty() || child.size() <= parent.size())
        return false;
    if (child.compare(0, parent.size(), parent) != 0)
        return false;
    const bool parentEndsWithSlash = parent.back() == '/';
    return parentEndsWithSlash || child[parent.size()] == '/';
}

} // namespace

Expected<void> ProjectStore::addSource(const std::string& projectId, RepoSource src)
{
    for (auto& p : m_projects)
    {
        if (p.id != projectId)
            continue;
        for (const auto& s : p.sources)
        {
            if (s.path == src.path)
                return std::unexpected(GitError{-1, "source already registered: " + src.path});
        }
        p.sources.push_back(std::move(src));
        return {};
    }
    return std::unexpected(GitError{-1, "unknown project: " + projectId});
}

Expected<void> ProjectStore::removeSource(const std::string& projectId, const std::string& path)
{
    for (auto& p : m_projects)
    {
        if (p.id != projectId)
            continue;
        for (auto it = p.sources.begin(); it != p.sources.end(); ++it)
        {
            if (it->path == path)
            {
                // Deliberately leaves p.repos alone: unregistering a source must
                // not silently drop repositories the user is working in.
                p.sources.erase(it);
                return {};
            }
        }
        return std::unexpected(GitError{-1, "source not found: " + path});
    }
    return std::unexpected(GitError{-1, "unknown project: " + projectId});
}

void ProjectStore::ignoreInSources(const std::string& projectId, const std::string& repoPath)
{
    for (auto& p : m_projects)
    {
        if (p.id != projectId)
            continue;
        for (auto& s : p.sources)
        {
            if (!isUnder(s.path, repoPath))
                continue;
            if (std::find(s.ignored.begin(), s.ignored.end(), repoPath) == s.ignored.end())
                s.ignored.push_back(repoPath);
        }
        return;
    }
}

Expected<void> ProjectStore::clearIgnored(const std::string& projectId, const std::string& sourcePath)
{
    for (auto& p : m_projects)
    {
        if (p.id != projectId)
            continue;
        for (auto& s : p.sources)
        {
            if (s.path == sourcePath)
            {
                s.ignored.clear();
                return {};
            }
        }
        return std::unexpected(GitError{-1, "source not found: " + sourcePath});
    }
    return std::unexpected(GitError{-1, "unknown project: " + projectId});
}
```

Add `#include <algorithm>` at the top of `core/src/projectstore.cpp` if it is not already there (`std::find`).

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ./build/tests/gittide_core_tests "[store]"`
Expected: PASS.

- [x] **Step 6: Commit**

```bash
git add core/include/gittide/projectstore.hpp core/src/projectstore.cpp tests/test_project_store.cpp
git commit -m "feat(core): add, remove and ignore-list repository sources"
```

---

## Task 4: ViewModel — `scanFolder`

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp`, `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp`

**Interfaces consumed:** `gittide::scanForRepos`, `gittide::ScanOptions` (Task 1).

**Interfaces produced:**
```cpp
Q_INVOKABLE QCoro::Task<void> ProjectController::scanFolder(QString path, int maxDepth);
signals: void scanFinished(const QVariantList& candidates); // { path, name, alreadyAdded }
         void scanFailed(const QString& message);
```

- [x] **Step 1: Write the failing test** — add these two slots to `TestProjectController` in `tests/ui/test_project_controller.cpp`, plus the helper at the bottom of the class (next to `makeRepoWithUpstream`):

```cpp
    void scanFolder_lists_candidates_and_marks_already_added()
    {
        const auto root = makeScanRoot({"api", "web"});

        ProjectStore store;
        store.projects().push_back(Project{.id    = "id-a",
                                           .name  = "Work",
                                           .repos = {RepoRef{.path = (root / "api").generic_string()}}});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::scanFinished);
        controller.scanFolder(QString::fromStdString(root.generic_string()), 1);
        QVERIFY(spy.wait(5000));

        const QVariantList candidates = spy.at(0).at(0).toList();
        QCOMPARE(candidates.size(), 2);

        // Sorted by path, so "api" precedes "web".
        const QVariantMap api = candidates.at(0).toMap();
        QCOMPARE(api.value("name").toString(), QStringLiteral("api"));
        QCOMPARE(api.value("alreadyAdded").toBool(), true);
        QCOMPARE(candidates.at(1).toMap().value("alreadyAdded").toBool(), false);
    }

    void scanFolder_reports_a_missing_folder()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::scanFailed);
        controller.scanFolder(QStringLiteral("/definitely/not/here"), 2);
        QVERIFY(spy.wait(5000));
        QVERIFY(!spy.at(0).at(0).toString().isEmpty());
    }
```

Helper (add next to `makeRepoWithUpstream`; `m_scanRoots` is a new member beside `m_temps`):

```cpp
    // A scratch folder holding one empty repository per name, removed with the
    // fixture. Used to exercise the folder scan without a full TempRepo each.
    std::filesystem::path makeScanRoot(const QStringList& names)
    {
        const auto root = std::filesystem::temp_directory_path() /
                          ("gittide-pc-scan-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(root);
        for (const QString& name : names)
        {
            const auto dir = root / name.toStdString();
            std::filesystem::create_directories(dir);
            git_repository* raw = nullptr;
            git_repository_init(&raw, dir.generic_string().c_str(), 0);
            git_repository_free(raw);
        }
        m_scanRoots.push_back(root);
        return root;
    }

    std::vector<std::filesystem::path> m_scanRoots;
```

and remove them in `cleanupTestCase()`, before `git_libgit2_shutdown()`:

```cpp
        for (const auto& root : m_scanRoots)
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --parallel` — expected: FAIL, `no member named 'scanFolder' in 'ProjectController'`.

- [x] **Step 3: Declare the API**

In `ui/include/gittide/ui/projectcontroller.hpp`, in `public slots:` next to `addExistingRepo`:

```cpp
    /// Scan `path` for repositories, `maxDepth` levels deep, off the GUI thread.
    /// Results arrive on scanFinished (one QVariantMap per candidate) or
    /// scanFailed. Safe to call with no active project — every candidate is then
    /// reported as not-already-added.
    Q_INVOKABLE QCoro::Task<void> scanFolder(QString path, int maxDepth);
```

and in `signals:`:

```cpp
    /// One QVariantMap per discovered repository:
    /// { path: QString, name: QString, alreadyAdded: bool }.
    void scanFinished(const QVariantList& candidates);
    void scanFailed(const QString& message);
```

- [x] **Step 4: Implement it**

In `ui/src/projectcontroller.cpp` — add `#include <QPointer>`, `#include <QSet>`, `#include <QVariantMap>` and `#include "gittide/reposcan.hpp"` / `#include "gittide/pathutil.hpp"` to the include block, then:

```cpp
QCoro::Task<void> ProjectController::scanFolder(QString path, int maxDepth)
{
    QPointer<ProjectController> self(this);
    const std::filesystem::path root(path.toStdString());

    auto result = co_await QtConcurrent::run(
        [root, maxDepth] { return gittide::scanForRepos(root, gittide::ScanOptions{.maxDepth = maxDepth}); });
    if (!self)
        co_return; // controller went away while the scan ran

    if (!result)
    {
        emit scanFailed(QString::fromStdString(result.error().message));
        co_return;
    }

    // Repos already in the project are reported, not dropped, so the checklist
    // shows the whole picture instead of silently hiding them.
    QSet<QString> existing;
    for (const auto& r : activeRepos())
        existing.insert(QString::fromStdString(r.path));

    QVariantList candidates;
    for (const auto& p : *result)
    {
        const QString qp   = QString::fromStdString(p);
        const QString name = QString::fromStdString(gittide::toGitPath(std::filesystem::path(p).filename()));
        candidates.append(QVariantMap{{QStringLiteral("path"), qp},
                                      {QStringLiteral("name"), name},
                                      {QStringLiteral("alreadyAdded"), existing.contains(qp)}});
    }
    emit scanFinished(candidates);
}
```

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ./build/tests/gittide_ui_tests`
Expected: PASS; `[ui-test] running TestProjectController` reports no failures.

- [x] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): scan a folder for repositories off-thread"
```

---

## Task 5: ViewModel — batch add and source registration

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp`, `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp`

**Interfaces consumed:** `ProjectStore::addSource` (Task 3), `makeScanRoot` (Task 4).

**Interfaces produced:**
```cpp
Q_INVOKABLE void ProjectController::addRepos(const QStringList& paths, const QStringList& unchecked,
                                             const QString& sourcePath, int maxDepth);
signals: void reposAdded(int added, const QStringList& failures);
```

- [x] **Step 1: Write the failing tests** — add to `TestProjectController`:

```cpp
    void addRepos_adds_the_batch_and_saves_once()
    {
        const auto root      = makeScanRoot({"api", "web"});
        const auto storePath = std::filesystem::temp_directory_path() /
                               ("gittide-pc-batch-" +
                                QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() + ".json");

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store, storePath);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::reposAdded);
        controller.addRepos({QString::fromStdString((root / "api").generic_string()),
                             QString::fromStdString((root / "web").generic_string())},
                            {}, QString(), 2);

        QCOMPARE(spy.count(), 1); // one signal for the whole batch, not one per repo
        QCOMPARE(spy.at(0).at(0).toInt(), 2);
        QCOMPARE(spy.at(0).at(1).toStringList().size(), 0);
        QCOMPARE(controller.repos()->rowCount(), 2);

        auto reloaded = ProjectStore::load(storePath);
        QVERIFY(reloaded.has_value());
        QCOMPARE(static_cast<int>(reloaded->projects()[0].repos.size()), 2);
    }

    void addRepos_reports_failures_without_aborting_the_batch()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::reposAdded);
        controller.addRepos({QStringLiteral("/definitely/not/a/repo"),
                             QString::fromStdString((root / "api").generic_string())},
                            {}, QString(), 2);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        QCOMPARE(spy.at(0).at(1).toStringList().size(), 1);
        QCOMPARE(controller.repos()->rowCount(), 1);
    }

    void addRepos_registers_a_source_with_the_unchecked_paths_ignored()
    {
        const auto root = makeScanRoot({"api", "web"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        controller.addRepos({QString::fromStdString((root / "api").generic_string())},
                            {QString::fromStdString((root / "web").generic_string())},
                            QString::fromStdString(root.generic_string()), 3);

        QCOMPARE(static_cast<int>(store.projects()[0].sources.size()), 1);
        QCOMPARE(store.projects()[0].sources[0].maxDepth, 3);
        QCOMPARE(static_cast<int>(store.projects()[0].sources[0].ignored.size()), 1);
        QCOMPARE(QString::fromStdString(store.projects()[0].sources[0].ignored[0]),
                 QString::fromStdString((root / "web").generic_string()));
    }

    void addRepos_without_an_active_project_fails_loudly()
    {
        ProjectStore      store;
        ProjectController controller(&store);

        QSignalSpy spy(&controller, &ProjectController::repoAddFailed);
        controller.addRepos({QStringLiteral("/anything")}, {}, QString(), 2);

        QCOMPARE(spy.count(), 1);
    }
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel` — expected: FAIL, `no member named 'addRepos'`.

- [x] **Step 3: Declare the API**

In `public slots:`, after `addExistingRepo`:

```cpp
    /// Add every path in `paths` to the active project in one batch: a repo that
    /// fails validation is reported, never aborts the rest, and the store is
    /// saved and the model refreshed once for the whole batch.
    ///
    /// When `sourcePath` is non-empty the folder is also registered as a
    /// RepoSource with `maxDepth`, seeded with `unchecked` as its ignore list —
    /// a repo left unchecked at registration is never offered again.
    Q_INVOKABLE void addRepos(const QStringList& paths, const QStringList& unchecked,
                              const QString& sourcePath, int maxDepth);
```

In `signals:`:

```cpp
    /// Result of one addRepos batch. `failures` holds a "name: message" line per
    /// repository that could not be added.
    void reposAdded(int added, const QStringList& failures);
```

- [x] **Step 4: Implement it**

Add `#include <QFileInfo>` to `ui/src/projectcontroller.cpp`, then after `addExistingRepo`:

```cpp
void ProjectController::addRepos(const QStringList& paths, const QStringList& unchecked,
                                 const QString& sourcePath, int maxDepth)
{
    if (m_activeId.isEmpty())
    {
        emit repoAddFailed(QStringLiteral("No active project"));
        return;
    }

    int         added = 0;
    QStringList failures;
    for (const QString& path : paths)
    {
        const QString               name = QFileInfo(path).fileName();
        const std::filesystem::path p(path.toStdString());

        auto validation = gittide::GitRepo::open(p);
        if (!validation)
        {
            failures << (name + QStringLiteral(": ") + QString::fromStdString(validation.error().message));
            continue;
        }
        auto result = m_store->addRepo(m_activeId.toStdString(), gittide::RepoRef{.path = path.toStdString()});
        if (!result)
        {
            failures << (name + QStringLiteral(": ") + QString::fromStdString(result.error().message));
            continue;
        }
        ++added;
    }

    if (!sourcePath.isEmpty())
    {
        gittide::RepoSource src{.path = sourcePath.toStdString(), .maxDepth = maxDepth};
        for (const QString& skipped : unchecked)
            src.ignored.push_back(skipped.toStdString());
        auto registered = m_store->addSource(m_activeId.toStdString(), std::move(src));
        if (!registered)
            failures << QString::fromStdString(registered.error().message);
    }

    // One save and one model refresh for the whole batch — never per repository.
    saveStore();
    refreshRepoModel();
    hydrateRepoModel();
    emit reposAdded(added, failures);
}
```

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ./build/tests/gittide_ui_tests`
Expected: PASS.

- [x] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): add repositories in one batch and register a source"
```

---

## Task 6: ViewModel — rescan on activation, removal feeds the ignore list

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp`, `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp`

**Interfaces consumed:** `scanForRepos` (Task 1), `ignoreInSources` (Task 3), `addRepos` (Task 5).

**Interfaces produced:**
```cpp
Q_INVOKABLE QCoro::Task<void> ProjectController::rescanSources();
signals: void sourcesRescanned(int added, int unavailableSources);
```

- [x] **Step 1: Write the failing tests** — add to `TestProjectController`:

```cpp
    void rescanSources_adds_a_repo_that_appeared_after_registration()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));
        controller.addRepos({QString::fromStdString((root / "api").generic_string())}, {},
                            QString::fromStdString(root.generic_string()), 1);
        QCOMPARE(controller.repos()->rowCount(), 1);

        // A repo cloned into the source folder after registration.
        const auto later = root / "web";
        std::filesystem::create_directories(later);
        git_repository* raw = nullptr;
        git_repository_init(&raw, later.generic_string().c_str(), 0);
        git_repository_free(raw);

        QSignalSpy spy(&controller, &ProjectController::sourcesRescanned);
        controller.rescanSources();
        QVERIFY(spy.wait(5000));

        QCOMPARE(spy.at(0).at(0).toInt(), 1);
        QCOMPARE(controller.repos()->rowCount(), 2);
    }

    void rescanSources_never_re_adds_a_removed_repo()
    {
        const auto root = makeScanRoot({"api", "web"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));
        controller.addRepos({QString::fromStdString((root / "api").generic_string()),
                             QString::fromStdString((root / "web").generic_string())},
                            {}, QString::fromStdString(root.generic_string()), 1);
        QCOMPARE(controller.repos()->rowCount(), 2);

        controller.removeRepo(QString::fromStdString((root / "web").generic_string()));
        QCOMPARE(controller.repos()->rowCount(), 1);

        QSignalSpy spy(&controller, &ProjectController::sourcesRescanned);
        controller.rescanSources();
        QVERIFY(spy.wait(5000));

        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QCOMPARE(controller.repos()->rowCount(), 1);
    }

    void rescanSources_counts_an_unavailable_source_and_keeps_going()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        // One source that does not exist, one that does.
        store.projects()[0].sources.push_back(gittide::RepoSource{.path = "/definitely/not/here", .maxDepth = 1});
        store.projects()[0].sources.push_back(
            gittide::RepoSource{.path = root.generic_string(), .maxDepth = 1});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QSignalSpy spy(&controller, &ProjectController::sourcesRescanned);
        QVERIFY(spy.wait(5000)); // activate() kicks the rescan itself

        QCOMPARE(spy.at(0).at(0).toInt(), 1); // the reachable source still added its repo
        QCOMPARE(spy.at(0).at(1).toInt(), 1); // and the missing one is reported
    }
```

Add `#include "gittide/projectstore.hpp"`-scoped `using gittide::RepoSource;` next to the existing `using` lines if the fully-qualified name is not used.

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel` — expected: FAIL, `no member named 'rescanSources'`.

- [x] **Step 3: Declare the API**

In `public slots:`:

```cpp
    /// Rescan every source of the active project and add what is new — skipping
    /// repositories already in the project and those on a source's ignore list.
    /// One save and one model refresh for the whole pass. A source whose folder
    /// has gone is counted as unavailable and left registered.
    Q_INVOKABLE QCoro::Task<void> rescanSources();
```

In `signals:`:

```cpp
    /// Result of one rescan pass over the active project's sources.
    void sourcesRescanned(int added, int unavailableSources);
```

- [x] **Step 4: Implement it**

In `ui/src/projectcontroller.cpp`:

```cpp
QCoro::Task<void> ProjectController::rescanSources()
{
    QPointer<ProjectController> self(this);
    if (m_activeId.isEmpty())
        co_return;

    const std::string pid = m_activeId.toStdString();

    // Copy the sources: the store is mutated below and may be re-entered across
    // a co_await, so iterating it directly would be a dangling-reference bug.
    std::vector<gittide::RepoSource> sources;
    for (const auto& p : m_store->projects())
    {
        if (p.id == pid)
        {
            sources = p.sources;
            break;
        }
    }
    if (sources.empty())
        co_return;

    int added       = 0;
    int unavailable = 0;
    for (const auto& src : sources)
    {
        const std::filesystem::path root(src.path);
        const int                   depth = src.maxDepth;

        auto found = co_await QtConcurrent::run(
            [root, depth] { return gittide::scanForRepos(root, gittide::ScanOptions{.maxDepth = depth}); });
        if (!self)
            co_return;

        if (!found)
        {
            ++unavailable; // folder gone or unreadable: report, never unregister
            continue;
        }

        for (const auto& path : *found)
        {
            if (std::find(src.ignored.begin(), src.ignored.end(), path) != src.ignored.end())
                continue;
            // addRepo rejects a path already in the project, so it doubles as the
            // duplicate filter — a repo the user already has is silently skipped.
            if (m_store->addRepo(pid, gittide::RepoRef{.path = path}))
                ++added;
        }
    }

    if (added > 0)
    {
        saveStore();
        refreshRepoModel();
        hydrateRepoModel();
    }
    emit sourcesRescanned(added, unavailable);
}
```

Add `#include <algorithm>` if absent.

- [x] **Step 5: Kick the rescan from `activate()` and feed the ignore list from `removeRepo()`**

In `ProjectController::activate`, immediately before `return;` in the matching branch (after `emit projectActivated(projectId);`):

```cpp
            // Pick up repos that appeared in a registered source since last time.
            // The task is intentionally not awaited — it settles on its own and
            // reports via sourcesRescanned, the same way refreshSubmodules does.
            rescanSources();
```

In `ProjectController::removeRepo`, after the `lastActiveRepo` clean-up loop and before the existing `saveStore()`:

```cpp
    // Removing a repo that came from a source must stick: record it so the next
    // rescan does not add it straight back.
    m_store->ignoreInSources(m_activeId.toStdString(), path.toStdString());
```

- [x] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ./build/tests/gittide_ui_tests`
Expected: PASS, including the pre-existing `activate_*` and `removeRepo` cases.

- [x] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): rescan repository sources on project activation"
```

---

## Task 7: QML — `AddFromFolderDialog` and its entry points

**Files:**
- Create: `ui/qml/AddFromFolderDialog.qml`, `ui/qml/ToastNotice.qml`, `tests/ui/test_qml_add_from_folder.cpp`
- Modify: `ui/qml/qml.qrc`, `ui/qml/Main.qml`, `ui/qml/Sidebar.qml`, `ui/qml/EmptyState.qml`, `ui/qml/WorkingPane.qml`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces consumed:** `projectController.scanFolder(path, maxDepth)`, `scanFinished`, `scanFailed`, `addRepos(...)`, `reposAdded` (Tasks 4–5).

**Interfaces produced:** QML `objectName`s `addFromFolderDialog`, `addFromFolderList`, `addFromFolderConfirm`, `addFromFolderKeepSource`, `addFromFolderCta`, and the `Sidebar`/`EmptyState`/`WorkingPane` signal `addFromFolderRequested()`.

- [x] **Step 1: Write the failing test** — create `tests/ui/test_qml_add_from_folder.cpp`:

```cpp
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtTest>

#include "gittide/projectstore.hpp"
#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

// The add-from-folder dialog must exist in the shell and be reachable from the
// empty state, so the bulk flow is not left orphaned behind a menu item.
class TestQmlAddFromFolder : public QObject
{
    Q_OBJECT
private slots:
    void dialog_and_cta_exist_in_the_shell()
    {
        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        ThemeManager          themes;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("projectController"), &controller);
        engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &themes);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        QObject* root = engine.rootObjects().first();
        QVERIFY(root->findChild<QObject*>(QStringLiteral("addFromFolderDialog")) != nullptr);
        QVERIFY(root->findChild<QObject*>(QStringLiteral("addFromFolderCta")) != nullptr);
    }
};
```

> **Note for the implementer:** mirror the context-property setup of the
> neighbouring `tests/ui/test_qml_shell.cpp` `main_qml_loads_without_errors()`
> slot — `Main.qml` reads several context properties, and any it needs beyond
> the two above must be provided here too or the load will warn. Copy that
> slot's setup verbatim and add the two `findChild` assertions.

Register it: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_add_from_folder.cpp` to `gittide_ui_test_sources` in `tests/CMakeLists.txt`, add `#include "test_qml_add_from_folder.cpp"` and `RUN(TestQmlAddFromFolder);` to `tests/ui/main.cpp`. Both edits are mandatory.

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --parallel && ./build/tests/gittide_ui_tests`
Expected: FAIL — `addFromFolderDialog` not found.

- [x] **Step 3: Write the dialog**

`ui/qml/AddFromFolderDialog.qml` — every visual comes from `theme`, and the
structure follows `CloneRepoDialog.qml` (picker row) and `BranchPickerDialog.qml`
(list-in-a-dialog):

```qml
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
        if (folder.length === 0 || !projectController)
            return
        errorText = ""
        candidates = []
        scanning = true
        projectController.scanFolder(folder, depthBox.value)
    }

    Connections {
        target: projectController
        function onScanFinished(found) {
            if (!dialog.visible)
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
            if (!dialog.visible)
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
            dialog.folder = selectedFolder.toString().replace(/^file:\/\//, "")
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
```

- [x] **Step 4: Register and wire it**

1. `ui/qml/qml.qrc` — add `<file>AddFromFolderDialog.qml</file>` next to `CloneRepoDialog.qml`.
2. `ui/qml/Sidebar.qml` — add `signal addFromFolderRequested()` next to `addExistingRequested()`, and a menu item in `addRepoMenu` after "Add existing repository…":
   ```qml
   AppMenuItem { text: "Add repositories from folder…"; onTriggered: sidebar.addFromFolderRequested() }
   ```
3. `ui/qml/EmptyState.qml` — add `signal addFromFolderRequested()` and a CTA after `addExistingCta`:
   ```qml
   Cta {
       objName: "addFromFolderCta"
       text: "Add repositories from folder"
       visible: emptyState.hasProject
       onClicked: emptyState.addFromFolderRequested()
   }
   ```
4. `ui/qml/WorkingPane.qml` — mirror the existing `addExistingRequested` signal and its forwarding from the embedded `EmptyState` with an `addFromFolderRequested` pair (follow whatever pattern the file already uses for `addExistingRequested`).
5. `ui/qml/Main.qml` — host the dialog next to `addExistingFolder` (around line 472):
   ```qml
   AddFromFolderDialog { id: addFromFolderDialog }
   ```
   and wire both hosts (around lines 150 and 165):
   ```qml
   onAddFromFolderRequested: addFromFolderDialog.openDialog()
   ```
6. Create `ui/qml/ToastNotice.qml` — the brief, non-modal surface for
   "repositories were added" (a modal would interrupt project switching, which is
   when the rescan runs):

   ```qml
   import QtQuick
   import QtQuick.Controls.Basic

   // Transient inline notice: fades in at the bottom of the window, holds for a
   // few seconds, fades out. Non-blocking by design — used for outcomes the user
   // does not have to act on, e.g. repositories picked up by a source rescan.
   Item {
       id: toast
       objectName: "toastNotice"

       property string text: ""
       readonly property bool showing: hold.running || fade.running

       anchors.horizontalCenter: parent.horizontalCenter
       anchors.bottom: parent.bottom
       anchors.bottomMargin: 24
       width: card.width
       height: card.height
       visible: opacity > 0
       opacity: 0

       function show(message) {
           if (message.length === 0)
               return
           toast.text = message
           toast.opacity = 1
           hold.restart()
       }

       OverlayCard {
           id: card
           width: label.implicitWidth + 32
           height: label.implicitHeight + 20

           Label {
               id: label
               anchors.centerIn: parent
               text: toast.text
               color: theme.textPrimary
               font.pixelSize: 12
           }
       }

       Timer {
           id: hold
           interval: 3500
           onTriggered: fade.start()
       }
       NumberAnimation {
           id: fade
           target: toast
           property: "opacity"
           to: 0
           duration: 200
       }
   }
   ```

   Register it in `ui/qml/qml.qrc`, instantiate it once in `Main.qml` inside the
   root window (after the SplitView so it paints on top), and wire the rescan
   result:

   ```qml
   ToastNotice { id: toastNotice }

   Connections {
       target: projectController
       function onSourcesRescanned(added, unavailableSources) {
           if (added > 0)
               toastNotice.show(added === 1 ? "1 repository added from a source"
                                            : added + " repositories added from sources")
       }
   }
   ```

7. `ui/qml/Main.qml` — surface batch results next to the existing `repoAddFailed` handling:
   ```qml
   Connections {
       target: projectController
       function onReposAdded(added, failures) {
           if (failures.length > 0) {
               // Reuse the existing add-failure dialog rather than inventing a
               // second error surface.
               fetchErrorDialog.failures = failures
               fetchErrorDialog.open()
           }
       }
   }
   ```
   > If `fetchErrorDialog` does not expose a `failures` property, follow the shape
   > it already uses for `fleetFetchFailed` and pass the list the same way.

- [x] **Step 5: Run the test to verify it passes**

Run: `cmake --build build --parallel && ./build/tests/gittide_ui_tests`
Expected: PASS, and `TestQmlShell` still green (no new QML warnings).

- [x] **Step 6: Verify by hand**

Run the app, create/activate a project, choose *Add repositories from folder…*, point it at a folder holding two repos, confirm the checklist matches the folder, add them, and confirm both appear in the sidebar.

- [x] **Step 7: Commit**

```bash
git add ui/qml tests/ui/test_qml_add_from_folder.cpp tests/ui/main.cpp tests/CMakeLists.txt
git commit -m "feat(ui): add-repositories-from-folder dialog"
```

---

## Task 8: QML — Sources section in Project Options

**Files:**
- Modify: `ui/qml/ProjectOptionsDialog.qml`, `ui/include/gittide/ui/projectcontroller.hpp`, `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp`

**Interfaces consumed:** `removeSource`, `clearIgnored` (Task 3), `rescanSources` (Task 6).

**Interfaces produced:**
```cpp
Q_INVOKABLE QVariantList ProjectController::activeProjectSources() const; // { path, maxDepth, ignoredCount, available }
Q_INVOKABLE void ProjectController::removeSource(const QString& path);
Q_INVOKABLE void ProjectController::clearIgnoredForSource(const QString& path);
```

- [x] **Step 1: Write the failing test** — add to `TestProjectController`:

```cpp
    void activeProjectSources_reports_paths_depth_ignores_and_availability()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        store.projects()[0].sources.push_back(
            gittide::RepoSource{.path = root.generic_string(), .maxDepth = 3, .ignored = {"x"}});
        store.projects()[0].sources.push_back(gittide::RepoSource{.path = "/definitely/not/here", .maxDepth = 1});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        const QVariantList rows = controller.activeProjectSources();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value("maxDepth").toInt(), 3);
        QCOMPARE(rows.at(0).toMap().value("ignoredCount").toInt(), 1);
        QCOMPARE(rows.at(0).toMap().value("available").toBool(), true);
        QCOMPARE(rows.at(1).toMap().value("available").toBool(), false);
    }

    void removeSource_and_clearIgnoredForSource_mutate_the_store()
    {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        store.projects()[0].sources.push_back(
            gittide::RepoSource{.path = "/home/u/projects", .maxDepth = 2, .ignored = {"/home/u/projects/x"}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        controller.clearIgnoredForSource(QStringLiteral("/home/u/projects"));
        QCOMPARE(static_cast<int>(store.projects()[0].sources[0].ignored.size()), 0);

        controller.removeSource(QStringLiteral("/home/u/projects"));
        QCOMPARE(static_cast<int>(store.projects()[0].sources.size()), 0);
    }
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel` — expected: FAIL, `no member named 'activeProjectSources'`.

- [x] **Step 3: Declare the API**

In `public slots:`, next to `activeProjectRepos`:

```cpp
    /// The active project's sources as { path, maxDepth, ignoredCount, available }
    /// maps for the Project Options dialog. `available` is false when the folder
    /// no longer exists. Empty when no project is active.
    Q_INVOKABLE QVariantList activeProjectSources() const;
    /// Unregister a source. The repositories it added stay in the project.
    Q_INVOKABLE void removeSource(const QString& path);
    /// Empty one source's ignore list, so its next scan offers everything again.
    Q_INVOKABLE void clearIgnoredForSource(const QString& path);
```

- [x] **Step 4: Implement them**

```cpp
QVariantList ProjectController::activeProjectSources() const
{
    QVariantList rows;
    if (m_activeId.isEmpty())
        return rows;

    for (const auto& p : m_store->projects())
    {
        if (QString::fromStdString(p.id) != m_activeId)
            continue;
        for (const auto& s : p.sources)
        {
            std::error_code ec;
            const bool      available = std::filesystem::is_directory(std::filesystem::path(s.path), ec) && !ec;
            rows.append(QVariantMap{{QStringLiteral("path"), QString::fromStdString(s.path)},
                                    {QStringLiteral("maxDepth"), s.maxDepth},
                                    {QStringLiteral("ignoredCount"), static_cast<int>(s.ignored.size())},
                                    {QStringLiteral("available"), available}});
        }
        break;
    }
    return rows;
}

void ProjectController::removeSource(const QString& path)
{
    if (m_activeId.isEmpty())
        return;
    if (m_store->removeSource(m_activeId.toStdString(), path.toStdString()))
        saveStore();
}

void ProjectController::clearIgnoredForSource(const QString& path)
{
    if (m_activeId.isEmpty())
        return;
    if (m_store->clearIgnored(m_activeId.toStdString(), path.toStdString()))
        saveStore();
}
```

- [x] **Step 5: Add the Sources section to the dialog**

In `ui/qml/ProjectOptionsDialog.qml`: extend `refresh()` with

```qml
        sources = (typeof projectController !== "undefined" && projectController)
                  ? projectController.activeProjectSources() : []
```

add the backing property next to `repos`:

```qml
    // Snapshot of the active project's repository sources: [{path,maxDepth,ignoredCount,available}].
    property var sources: []
```

and append this section to the `DialogColumn`, after the repositories block:

```qml
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

        // ---- Repository sources ----
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: "Repository sources"
                    color: theme.textSecondary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                AppButton {
                    objectName: "rescanSourcesButton"
                    variant: "secondary"
                    text: "Rescan now"
                    visible: dialog.sources.length > 0
                    onClicked: if (projectController) projectController.rescanSources()
                }
            }

            Label {
                Layout.fillWidth: true
                visible: dialog.sources.length === 0
                text: "No sources. Add one from “Add repositories from folder…”."
                color: theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: dialog.sources
                delegate: RowLayout {
                    id: sourceRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: sourceRow.modelData.path
                            color: theme.textPrimary
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                        }
                        Label {
                            Layout.fillWidth: true
                            text: sourceRow.modelData.available
                                  ? ("depth " + sourceRow.modelData.maxDepth
                                     + " · " + sourceRow.modelData.ignoredCount + " ignored")
                                  : "folder not found"
                            color: sourceRow.modelData.available ? theme.textMuted : theme.stateDeleted
                            font.pixelSize: 11
                        }
                    }
                    AppButton {
                        variant: "secondary"
                        text: "Clear ignored"
                        enabled: sourceRow.modelData.ignoredCount > 0
                        onClicked: {
                            if (projectController)
                                projectController.clearIgnoredForSource(sourceRow.modelData.path)
                            dialog.refresh()
                        }
                    }
                    AppButton {
                        variant: "danger"
                        text: "Remove"
                        onClicked: {
                            if (projectController)
                                projectController.removeSource(sourceRow.modelData.path)
                            dialog.refresh()
                        }
                    }
                }
            }
        }
```

- [x] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ./build/tests/gittide_ui_tests`
Expected: PASS.

- [x] **Step 7: Verify by hand**

Register a source via the new dialog, open Project Options, confirm the source is listed with its depth; remove a repo from the project, reopen Project Options and confirm the ignored count went up; hit *Clear ignored* then *Rescan now* and confirm the repo comes back.

- [x] **Step 8: Commit**

```bash
git add ui/qml/ProjectOptionsDialog.qml ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): manage repository sources in project options"
```

---

## Task 9: Close-out — spec, wish, plan

**Files:**
- Modify: `docs/spec/product/product.md`, `docs/spec/engineering/engineering.md`, `docs/spec/design/design.md`, `docs/wishlist/bulk-add-projects.md`, `docs/wishlist/index.md`, this plan
- Move: `docs/wishlist/bulk-add-projects.md` → `docs/wishlist/shipped/bulk-add-projects.md`

- [x] **Step 1: Run the whole suite**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: everything green. Do not proceed until it is.

- [x] **Step 2: Update the living spec**

- `spec/product`: the bulk-add flow — pick folder → depth → checklist → add, and what a registered source does (auto-add on activation, removal is permanent).
- `spec/engineering`: `scanForRepos` and the `RepoSource` model, the `"sources"` key in `projects.json` (additive, still version 1), and the batch rule — **one save and one model refresh per batch/pass, never per repo**.
- `spec/design`: the add-from-folder dialog in the dialog inventory, and the Sources section of Project Options.

Symbol-level facts stay in the Doxygen comments written in Tasks 1–8 — do not restate them in the spec.

- [x] **Step 3: Close the wish**

In `docs/wishlist/bulk-add-projects.md`: set **Status** to `done`, add **Shipped** `2026-08-05`, and fill the graduation footer with links to the spec sections and this plan. Note explicitly that scan depth became configurable (default 2) rather than direct-children-only, and that repository sources were added beyond the original wish. Move the file to `docs/wishlist/shipped/` and move its row to the Shipped table in `docs/wishlist/index.md`, fixing the link.

- [x] **Step 4: Fill in this plan's Outcome and flip its Status to `done`.**

- [x] **Step 5: Commit**

```bash
git add docs
git commit -m "docs: close the bulk-add-repositories wish"
```

---

## Outcome

- **Shipped:** Add many existing repositories in one action by scanning a
  folder — pick folder → depth → checklist → add, with already-added repos
  shown disabled rather than hidden — and, going beyond the original wish,
  register that folder as a **repository source** that is rescanned
  automatically on every project activation so repositories that appear there
  later join the project without revisiting the dialog. Removing a repo that
  came from a source is permanent across rescans. Sources are inspected,
  rescanned on demand, and removed from a new Sources section in Project
  Options. Scan depth is a configurable stepper (default 2), not the
  direct-children-only cut the original wish scoped to. Also fixed a
  pre-existing bug along the way: folder-picker URLs now convert via
  `QUrl::toLocalFile()` instead of string surgery, so a path containing a space
  is no longer stored percent-encoded.
- **Spec updated:**
  [`spec/product#bulk-add--repository-sources`](../spec/product/product.md#bulk-add--repository-sources)
  (the flow and what a source does), plus a note in
  [`spec/product#data--persistence-what-is-stored-and-where`](../spec/product/product.md#data--persistence-what-is-stored-and-where);
  [`spec/engineering#bulk-add-folder-scan-and-repository-sources`](../spec/engineering/engineering.md#bulk-add-folder-scan-and-repository-sources)
  (`scanForRepos`, `RepoSource`, the additive `"sources"` key, `addRepos`'s
  unconditional single-save/single-refresh batch rule and `rescanSources`'s
  conditional (only-when-it-added-something) counterpart, and its
  single-flight / stale-pass-abandonment shape); and two additions to
  [`spec/design`](../spec/design/design.md#components) (the add-from-folder
  dialog + toast notice in the dialog inventory, and the Project Options
  Sources section).
- **Code:** `core/include/gittide/reposcan.hpp` + `core/src/reposcan.cpp`
  (`scanForRepos`, `ScanOptions`); `RepoSource` and `Project::sources` plus the
  `addSource`/`removeSource`/`ignoreInSources`/`clearIgnored` mutators in
  `core/include/gittide/projectstore.hpp` + `core/src/projectstore.cpp`;
  `ProjectController::scanFolder`/`addRepos`/`rescanSources`/
  `activeProjectSources`/`removeSource`/`clearIgnoredForSource`/
  `localPathFromUrl` in `ui/include/gittide/ui/projectcontroller.hpp` +
  `ui/src/projectcontroller.cpp`; `ui/qml/AddFromFolderDialog.qml` and
  `ui/qml/ToastNotice.qml` (new), plus entry points wired into
  `EmptyState.qml`, `Sidebar.qml`, `WorkingPane.qml`, `Main.qml`, and a new
  Sources section in `ProjectOptionsDialog.qml`.
