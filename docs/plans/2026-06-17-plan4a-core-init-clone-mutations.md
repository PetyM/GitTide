# Plan 4a — Core: GitRepo::init / GitRepo::clone / ProjectStore Mutations

| | |
|--|--|
| **Date** | 2026-06-17 |
| **Status** | `done` |
| **Spec** | [engineering](../spec/engineering/engineering.md) · [product](../spec/product/product.md) |
| **Depends on** | [Core foundation](2026-06-16-core-foundation.md) |

**Goal:** Extend the Core library with `GitRepo::init`, `GitRepo::clone` (with cancellable progress callback), `ProjectStore::createProject`, and `ProjectStore::addRepo` with duplicate-path rejection.

**Architecture:** All changes live in `core/` — pure C++23, Qt-free, tested with Catch2. `GitRepo` gains two static factory methods (mirroring the existing `open`). `ProjectStore` gains two mutation helpers that operate on the in-memory model; callers are responsible for persistence (call `save()` after mutating). All new methods follow the existing `Expected<T>` = `std::expected<T, GitError>` error convention.

**Tech Stack:** C++23, libgit2 (via `gitgui_core`), Catch2.

---

## Background the implementer needs

**Core API before this plan** (`core/include/gitgui/GitRepo.hpp`):
```cpp
static Expected<GitRepo> open(const std::filesystem::path& path);
Expected<std::vector<FileStatus>> status() const;
Expected<DiffResult> diff(DiffTarget target, const std::filesystem::path& file) const;
Expected<void> stage(const StageSelection& sel);
Expected<void> unstage(const StageSelection& sel);
Expected<void> discard(const StageSelection& sel);
Expected<std::string> commit(const CommitRequest& req);
```

**Core API before this plan** (`core/include/gitgui/ProjectStore.hpp`):
```cpp
struct RepoRef { std::string path; std::string alias; };
struct Project { std::string id; std::string name;
                 std::vector<RepoRef> repos; std::string lastActiveRepo; };
class ProjectStore {
    std::vector<Project>& projects();
    const std::string& activeProject() const;
    void setActiveProject(std::string id);
    std::string to_json() const;
    static Expected<ProjectStore> from_json(const std::string& json);
    Expected<void> save(const std::filesystem::path& file) const;
    static Expected<ProjectStore> load(const std::filesystem::path& file);
};
```

**Error type**: `Expected<T>` = `std::expected<T, GitError>` where `GitError { int code; std::string message; }`. Use `last_git_error(rc)` (from `gitgui/GitError.hpp`) immediately after a failing libgit2 call.

**PathUtil helpers** (`gitgui/PathUtil.hpp`):
- `to_git_path(path)` — converts `std::filesystem::path` to UTF-8 forward-slash string for libgit2.
- `from_git_path(sv)` — converts libgit2's UTF-8 path back to `std::filesystem::path`.

**Test patterns:**
- `#include "support/TempRepo.hpp"` + `gitgui::test::TempRepo tmp;` — creates a temp git repo; also owns a `LibGit2Context` (libgit2 init/shutdown).
- `tmp.path()` — absolute path to the repo root.
- `tmp.write_file(rel, contents)` — creates/overwrites a file.
- `tmp.commit_all(message)` — stages all changes and commits with test identity.
- `tmp.set_identity(name, email)` — writes `user.name`/`user.email` into the repo's config.
- Tests that use libgit2 but do NOT create a `TempRepo` must declare `gitgui::LibGit2Context ctx;` (from `gitgui/LibGit2Context.hpp`) to ensure libgit2 is initialized.

**Build / test commands:**
```bash
cmake --build build -j                                             # build everything
ctest --test-dir build -R gitgui_core_tests --output-on-failure   # Core tests only
ctest --test-dir build --output-on-failure                         # full suite
```

---

## File Structure

**Modified:**
- `core/include/gitgui/GitRepo.hpp` — add `ProgressCallback` typedef, `init()`, `clone()` declarations.
- `core/src/GitRepo.cpp` — implement `init()`, `clone()`.
- `core/include/gitgui/ProjectStore.hpp` — add `createProject()`, `addRepo()` declarations.
- `core/src/ProjectStore.cpp` — implement `createProject()`, `addRepo()`.
- `tests/test_project_store.cpp` — append mutation test cases.

**Created:**
- `tests/test_git_repo_init_clone.cpp` — Catch2 tests for `init` and `clone`.

**Modified (build):**
- `tests/CMakeLists.txt` — add `test_git_repo_init_clone.cpp` to `gitgui_core_tests`.

