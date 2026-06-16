# Core Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a tested, Qt-free C++23 Core library for GitGUI: project scaffold, libgit2-backed `GitRepo` (open + status), path helpers, and JSON `ProjectStore`.

**Architecture:** A pure C++23 static library `gitgui_core` with no Qt dependency, linked against libgit2 (fetched via FetchContent). All logic returns `std::expected<T, GitError>` — no exceptions across boundaries. Tested with Catch2 against throwaway repositories created in-test under `std::filesystem::temp_directory_path()`.

**Tech Stack:** C++23, CMake ≥ 3.28, libgit2 (FetchContent), Catch2 v3 (FetchContent), `nlohmann/json` (FetchContent), `std::expected`, `std::filesystem`.

---

## File Structure

```
CMakeLists.txt                      # top-level: project, options, fetch deps, subdirs
cmake/Dependencies.cmake            # FetchContent declarations
core/CMakeLists.txt                 # gitgui_core static lib target
core/include/gitgui/GitError.hpp    # GitError struct + Expected alias
core/include/gitgui/PathUtil.hpp    # to_git_path / from_git_path
core/include/gitgui/GitRepo.hpp     # GitRepo class (RAII libgit2 wrapper)
core/include/gitgui/FileStatus.hpp  # FileStatus struct + StatusFlag
core/include/gitgui/ProjectStore.hpp# Project / ProjectStore model + persistence
core/src/PathUtil.cpp
core/src/GitRepo.cpp
core/src/ProjectStore.cpp
core/src/LibGit2Context.cpp         # global git_libgit2_init/shutdown RAII
core/include/gitgui/LibGit2Context.hpp
tests/CMakeLists.txt                # gitgui_core_tests target
tests/test_path_util.cpp
tests/test_git_repo.cpp
tests/test_project_store.cpp
tests/support/TempRepo.hpp          # helper: create throwaway repo in-test
tests/support/TempRepo.cpp
```

Header/source split keeps each unit focused. Tests mirror the unit they cover. `TempRepo` test helper is shared infrastructure for repo-touching tests.

---

## Task 1: CMake scaffold + Catch2 + first passing test

Verifies the build/test pipeline before any real logic.

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/Dependencies.cmake`
- Create: `core/CMakeLists.txt`
- Create: `core/include/gitgui/Version.hpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_smoke.cpp`

- [ ] **Step 1: Write the failing test**

`tests/test_smoke.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/Version.hpp"