---

## Task 1: `GitRepo::init`

Initialize a new git repository at a given path. Rejects paths that already contain `.git`.

**Files:**
- Modify: `core/include/gitgui/GitRepo.hpp`
- Modify: `core/src/GitRepo.cpp`
- Create: `tests/test_git_repo_init_clone.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `static Expected<GitRepo> GitRepo::init(const std::filesystem::path& path)` — non-bare init; errors if `.git` already exists at path.

- [ ] **Step 1: Write the failing test**

Create `tests/test_git_repo_init_clone.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "gitgui/LibGit2Context.hpp"
#include "support/TempRepo.hpp"
#include <filesystem>
#include <random>

namespace {
std::filesystem::path unique_empty_dir() {
    std::mt19937_64 rng{std::random_device{}()};
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui_init_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}
}  // namespace

TEST_CASE("GitRepo::init creates a valid repository in an empty directory", "[git_repo][init]") {
    gitgui::LibGit2Context ctx;
    auto dir = unique_empty_dir();

    auto result = gitgui::GitRepo::init(dir);
    REQUIRE(result.has_value());

    auto opened = gitgui::GitRepo::open(dir);
    REQUIRE(opened.has_value());

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitRepo::init rejects a path that already has a .git directory", "[git_repo][init]") {
    gitgui::test::TempRepo existing;  // TempRepo owns LibGit2Context

    auto result = gitgui::GitRepo::init(existing.path());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(!result.error().message.empty());
}
```

- [ ] **Step 2: Register the test file in the build**

In `tests/CMakeLists.txt`, add `test_git_repo_init_clone.cpp` to the `add_executable(gitgui_core_tests ...)` source list (after `test_git_repo_discard.cpp`):

```cmake
  test_git_repo_init_clone.cpp
```

- [ ] **Step 3: Run — verify FAIL to compile**

```bash
cmake --build build -j 2>&1 | tail -10
```

Expected: compile error — `GitRepo` has no `init`.

- [ ] **Step 4: Declare `init` in the header**

In `core/include/gitgui/GitRepo.hpp`, add after the `open` declaration:

```cpp
    // Initialise a new non-bare repository at path. Creates path if absent.
    // Errors if a .git directory already exists at path.
    static Expected<GitRepo> init(const std::filesystem::path& path);
```

- [ ] **Step 5: Implement `init` in `GitRepo.cpp`**

Add after the `GitRepo::open` implementation:

```cpp
Expected<GitRepo> GitRepo::init(const std::filesystem::path& path) {
    if (std::filesystem::exists(path / ".git"))
        return std::unexpected(GitError{-1, "repository already exists at " +
                                             path.generic_string()});
    git_repository* repo = nullptr;
    int rc = git_repository_init(&repo, to_git_path(path).c_str(), /*is_bare=*/0);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    return GitRepo(repo);
}
```

- [ ] **Step 6: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_core_tests --output-on-failure
```

Expected: PASS — both `init` test cases pass, no regressions.

- [ ] **Step 7: Commit**

```bash
git add core/include/gitgui/GitRepo.hpp core/src/GitRepo.cpp \
        tests/test_git_repo_init_clone.cpp tests/CMakeLists.txt
git commit -m "feat(core): GitRepo::init creates a new git repository"
```

---

## Task 2: `GitRepo::clone` with progress callback

Clone a repository from a URL into a destination path, reporting transfer progress and supporting cancellation.

**Files:**
- Modify: `core/include/gitgui/GitRepo.hpp`
- Modify: `core/src/GitRepo.cpp`
- Modify: `tests/test_git_repo_init_clone.cpp`

**Interfaces:**
- Consumes: `GitRepo::init` (used in tests to create a source repo).
- Produces:
  - `gitgui::ProgressCallback` = `std::function<bool(unsigned received, unsigned total)>` — free type alias in namespace `gitgui`. Return `true` = continue, `false` = cancel.
  - `static Expected<GitRepo> GitRepo::clone(const std::string& url, const std::filesystem::path& dest, ProgressCallback cb)` — clones url into dest; calls `cb` during transfer; returns error if cb returns false or clone fails. dest must not exist yet.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_git_repo_init_clone.cpp`:

```cpp
TEST_CASE("GitRepo::clone from file:// produces a working repo and invokes callback",
          "[git_repo][clone]") {
    gitgui::test::TempRepo source;
    source.set_identity("Test", "t@t.test");
    source.write_file("README.md", "hello\n");
    source.commit_all("initial");

    auto dest = unique_empty_dir();
    std::filesystem::remove_all(dest);  // clone creates dest itself

    int progress_calls = 0;
    gitgui::ProgressCallback cb = [&](unsigned, unsigned) {
        ++progress_calls;
        return true;  // continue
    };
    auto result = gitgui::GitRepo::clone(
        "file://" + source.path().generic_string(), dest, std::move(cb));

    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(dest / "README.md"));
    REQUIRE(progress_calls > 0);

    std::filesystem::remove_all(dest);
}

TEST_CASE("GitRepo::clone aborts when callback returns false", "[git_repo][clone]") {
    gitgui::test::TempRepo source;
    source.set_identity("Test", "t@t.test");
    source.write_file("a.txt", "data\n");
    source.commit_all("initial");

    auto dest = unique_empty_dir();
    std::filesystem::remove_all(dest);

    gitgui::ProgressCallback cb = [](unsigned, unsigned) { return false; };  // cancel
    auto result = gitgui::GitRepo::clone(
        "file://" + source.path().generic_string(), dest, std::move(cb));

    REQUIRE_FALSE(result.has_value());

    std::filesystem::remove_all(dest);
}

TEST_CASE("GitRepo::clone into a missing URL returns an error", "[git_repo][clone]") {
    gitgui::LibGit2Context ctx;
    auto dest = unique_empty_dir();
    std::filesystem::remove_all(dest);

    gitgui::ProgressCallback cb = [](unsigned, unsigned) { return true; };
    auto result = gitgui::GitRepo::clone("/no/such/gitgui-clone-src", dest, std::move(cb));

    REQUIRE_FALSE(result.has_value());
    std::filesystem::remove_all(dest);
}
```

- [ ] **Step 2: Run — verify FAIL to compile**

```bash
cmake --build build -j 2>&1 | tail -10
```

Expected: compile error — `ProgressCallback` and `GitRepo::clone` not declared.

- [ ] **Step 3: Add `ProgressCallback` and `clone` to the header**

In `core/include/gitgui/GitRepo.hpp`, add two new includes (after `#include <vector>`):

```cpp
#include <functional>
#include <string>
```

Then add in namespace `gitgui` (before the `GitRepo` class declaration):

```cpp
// Callback invoked during a clone transfer: (received_objects, total_objects).
// Return true to continue, false to cancel (clone returns an error).
using ProgressCallback = std::function<bool(unsigned received, unsigned total)>;
```

Then add inside the `GitRepo` class (after the `init` declaration):

```cpp
    // Clone the repository at url into dest. Calls cb during the transfer.
    // dest must not exist (libgit2 creates it). Returns error on failure or cancel.
    static Expected<GitRepo> clone(const std::string& url,
                                   const std::filesystem::path& dest,
                                   ProgressCallback cb);
```

- [ ] **Step 4: Implement `clone` in `GitRepo.cpp`**

Add a file-scope trampoline inside the existing anonymous namespace (the one that already contains `is_whole_file` and `map_status`):

```cpp
int transfer_progress_trampoline(const git_indexer_progress* stats, void* payload) {
    auto* cb = static_cast<gitgui::ProgressCallback*>(payload);
    return (*cb)(stats->received_objects, stats->total_objects) ? 0 : -1;
}
```

Then add the `clone` implementation after `GitRepo::init`:

```cpp
Expected<GitRepo> GitRepo::clone(const std::string& url,
                                  const std::filesystem::path& dest,
                                  ProgressCallback cb) {
    git_clone_options opts = GIT_CLONE_OPTIONS_INIT;
    opts.fetch_opts.callbacks.transfer_progress = transfer_progress_trampoline;
    opts.fetch_opts.callbacks.payload = &cb;  // cb lives on the stack for the duration

    git_repository* repo = nullptr;
    int rc = git_clone(&repo, url.c_str(), to_git_path(dest).c_str(), &opts);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    return GitRepo(repo);
}
```

- [ ] **Step 5: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_core_tests --output-on-failure
```

Expected: PASS — all five init/clone test cases pass, no regressions.

- [ ] **Step 6: Commit**

```bash
git add core/include/gitgui/GitRepo.hpp core/src/GitRepo.cpp \
        tests/test_git_repo_init_clone.cpp