TEST_CASE("core version string is non-empty", "[smoke]") {
    REQUIRE_FALSE(gitgui::kVersion.empty());
}
```

- [ ] **Step 2: Create the build files**

`cmake/Dependencies.cmake`:
```cmake
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)
```

`CMakeLists.txt` (top-level):
```cmake
cmake_minimum_required(VERSION 3.28)
project(gitgui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GITGUI_BUILD_TESTS "Build tests" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(core)

if(GITGUI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
```

`core/CMakeLists.txt`:
```cmake
add_library(gitgui_core STATIC
  # sources added in later tasks
)
target_include_directories(gitgui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gitgui_core PUBLIC cxx_std_23)

# Header-only for now; add a dummy source so the static lib links.
target_sources(gitgui_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/_placeholder.cpp)
```

Create `core/src/_placeholder.cpp`:
```cpp
// Intentionally empty; removed once real sources are added in Task 3.
namespace { [[maybe_unused]] int gitgui_core_placeholder = 0; }
```

`core/include/gitgui/Version.hpp`:
```cpp
#pragma once
#include <string_view>

namespace gitgui {
inline constexpr std::string_view kVersion = "0.1.0";
}
```

`tests/CMakeLists.txt`:
```cmake
add_executable(gitgui_core_tests
  test_smoke.cpp
)
target_link_libraries(gitgui_core_tests PRIVATE gitgui_core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(gitgui_core_tests)
```

- [ ] **Step 3: Configure and verify the test fails to build, then passes after files exist**

Run:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
Expected: builds successfully (all files present).

- [ ] **Step 4: Run the test**

Run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed`, the `[smoke]` test passes.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt cmake/ core/ tests/
git commit -m "build: scaffold CMake project with Catch2 and smoke test"
```

---

## Task 2: FetchContent libgit2 + RAII init context

**Files:**
- Modify: `cmake/Dependencies.cmake`
- Modify: `core/CMakeLists.txt`
- Create: `core/include/gitgui/LibGit2Context.hpp`
- Create: `core/src/LibGit2Context.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_libgit2.cpp`

- [ ] **Step 1: Write the failing test**

`tests/test_libgit2.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/LibGit2Context.hpp"
#include <git2.h>

TEST_CASE("libgit2 initializes and reports a version", "[libgit2]") {
    gitgui::LibGit2Context ctx;  // RAII init
    int major = 0, minor = 0, rev = 0;
    git_libgit2_version(&major, &minor, &rev);
    REQUIRE(major >= 1);
}
```

- [ ] **Step 2: Add libgit2 to dependencies**

Append to `cmake/Dependencies.cmake`:
```cmake
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)        # don't build libgit2's own tests
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(USE_SSH OFF CACHE BOOL "" FORCE)            # enable later when network ops land
FetchContent_Declare(
  libgit2
  GIT_REPOSITORY https://github.com/libgit2/libgit2.git
  GIT_TAG        v1.8.1
)
FetchContent_MakeAvailable(libgit2)
```

- [ ] **Step 3: Link libgit2 and add the RAII context**

Append to `core/CMakeLists.txt` `target_link_libraries`:
```cmake
target_link_libraries(gitgui_core PUBLIC libgit2package)
```
> Note: libgit2 v1.8 exposes the target `libgit2package` (static) via FetchContent. If the configure step reports an unknown target, list available targets with `cmake --build build --target help` and use the `libgit2`-prefixed library target it provides.

`core/include/gitgui/LibGit2Context.hpp`:
```cpp
#pragma once

namespace gitgui {

// RAII wrapper around git_libgit2_init / git_libgit2_shutdown.
// Construct once near program start (and once per test that uses libgit2).
class LibGit2Context {
public:
    LibGit2Context();
    ~LibGit2Context();
    LibGit2Context(const LibGit2Context&) = delete;
    LibGit2Context& operator=(const LibGit2Context&) = delete;
};

}  // namespace gitgui
```

`core/src/LibGit2Context.cpp`:
```cpp
#include "gitgui/LibGit2Context.hpp"
#include <git2.h>

namespace gitgui {
LibGit2Context::LibGit2Context()  { git_libgit2_init(); }
LibGit2Context::~LibGit2Context() { git_libgit2_shutdown(); }
}  // namespace gitgui
```

Add to `core/CMakeLists.txt` `target_sources` (and delete the placeholder line + file):
```cmake
target_sources(gitgui_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/LibGit2Context.cpp
)
```

Add to `tests/CMakeLists.txt` `add_executable` source list: `test_libgit2.cpp`.

- [ ] **Step 4: Build and run the test**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R libgit2
```
Expected: `[libgit2]` test passes; `major >= 1`.

- [ ] **Step 5: Commit**

```bash
git rm core/src/_placeholder.cpp
git add cmake/Dependencies.cmake core/ tests/
git commit -m "build: fetch and link libgit2 with RAII init context"
```

---

## Task 3: Path helpers (to_git_path / from_git_path)

Enforces the spec's path-handling rule in one place.

**Files:**
- Create: `core/include/gitgui/PathUtil.hpp`
- Create: `core/src/PathUtil.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_path_util.cpp`

- [ ] **Step 1: Write the failing test**

`tests/test_path_util.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/PathUtil.hpp"
#include <filesystem>

TEST_CASE("to_git_path produces forward-slash UTF-8", "[path]") {
    std::filesystem::path p = std::filesystem::path("a") / "b c" / "ünïcode.txt";
    std::string g = gitgui::to_git_path(p);
    REQUIRE(g.find('\\') == std::string::npos);   // no backslashes even on Windows
    REQUIRE(g.find("b c") != std::string::npos);   // spaces preserved verbatim
    REQUIRE(g.find("ünïcode.txt") != std::string::npos);
}

TEST_CASE("from_git_path round-trips to_git_path", "[path]") {
    std::filesystem::path p = std::filesystem::path("dir") / "ünïcode.txt";
    REQUIRE(gitgui::from_git_path(gitgui::to_git_path(p)) ==
            p.lexically_normal());
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `test_path_util.cpp` to `tests/CMakeLists.txt`, then:
```bash
cmake --build build
```
Expected: FAIL — `gitgui/PathUtil.hpp` not found.

- [ ] **Step 3: Write the implementation**

`core/include/gitgui/PathUtil.hpp`:
```cpp
#pragma once
#include <filesystem>
#include <string>

namespace gitgui {

// Convert a filesystem path to the form libgit2 expects:
// UTF-8 bytes with forward-slash separators. Use this at EVERY libgit2 call.
std::string to_git_path(const std::filesystem::path& p);

// Convert a UTF-8 forward-slash path (as returned by libgit2) back to a
// native std::filesystem::path.
std::filesystem::path from_git_path(std::string_view git_path);

}  // namespace gitgui
```

`core/src/PathUtil.cpp`:
```cpp
#include "gitgui/PathUtil.hpp"

namespace gitgui {

std::string to_git_path(const std::filesystem::path& p) {
    // generic_u8string() yields UTF-8 with '/' separators on every platform.
    auto u8 = p.generic_u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path from_git_path(std::string_view git_path) {
    std::u8string u8(git_path.begin(), git_path.end());
    return std::filesystem::path(u8).lexically_normal();
}

}  // namespace gitgui
```

Add `src/PathUtil.cpp` to `core/CMakeLists.txt` `target_sources`.

- [ ] **Step 4: Build and run the tests**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R path
```
Expected: both `[path]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add core/include/gitgui/PathUtil.hpp core/src/PathUtil.cpp core/CMakeLists.txt tests/
git commit -m "feat(core): add to_git_path/from_git_path with encoding rule"
```

---

## Task 4: GitError + Expected alias

**Files:**
- Create: `core/include/gitgui/GitError.hpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_git_error.cpp`

- [ ] **Step 1: Write the failing test**

`tests/test_git_error.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitError.hpp"

TEST_CASE("GitError carries code and message", "[error]") {
    gitgui::GitError e{-3, "not found"};
    REQUIRE(e.code == -3);
    REQUIRE(e.message == "not found");
}

TEST_CASE("Expected can hold a value or an error", "[error]") {
    gitgui::Expected<int> ok = 42;
    REQUIRE(ok.has_value());
    REQUIRE(*ok == 42);

    gitgui::Expected<int> bad = std::unexpected(gitgui::GitError{-1, "boom"});
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().message == "boom");
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `test_git_error.cpp` to `tests/CMakeLists.txt`, then:
```bash
cmake --build build
```
Expected: FAIL — `gitgui/GitError.hpp` not found.

- [ ] **Step 3: Write the implementation**

`core/include/gitgui/GitError.hpp`:
```cpp
#pragma once
#include <expected>
#include <string>

namespace gitgui {

struct GitError {
    int code = 0;          // libgit2 error code (git_error_code) or custom
    std::string message;   // human-readable detail
};

template <typename T>
using Expected = std::expected<T, GitError>;

// Build a GitError from the current libgit2 thread-local error after a failed call.
GitError last_git_error(int code);

}  // namespace gitgui
```

`core/src/GitError.cpp`:
```cpp
#include "gitgui/GitError.hpp"
#include <git2.h>

namespace gitgui {
GitError last_git_error(int code) {
    const git_error* e = git_error_last();
    return GitError{code, e && e->message ? e->message : "unknown libgit2 error"};
}
}  // namespace gitgui
```

Add `src/GitError.cpp` to `core/CMakeLists.txt` `target_sources`.

- [ ] **Step 4: Build and run the tests**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R error
```
Expected: both `[error]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add core/include/gitgui/GitError.hpp core/src/GitError.cpp core/CMakeLists.txt tests/
git commit -m "feat(core): add GitError and std::expected alias"
```

---

## Task 5: TempRepo test helper

Shared infrastructure: create a throwaway repository with commits for repo tests.

**Files:**
- Create: `tests/support/TempRepo.hpp`
- Create: `tests/support/TempRepo.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_temp_repo.cpp`

- [ ] **Step 1: Write the failing test**

`tests/test_temp_repo.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "support/TempRepo.hpp"
#include <filesystem>

TEST_CASE("TempRepo creates a real git repo on disk", "[support]") {
    gitgui::test::TempRepo repo;
    REQUIRE(std::filesystem::exists(repo.path() / ".git"));
}

TEST_CASE("TempRepo writes a file and commits it", "[support]") {
    gitgui::test::TempRepo repo;
    repo.write_file("hello.txt", "hi");
    repo.commit_all("first commit");
    REQUIRE(std::filesystem::exists(repo.path() / "hello.txt"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `test_temp_repo.cpp` and `support/TempRepo.cpp` to `tests/CMakeLists.txt`, then:
```bash
cmake --build build
```
Expected: FAIL — `support/TempRepo.hpp` not found.

- [ ] **Step 3: Write the implementation**

`tests/support/TempRepo.hpp`:
```cpp
#pragma once
#include <filesystem>
#include <string_view>
#include "gitgui/LibGit2Context.hpp"

struct git_repository;

namespace gitgui::test {

// Creates a unique temporary git repository under temp_directory_path().
// Removes the directory on destruction. Owns a LibGit2Context for its lifetime.
class TempRepo {
public:
    TempRepo();
    ~TempRepo();
    TempRepo(const TempRepo&) = delete;
    TempRepo& operator=(const TempRepo&) = delete;

    const std::filesystem::path& path() const { return dir_; }

    // Write (or overwrite) a file at a repo-relative path.
    void write_file(std::string_view rel_path, std::string_view contents);

    // Stage all changes and create a commit with a fixed test author.
    void commit_all(std::string_view message);

private:
    LibGit2Context ctx_;
    std::filesystem::path dir_;
    git_repository* repo_ = nullptr;
};

}  // namespace gitgui::test
```

`tests/support/TempRepo.cpp`:
```cpp
#include "support/TempRepo.hpp"
#include "gitgui/PathUtil.hpp"
#include <git2.h>
#include <fstream>
#include <random>
#include <stdexcept>

namespace gitgui::test {
namespace {
std::filesystem::path unique_dir() {
    std::random_device rd;
    auto base = std::filesystem::temp_directory_path();
    return base / ("gitgui_test_" + std::to_string(rd()));
}
void check(int rc, const char* what) {
    if (rc < 0) throw std::runtime_error(what);
}
}  // namespace

TempRepo::TempRepo() : dir_(unique_dir()) {
    std::filesystem::create_directories(dir_);
    check(git_repository_init(&repo_, to_git_path(dir_).c_str(), /*is_bare=*/0),
          "git_repository_init failed");
}

TempRepo::~TempRepo() {
    if (repo_) git_repository_free(repo_);
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
}

void TempRepo::write_file(std::string_view rel_path, std::string_view contents) {
    std::ofstream out(dir_ / std::filesystem::path(rel_path), std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

void TempRepo::commit_all(std::string_view message) {
    git_index* index = nullptr;
    check(git_repository_index(&index, repo_), "git_repository_index failed");
    check(git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr),
          "git_index_add_all failed");
    check(git_index_write(index), "git_index_write failed");

    git_oid tree_oid;
    check(git_index_write_tree(&tree_oid, index), "git_index_write_tree failed");
    git_tree* tree = nullptr;
    check(git_tree_lookup(&tree, repo_, &tree_oid), "git_tree_lookup failed");

    git_signature* sig = nullptr;
    check(git_signature_now(&sig, "Test", "test@example.com"),
          "git_signature_now failed");

    // Parent = current HEAD commit, if any.
    git_oid parent_oid;
    git_commit* parent = nullptr;
    const git_commit* parents[1] = {nullptr};
    size_t parent_count = 0;
    if (git_reference_name_to_id(&parent_oid, repo_, "HEAD") == 0) {
        git_commit_lookup(&parent, repo_, &parent_oid);
        parents[0] = parent;
        parent_count = 1;
    }

    git_oid commit_oid;
    check(git_commit_create(&commit_oid, repo_, "HEAD", sig, sig,
                            nullptr, std::string(message).c_str(), tree,
                            parent_count, parents),
          "git_commit_create failed");

    if (parent) git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
}

}  // namespace gitgui::test
```

Update `tests/CMakeLists.txt` so the test target can include `support/` and compiles the helper:
```cmake
target_include_directories(gitgui_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```
(Add `support/TempRepo.cpp` to the `add_executable` source list.)

- [ ] **Step 4: Build and run the tests**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R support
```
Expected: both `[support]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add tests/
git commit -m "test(core): add TempRepo helper for repo-backed tests"
```

---

## Task 6: GitRepo — open

**Files:**
- Create: `core/include/gitgui/GitRepo.hpp`
- Create: `core/src/GitRepo.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_git_repo.cpp`

- [ ] **Step 1: Write the failing test**

`tests/test_git_repo.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"

TEST_CASE("GitRepo::open succeeds on a real repo", "[repo]") {
    gitgui::test::TempRepo tmp;
    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
}

TEST_CASE("GitRepo::open fails on a non-repo directory", "[repo]") {
    auto repo = gitgui::GitRepo::open(std::filesystem::temp_directory_path());
    REQUIRE_FALSE(repo.has_value());
    REQUIRE(repo.error().code != 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Add `test_git_repo.cpp` to `tests/CMakeLists.txt`, then:
```bash
cmake --build build
```
Expected: FAIL — `gitgui/GitRepo.hpp` not found.

- [ ] **Step 3: Write the implementation**

`core/include/gitgui/GitRepo.hpp`:
```cpp
#pragma once
#include <filesystem>
#include <memory>
#include <vector>
#include "gitgui/GitError.hpp"
#include "gitgui/FileStatus.hpp"

struct git_repository;

namespace gitgui {

// RAII wrapper around a single libgit2 git_repository.
// Move-only. Not safe to share across threads; one owner per repo.
class GitRepo {
public:
    GitRepo(GitRepo&&) noexcept;
    GitRepo& operator=(GitRepo&&) noexcept;
    GitRepo(const GitRepo&) = delete;
    GitRepo& operator=(const GitRepo&) = delete;
    ~GitRepo();

    // Open an existing repository at (or above) the given path.
    static Expected<GitRepo> open(const std::filesystem::path& path);

    // Working-tree + index status (added in Task 7).
    Expected<std::vector<FileStatus>> status() const;

private:
    explicit GitRepo(git_repository* repo) : repo_(repo) {}
    git_repository* repo_ = nullptr;
};

}  // namespace gitgui
```

`core/include/gitgui/FileStatus.hpp`:
```cpp
#pragma once
#include <filesystem>
#include <cstdint>

namespace gitgui {

enum class StatusFlag : std::uint32_t {
    None        = 0,
    IndexNew    = 1 << 0,   // staged: new file
    IndexModified = 1 << 1, // staged: modified
    IndexDeleted  = 1 << 2, // staged: deleted
    WtNew       = 1 << 3,   // unstaged: untracked
    WtModified  = 1 << 4,   // unstaged: modified
    WtDeleted   = 1 << 5,   // unstaged: deleted
};

constexpr StatusFlag operator|(StatusFlag a, StatusFlag b) {
    return static_cast<StatusFlag>(static_cast<std::uint32_t>(a) |
                                   static_cast<std::uint32_t>(b));
}
constexpr StatusFlag& operator|=(StatusFlag& a, StatusFlag b) {
    a = a | b; return a;
}
constexpr bool has_flag(StatusFlag value, StatusFlag flag) {
    return (static_cast<std::uint32_t>(value) &
            static_cast<std::uint32_t>(flag)) != 0;
}

struct FileStatus {
    std::filesystem::path path;   // repo-relative
    StatusFlag flags = StatusFlag::None;
};

}  // namespace gitgui
```

`core/src/GitRepo.cpp` (open + move + dtor; `status()` filled in Task 7):
```cpp
#include "gitgui/GitRepo.hpp"
#include "gitgui/PathUtil.hpp"
#include <git2.h>
#include <utility>

namespace gitgui {

GitRepo::GitRepo(GitRepo&& o) noexcept : repo_(std::exchange(o.repo_, nullptr)) {}

GitRepo& GitRepo::operator=(GitRepo&& o) noexcept {
    if (this != &o) {
        if (repo_) git_repository_free(repo_);
        repo_ = std::exchange(o.repo_, nullptr);
    }
    return *this;
}

GitRepo::~GitRepo() {
    if (repo_) git_repository_free(repo_);
}

Expected<GitRepo> GitRepo::open(const std::filesystem::path& path) {
    git_repository* repo = nullptr;
    int rc = git_repository_open(&repo, to_git_path(path).c_str());
    if (rc < 0) return std::unexpected(last_git_error(rc));
    return GitRepo(repo);
}

}  // namespace gitgui
```

Add `src/GitRepo.cpp` to `core/CMakeLists.txt` `target_sources`.

> Note: `GitError.cpp` from Task 4 must be in `target_sources` for `last_git_error` to link.

- [ ] **Step 4: Build and run the tests**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R repo
```
Expected: both `[repo]` tests pass. (The `status()` declaration is unused here; it links once defined in Task 7 — if the linker complains about an undefined `status`, proceed to Task 7 which defines it; no test calls it yet, so it should not be referenced.)

- [ ] **Step 5: Commit**

```bash
git add core/include/gitgui/GitRepo.hpp core/include/gitgui/FileStatus.hpp core/src/GitRepo.cpp core/CMakeLists.txt tests/
git commit -m "feat(core): add GitRepo::open with RAII libgit2 handle"
```

---

## Task 7: GitRepo — status

**Files:**
- Modify: `core/src/GitRepo.cpp`
- Modify: `tests/test_git_repo.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_git_repo.cpp`:
```cpp
#include <algorithm>

TEST_CASE("status reports an untracked file as WtNew", "[repo]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("new.txt", "data");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());

    auto it = std::find_if(st->begin(), st->end(), [](const gitgui::FileStatus& f) {
        return f.path == std::filesystem::path("new.txt");
    });
    REQUIRE(it != st->end());
    REQUIRE(gitgui::has_flag(it->flags, gitgui::StatusFlag::WtNew));
}

TEST_CASE("status reports a committed-then-modified file as WtModified", "[repo]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "one");
    tmp.commit_all("add a.txt");
    tmp.write_file("a.txt", "two");   // modify after commit

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    auto it = std::find_if(st->begin(), st->end(), [](const gitgui::FileStatus& f) {
        return f.path == std::filesystem::path("a.txt");
    });
    REQUIRE(it != st->end());
    REQUIRE(gitgui::has_flag(it->flags, gitgui::StatusFlag::WtModified));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R repo
```
Expected: FAIL — link error on `GitRepo::status` (undefined) or assertion failure.

- [ ] **Step 3: Write the implementation**

Append to `core/src/GitRepo.cpp` (inside `namespace gitgui`):
```cpp
#include "gitgui/FileStatus.hpp"

namespace {
gitgui::StatusFlag map_status(unsigned int s) {
    using gitgui::StatusFlag;
    StatusFlag f = StatusFlag::None;
    if (s & GIT_STATUS_INDEX_NEW)      f |= StatusFlag::IndexNew;
    if (s & GIT_STATUS_INDEX_MODIFIED) f |= StatusFlag::IndexModified;
    if (s & GIT_STATUS_INDEX_DELETED)  f |= StatusFlag::IndexDeleted;
    if (s & GIT_STATUS_WT_NEW)         f |= StatusFlag::WtNew;
    if (s & GIT_STATUS_WT_MODIFIED)    f |= StatusFlag::WtModified;
    if (s & GIT_STATUS_WT_DELETED)     f |= StatusFlag::WtDeleted;
    return f;
}
}  // namespace

Expected<std::vector<FileStatus>> GitRepo::status() const {
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list* list = nullptr;
    int rc = git_status_list_new(&list, repo_, &opts);
    if (rc < 0) return std::unexpected(last_git_error(rc));

    std::vector<FileStatus> result;
    size_t n = git_status_list_entrycount(list);
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const git_status_entry* e = git_status_byindex(list, i);
        const char* raw =
            e->head_to_index   ? e->head_to_index->new_file.path
          : e->index_to_workdir ? e->index_to_workdir->new_file.path
          : nullptr;
        if (!raw) continue;
        result.push_back(FileStatus{from_git_path(raw), map_status(e->status)});
    }
    git_status_list_free(list);
    return result;
}
```

- [ ] **Step 4: Build and run the tests**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R repo
```
Expected: all `[repo]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add core/src/GitRepo.cpp tests/test_git_repo.cpp
git commit -m "feat(core): add GitRepo::status mapping libgit2 status flags"
```

---

## Task 8: ProjectStore — model + JSON round-trip

**Files:**
- Modify: `cmake/Dependencies.cmake`
- Create: `core/include/gitgui/ProjectStore.hpp`
- Create: `core/src/ProjectStore.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/test_project_store.cpp`

- [ ] **Step 1: Add nlohmann/json dependency**

Append to `cmake/Dependencies.cmake`:
```cmake
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
```
Append to `core/CMakeLists.txt` `target_link_libraries`:
```cmake
target_link_libraries(gitgui_core PUBLIC nlohmann_json::nlohmann_json)
```

- [ ] **Step 2: Write the failing test**

`tests/test_project_store.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/ProjectStore.hpp"

TEST_CASE("ProjectStore serializes and deserializes round-trip", "[store]") {
    gitgui::ProjectStore store;
    gitgui::Project p;
    p.id = "uuid-1";
    p.name = "Work";
    p.repos.push_back(gitgui::RepoRef{"/home/u/api", "api"});
    p.lastActiveRepo = "/home/u/api";
    store.projects().push_back(p);
    store.setActiveProject("uuid-1");

    std::string json = store.to_json();
    auto loaded = gitgui::ProjectStore::from_json(json);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->activeProject() == "uuid-1");
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "Work");
    REQUIRE(loaded->projects()[0].repos.size() == 1);
    REQUIRE(loaded->projects()[0].repos[0].alias == "api");
}
```

- [ ] **Step 3: Run test to verify it fails**

Add `test_project_store.cpp` to `tests/CMakeLists.txt`, then:
```bash
cmake --build build
```
Expected: FAIL — `gitgui/ProjectStore.hpp` not found.

- [ ] **Step 4: Write the implementation**

`core/include/gitgui/ProjectStore.hpp`:
```cpp
#pragma once
#include <string>
#include <vector>
#include "gitgui/GitError.hpp"

namespace gitgui {

struct RepoRef {
    std::string path;    // absolute, stored as UTF-8 generic path
    std::string alias;
};

struct Project {
    std::string id;
    std::string name;
    std::vector<RepoRef> repos;
    std::string lastActiveRepo;
};

// In-memory model of projects.json. Persistence (load/save to disk) in Task 9.
class ProjectStore {
public:
    static constexpr int kVersion = 1;

    std::vector<Project>& projects() { return projects_; }
    const std::vector<Project>& projects() const { return projects_; }

    const std::string& activeProject() const { return activeProject_; }
    void setActiveProject(std::string id) { activeProject_ = std::move(id); }

    std::string to_json() const;
    static Expected<ProjectStore> from_json(const std::string& json);

private:
    std::vector<Project> projects_;
    std::string activeProject_;
};

}  // namespace gitgui
```

`core/src/ProjectStore.cpp`:
```cpp
#include "gitgui/ProjectStore.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace gitgui {

std::string ProjectStore::to_json() const {
    json root;
    root["version"] = kVersion;
    root["activeProject"] = activeProject_;
    json arr = json::array();
    for (const auto& p : projects_) {
        json jp;
        jp["id"] = p.id;
        jp["name"] = p.name;
        jp["lastActiveRepo"] = p.lastActiveRepo;
        json repos = json::array();
        for (const auto& r : p.repos) {
            repos.push_back({{"path", r.path}, {"alias", r.alias}});
        }
        jp["repos"] = std::move(repos);
        arr.push_back(std::move(jp));
    }
    root["projects"] = std::move(arr);
    return root.dump(2);
}

Expected<ProjectStore> ProjectStore::from_json(const std::string& text) {
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded())
        return std::unexpected(GitError{-1, "invalid JSON in project store"});

    ProjectStore store;
    store.activeProject_ = root.value("activeProject", std::string{});
    for (const auto& jp : root.value("projects", json::array())) {
        Project p;
        p.id = jp.value("id", std::string{});
        p.name = jp.value("name", std::string{});
        p.lastActiveRepo = jp.value("lastActiveRepo", std::string{});
        for (const auto& jr : jp.value("repos", json::array())) {
            p.repos.push_back(RepoRef{jr.value("path", std::string{}),
                                      jr.value("alias", std::string{})});
        }
        store.projects_.push_back(std::move(p));
    }
    return store;
}

}  // namespace gitgui
```

Add `src/ProjectStore.cpp` to `core/CMakeLists.txt` `target_sources`.

- [ ] **Step 5: Build and run the tests**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R store
```
Expected: the `[store]` round-trip test passes.

- [ ] **Step 6: Commit**

```bash
git add cmake/Dependencies.cmake core/include/gitgui/ProjectStore.hpp core/src/ProjectStore.cpp core/CMakeLists.txt tests/
git commit -m "feat(core): add ProjectStore JSON serialization"
```

---

## Task 9: ProjectStore — atomic save/load + corrupt-input handling

**Files:**
- Modify: `core/include/gitgui/ProjectStore.hpp`
- Modify: `core/src/ProjectStore.cpp`
- Modify: `tests/test_project_store.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_project_store.cpp`:
```cpp
#include <filesystem>
#include <fstream>
#include <random>

namespace {
std::filesystem::path temp_json_path() {
    std::random_device rd;
    return std::filesystem::temp_directory_path() /
           ("gitgui_store_" + std::to_string(rd()) + ".json");
}
}

TEST_CASE("save then load round-trips through disk", "[store]") {
    auto path = temp_json_path();
    gitgui::ProjectStore store;
    gitgui::Project p; p.id = "x"; p.name = "Proj";
    store.projects().push_back(p);

    auto saved = store.save(path);
    REQUIRE(saved.has_value());

    auto loaded = gitgui::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().size() == 1);
    REQUIRE(loaded->projects()[0].name == "Proj");

    std::filesystem::remove(path);
}

TEST_CASE("load of a missing file returns an empty store", "[store]") {
    auto loaded = gitgui::ProjectStore::load(temp_json_path());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
}

TEST_CASE("load of corrupt JSON backs up the file and returns empty store", "[store]") {
    auto path = temp_json_path();
    { std::ofstream(path) << "{ this is not json"; }

    auto loaded = gitgui::ProjectStore::load(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->projects().empty());
    REQUIRE(std::filesystem::exists(path.string() + ".corrupt"));

    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + ".corrupt");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
cmake --build build
```
Expected: FAIL — `ProjectStore::save` / `ProjectStore::load` not declared.

- [ ] **Step 3: Write the implementation**

Add to `ProjectStore` public section in `core/include/gitgui/ProjectStore.hpp`:
```cpp
#include <filesystem>
    // Save atomically (temp file + rename). Returns error on I/O failure.
    Expected<void> save(const std::filesystem::path& file) const;

    // Load from disk. Missing file -> empty store. Corrupt file -> back it up
    // to "<file>.corrupt" and return an empty store (never fails on bad data).
    static Expected<ProjectStore> load(const std::filesystem::path& file);
```

Append to `core/src/ProjectStore.cpp`:
```cpp
#include "gitgui/PathUtil.hpp"
#include <fstream>
#include <system_error>

namespace gitgui {

Expected<void> ProjectStore::save(const std::filesystem::path& file) const {
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(GitError{-1, "cannot open temp file for write"});
        out << to_json();
        if (!out) return std::unexpected(GitError{-1, "write to temp file failed"});
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::rename(tmp, file, ec);
    if (ec) return std::unexpected(GitError{-1, "atomic rename failed: " + ec.message()});
    return {};
}

Expected<ProjectStore> ProjectStore::load(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return ProjectStore{};  // empty

    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(GitError{-1, "cannot open project store for read"});
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    auto parsed = from_json(text);
    if (!parsed.has_value()) {
        std::filesystem::path backup = file;
        backup += ".corrupt";
        std::filesystem::rename(file, backup, ec);  // best-effort
        return ProjectStore{};  // empty store, no crash
    }
    return parsed;
}

}  // namespace gitgui
```

- [ ] **Step 4: Build and run the tests**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R store
```
Expected: all `[store]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add core/include/gitgui/ProjectStore.hpp core/src/ProjectStore.cpp tests/test_project_store.cpp
git commit -m "feat(core): add atomic save/load with corrupt-file backup"
```

---

## Task 10: Full test run + CI workflow

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Run the whole suite locally**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed` across all tags (`[smoke] [libgit2] [path] [error] [support] [repo] [store]`).

- [ ] **Step 2: Add CI workflow**

`.github/workflows/ci.yml`:
```yaml
name: CI
on: [push, pull_request]
jobs:
  build-test:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-latest, windows-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build --config Release
      - name: Test
        run: ctest --test-dir build --output-on-failure --build-config Release
```

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: build and test on Linux/macOS/Windows"
```

---

## Self-Review Notes

- **Spec coverage:** Core layer (GitRepo open+status, ProjectStore, PathUtil, GitError/`std::expected`), path-handling rule, atomic writes, corrupt-input handling, Catch2 + temp-repo testing, CI matrix — all covered. Qt/UI, DiffEngine, GraphBuilder, coroutines, and parallel execution are intentionally deferred to Plans 2–4.
- **Type consistency:** `to_git_path`/`from_git_path`, `Expected<T>`, `GitError{code,message}`, `StatusFlag`/`has_flag`, `FileStatus{path,flags}`, `RepoRef{path,alias}`, `Project{id,name,repos,lastActiveRepo}`, `ProjectStore::{projects,activeProject,setActiveProject,to_json,from_json,save,load}` are used consistently across tasks.
- **libgit2 target name caveat** flagged in Task 2 — verify the exact FetchContent target at configure time.
```