git commit -m "feat(core): GitRepo::clone with cancellable ProgressCallback"
```

---

## Task 3: `ProjectStore::createProject`

Append a new `Project` with a randomly-generated unique ID.

**Files:**
- Modify: `core/include/gitgui/ProjectStore.hpp`
- Modify: `core/src/ProjectStore.cpp`
- Modify: `tests/test_project_store.cpp`

**Interfaces:**
- Produces: `Project& ProjectStore::createProject(const std::string& name)` — appends a new `Project` with a 128-bit random hex `id` and the given `name`, returns a reference to it. Callers call `save()` to persist.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_project_store.cpp` (after all existing test cases; the file already defines `temp_json_path()` in an anonymous namespace — use it here):

```cpp
TEST_CASE("createProject appends a project with unique id and given name", "[store][mutations]") {
    gitgui::ProjectStore store;
    auto& p1 = store.createProject("Alpha");
    auto& p2 = store.createProject("Beta");

    REQUIRE(store.projects().size() == 2);
    REQUIRE(p1.name == "Alpha");
    REQUIRE(p2.name == "Beta");
    REQUIRE(!p1.id.empty());
    REQUIRE(!p2.id.empty());
    REQUIRE(p1.id != p2.id);
}

TEST_CASE("createProject persists via save/load round-trip", "[store][mutations]") {
    auto path = temp_json_path();
    gitgui::ProjectStore store;
    store.createProject("MyProject");
    REQUIRE(store.save(path).has_value());

    auto loaded = gitgui::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "MyProject");
    REQUIRE(!loaded->projects()[0].id.empty());

    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Run — verify FAIL to compile**

```bash
cmake --build build -j 2>&1 | tail -10
```

Expected: compile error — `ProjectStore` has no `createProject`.

- [ ] **Step 3: Declare `createProject` in the header**

In `core/include/gitgui/ProjectStore.hpp`, add to the public section (before `to_json()`):

```cpp
    // Append a new Project with a random unique id and the given name.
    // Returns a reference to the newly created project.
    // Call save() after mutating to persist the change.
    Project& createProject(const std::string& name);
```

- [ ] **Step 4: Implement `createProject` in `ProjectStore.cpp`**

Add to `ProjectStore.cpp` after the existing includes:

```cpp
#include <iomanip>
#include <random>
#include <sstream>
```

Add the implementation before the closing `}  // namespace gitgui`:

```cpp
Project& ProjectStore::createProject(const std::string& name) {
    std::mt19937_64 gen{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(16) << dist(gen)
        << std::setw(16) << dist(gen);
    Project p;
    p.id = oss.str();
    p.name = name;
    projects_.push_back(std::move(p));
    return projects_.back();
}
```

- [ ] **Step 5: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_core_tests --output-on-failure
```

Expected: PASS — both new test cases pass, no regressions.

- [ ] **Step 6: Commit**

```bash
git add core/include/gitgui/ProjectStore.hpp core/src/ProjectStore.cpp \
        tests/test_project_store.cpp
git commit -m "feat(core): ProjectStore::createProject with random unique id"
```

---

## Task 4: `ProjectStore::addRepo`

Insert a `RepoRef` into a named project, rejecting duplicate paths.

**Files:**
- Modify: `core/include/gitgui/ProjectStore.hpp`
- Modify: `core/src/ProjectStore.cpp`
- Modify: `tests/test_project_store.cpp`

**Interfaces:**
- Consumes: `Project& ProjectStore::createProject(...)` (Task 3 — used in tests to get a valid project id).
- Produces: `Expected<void> ProjectStore::addRepo(const std::string& projectId, RepoRef repo)` — finds project by id, checks for duplicate `path`, appends `RepoRef`. Returns error if project not found or path already exists in that project.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_project_store.cpp`:

```cpp
TEST_CASE("addRepo inserts a repo into the project", "[store][mutations]") {
    gitgui::ProjectStore store;
    auto& p = store.createProject("Work");
    auto result = store.addRepo(p.id, gitgui::RepoRef{"/home/u/api", "api"});
    REQUIRE(result.has_value());
    REQUIRE(store.projects()[0].repos.size() == 1);
    REQUIRE(store.projects()[0].repos[0].path == "/home/u/api");
    REQUIRE(store.projects()[0].repos[0].alias == "api");
}

TEST_CASE("addRepo rejects a duplicate path within the same project", "[store][mutations]") {
    gitgui::ProjectStore store;
    auto& p = store.createProject("Work");
    REQUIRE(store.addRepo(p.id, gitgui::RepoRef{"/home/u/api", "api"}).has_value());

    auto dup = store.addRepo(p.id, gitgui::RepoRef{"/home/u/api", "api-copy"});
    REQUIRE_FALSE(dup.has_value());
    REQUIRE(!dup.error().message.empty());
    REQUIRE(store.projects()[0].repos.size() == 1);
}

TEST_CASE("addRepo returns error for unknown project id", "[store][mutations]") {
    gitgui::ProjectStore store;
    auto result = store.addRepo("no-such-id", gitgui::RepoRef{"/some/path", "r"});
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("addRepo round-trips through save/load", "[store][mutations]") {
    auto path = temp_json_path();
    gitgui::ProjectStore store;
    auto& p = store.createProject("Proj");
    store.addRepo(p.id, gitgui::RepoRef{"/srv/myrepo", "myrepo"});
    REQUIRE(store.save(path).has_value());

    auto loaded = gitgui::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects()[0].repos.size() == 1);
    REQUIRE(loaded->projects()[0].repos[0].alias == "myrepo");

    std::filesystem::remove(path);
}
```

- [ ] **Step 2: Run — verify FAIL to compile**

```bash
cmake --build build -j 2>&1 | tail -10
```

Expected: compile error — `ProjectStore` has no `addRepo`.

- [ ] **Step 3: Declare `addRepo` in the header**

In `core/include/gitgui/ProjectStore.hpp`, add after `createProject`:

```cpp
    // Add repo to the named project. Returns error if projectId is not found,
    // or if a repo with the same path already exists in that project.
    // Call save() after mutating to persist the change.
    Expected<void> addRepo(const std::string& projectId, RepoRef repo);
```

- [ ] **Step 4: Implement `addRepo` in `ProjectStore.cpp`**

Add `#include <algorithm>` to `ProjectStore.cpp` (after the existing includes).

Add the implementation before the closing `}  // namespace gitgui`:

```cpp
Expected<void> ProjectStore::addRepo(const std::string& projectId, RepoRef repo) {
    auto it = std::find_if(projects_.begin(), projects_.end(),
                           [&](const Project& p) { return p.id == projectId; });
    if (it == projects_.end())
        return std::unexpected(GitError{-1, "project not found: " + projectId});

    for (const auto& existing : it->repos) {
        if (existing.path == repo.path)
            return std::unexpected(
                GitError{-1, "repository already in project: " + repo.path});
    }
    it->repos.push_back(std::move(repo));
    return {};
}
```

- [ ] **Step 5: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_core_tests --output-on-failure
```

Expected: PASS — all four new test cases pass, no regressions.

- [ ] **Step 6: Run the full suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass (Core Catch2 + UI QtTest).

- [ ] **Step 7: Commit**

```bash
git add core/include/gitgui/ProjectStore.hpp core/src/ProjectStore.cpp \
        tests/test_project_store.cpp
git commit -m "feat(core): ProjectStore::addRepo with duplicate-path rejection"
```

---

## Self-Review

**1. Spec coverage:**
- `GitRepo::init` — ✅ Task 1: creates valid repo, rejects existing `.git`.
- `GitRepo::clone` — ✅ Task 2: local `file://` clone, progress callback invoked, cancel aborts, missing URL errors.
- `ProjectStore::createProject` — ✅ Task 3: unique id, name stored, persistence round-trip.
- `ProjectStore::addRepo` — ✅ Task 4: happy path, dedup rejection, unknown project id, persistence round-trip.

**2. Placeholder scan:** No TBD or "similar to Task N" references. All code shown inline.

**3. Type consistency:**
- `gitgui::ProgressCallback` declared in Task 2 header, used in Task 2 tests — consistent.
- `Project&` returned from `createProject` (Task 3) is used in Task 4 tests via `.id` — consistent with `Project.id` field.
- `temp_json_path()` used in Tasks 3–4 is defined in the existing anonymous namespace in `test_project_store.cpp` — same file, accessible.

**Notes:**
- `ProgressCallback` sits in namespace `gitgui` (not nested in `GitRepo`) so callers write `gitgui::ProgressCallback`, not `gitgui::GitRepo::ProgressCallback`.
- `clone()` uses `file://` in tests to force libgit2's packfile (network) transport, which reliably calls `transfer_progress`. A bare path would use the local copy transport which may skip the callback.
- `addRepo` and `createProject` do NOT auto-save; the `ProjectController` (Plan 4b) calls `save()` after mutations. This keeps Core Qt-free and the `ProjectStore` path-agnostic.
- `init` checks only for `.git` at the exact target path. Detecting "inside an existing repo" (i.e., nested) is deferred — the spec's "unsafe target" requirement is satisfied by the existing-repo check.

---

## Outcome

Added `GitRepo::init` and `GitRepo::clone` (cancellable progress callback), plus `ProjectStore::createProject` and `addRepo` (duplicate-path rejection). Core-only.
