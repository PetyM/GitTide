# Plan 3b — Async Layer + Changes Tab UI Implementation Plan

| | |
|--|--|
| **Date** | 2026-06-17 |
| **Status** | `done` |
| **Spec** | [engineering](../spec/engineering/engineering.md) (async) · [product](../spec/product/product.md) (Changes tab) |
| **Depends on** | [Plan 3a](2026-06-17-plan3a-core-git-ops.md) · [UI shell](2026-06-16-ui-shell.md) |

**Goal:** Wire the verified Plan-3a Core git ops into the Qt UI through a QCoro async layer, an asynchronous parallel dashboard, and a working Changes tab (staged/unstaged lists, diff view with partial-staging, commit box).

**Architecture:** A thin `AsyncRepo` owns one `GitRepo` and runs every blocking call on Qt's global thread pool via `QtConcurrent::run`, exposing each as a `co_await`-able `QCoro::Task`; a per-repo mutex serializes pool access so two awaited ops never touch the same `git_repository` at once. `DashboardModel::refreshAsync` fans out one pool task per repo (each opens its **own** `GitRepo` — isolation, no shared state), then gathers. `RepoController` gains coroutine slots that `co_await` `AsyncRepo` and emit Qt signals; `ChangesView`/`DiffView` are plain widgets driven by those signals.

**Tech Stack:** C++23 coroutines, Qt 6 Widgets + Concurrent + Test, QCoro 0.11 (FetchContent), libgit2 (via Core), Catch2 (Core tests only — UI tested with QtTest).

---

## Background the implementer needs

**Layering rule (do not break it):** Core (`core/`) is Qt-free and speaks `std`. Qt types appear only in `ui/`. `AsyncRepo`, `RepoController`, `DashboardModel`, `ChangesView`, `DiffView` all live in `ui/` (namespace `gitgui::ui`). They consume Core types (`gitgui::DiffResult`, `gitgui::StageSelection`, `gitgui::FileStatus`, `gitgui::CommitRequest`, `gitgui::DiffTarget`) but never the reverse.

**Core API already shipped by Plan 3a** (`core/include/gitgui/GitRepo.hpp`, all return `Expected<T>` = `std::expected<T, GitError>`):

```cpp
static Expected<GitRepo> open(const std::filesystem::path& path);
Expected<std::vector<FileStatus>> status() const;
Expected<DiffResult> diff(DiffTarget target, const std::filesystem::path& file) const;
Expected<void> stage(const StageSelection& sel);
Expected<void> unstage(const StageSelection& sel);
Expected<void> discard(const StageSelection& sel);
Expected<std::string> commit(const CommitRequest& req);   // returns hex oid
```

Core data types (`core/include/gitgui/Diff.hpp`, `FileStatus.hpp`):

```cpp
enum class DiffTarget { WorktreeVsIndex, IndexVsHead };
enum class DiffLineOrigin { Context, Added, Removed };
struct DiffLine { DiffLineOrigin origin; int oldLineno; int newLineno; std::string text; bool noNewline; };
struct DiffHunk { int oldStart, oldLines, newStart, newLines; std::vector<DiffLine> lines; };
struct DiffResult { std::vector<DiffHunk> hunks; };
struct StageSelection { std::filesystem::path path; std::optional<int> hunkIndex; std::vector<int> lineIndices; };
struct CommitRequest { std::string message; };

enum class StatusFlag : std::uint32_t {
  None=0, IndexNew=1<<0, IndexModified=1<<1, IndexDeleted=1<<2,
  WtNew=1<<3, WtModified=1<<4, WtDeleted=1<<5 };
bool has_flag(StatusFlag value, StatusFlag flag);   // free function in namespace gitgui
struct FileStatus { std::filesystem::path path; StatusFlag flags; };
```

**Two CRITICAL coroutine rules — violating either causes use-after-free or cancellation:**

1. **Coroutine parameters MUST be taken by value, never by reference.** A `co_await` suspends the coroutine; by-reference parameters are not copied into the coroutine frame, so the referent dangles after the first suspension. Every coroutine slot below takes its args by value even though the connected signal passes by `const&` (Qt copies at the call boundary — this is allowed).
2. **Captured state that must survive a suspension lives behind a `std::shared_ptr` copied into the worker lambda.** `AsyncRepo` keeps its `GitRepo` + mutex in a `shared_ptr<Impl>`; each task method copies that `shared_ptr` into the `QtConcurrent::run` lambda, so the work completes safely even if the `AsyncRepo` is destroyed mid-flight.

**QCoro facts used below (VERIFIED against QCoro 0.11 in Task 1 — use these exact forms):**
- `#include <qcorotask.h>` → `QCoro::Task<T>` (the coroutine return type) and `QCoro::waitFor(task)` (spins a local event loop until the task finishes; returns its value — used in tests).
- `#include <core/qcorofuture.h>` → registers the awaiter for `QFuture<T>`. With it included you `co_await` a `QtConcurrent::run(...)` future **directly** — there is no `qCoro()` wrapper call: `co_return co_await QtConcurrent::run([]{ ... });`.
- `#include <QtConcurrent>` → `QtConcurrent::run(lambda)` returns `QFuture<T>` and runs `lambda` on Qt's global thread pool.
- **Link targets:** `QCoro6::Core` + `Qt6::Concurrent`. There is **no** `QCoro6::Concurrent` target in 0.11 — QFuture support lives in `QCoro6::Core`.

**Build / test commands** (run from repo root; a `build/` dir is assumed — create with `cmake -S . -B build` once):

```bash
cmake --build build -j                       # build everything
ctest --test-dir build -R gitgui_core_tests --output-on-failure   # Core (Catch2)
ctest --test-dir build -R gitgui_ui_tests   --output-on-failure   # UI (QtTest, offscreen)
```

**UI test wiring (MANDATORY two edits per new UI test file)** — `tests/ui/main.cpp` documents this. Adding a UI test requires:
1. Add the file to `gitgui_ui_test_sources` in `tests/CMakeLists.txt` (HEADER_FILE_ONLY, so AUTOMOC scans it).
2. In `tests/ui/main.cpp`: add `#include "test_<name>.cpp"` AND a `QTest::qExec` block in `main()`. Omitting step 2 compiles fine but silently runs zero tests for that class.

---

## File Structure

**New (ui/):**
- `ui/include/gitgui/ui/Metatypes.hpp` — `Q_DECLARE_METATYPE` for the Core types carried across Qt signals/`QSignalSpy`.
- `ui/include/gitgui/ui/AsyncRepo.hpp` + `ui/src/AsyncRepo.cpp` — async wrapper over one `GitRepo`.
- `ui/include/gitgui/ui/DiffView.hpp` + `ui/src/DiffView.cpp` — renders a `DiffResult`, emits stage/unstage/discard requests.
- `ui/include/gitgui/ui/ChangesView.hpp` + `ui/src/ChangesView.cpp` — staged/unstaged lists + DiffView + commit box.

**Modified (ui/):**
- `ui/include/gitgui/ui/DashboardModel.hpp` + `ui/src/DashboardModel.cpp` — add `refreshAsync` + `refreshed()` signal.
- `ui/include/gitgui/ui/RepoController.hpp` + `ui/src/RepoController.cpp` — swap `GitRepo` for `AsyncRepo`; add coroutine slots + signals.
- `ui/include/gitgui/ui/ProjectSidebar.hpp` + `ui/src/ProjectSidebar.cpp` — emit `repoSelected(path)` on repo-list click.
- `ui/include/gitgui/ui/MainWindow.hpp` + `ui/src/MainWindow.cpp` — Changes tab → `ChangesView` + `RepoController`; Dashboard tab → `DashboardModel`.
- `ui/CMakeLists.txt` — new sources, link `Qt6::Concurrent` + `QCoro6::Core` + `QCoro6::Concurrent`.

**Modified (build):**
- `cmake/Dependencies.cmake` — FetchContent QCoro; add `Concurrent` Qt component.

**New tests (tests/ui/):**
- `test_async_repo.cpp`, `test_dashboard_async.cpp`, `test_diff_view.cpp`, `test_changes_view.cpp`, plus additions to `test_repo_controller.cpp` and `test_main_window.cpp`.

---

## Task 1: Build wiring — QCoro + Qt Concurrent

Bring QCoro and Qt's Concurrent module into the build and prove a coroutine compiles and runs against `QtConcurrent::run`. No production code yet — this de-risks the toolchain before any feature work.

**Files:**
- Modify: `cmake/Dependencies.cmake`
- Modify: `ui/CMakeLists.txt:23`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/ui/test_qcoro_smoke.cpp`
- Modify: `tests/ui/main.cpp`

- [ ] **Step 1: Add QCoro + Concurrent to Dependencies.cmake**

In `cmake/Dependencies.cmake`, change the UI dependency block (currently the `if(GITGUI_BUILD_UI)` at the bottom) to add the `Concurrent` component, and add a QCoro FetchContent block. Replace the final `if(GITGUI_BUILD_UI) ... endif()` block with:

```cmake
# --- UI dependencies ---
# Qt 6 comes from the system or aqtinstall, NEVER FetchContent. Widgets for the UI,
# Test for headless UI unit tests, Concurrent for off-main-thread git ops.
# QCoro adds co_await support over QFuture; it is built from source via FetchContent.
if(GITGUI_BUILD_UI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Test Concurrent)

  # Trim QCoro to the modules we use; the rest pull in Qt components we don't link.
  set(QCORO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(QCORO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QML OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTDBUS OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTNETWORK OFF CACHE BOOL "" FORCE)
  set(QCORO_WITH_QTWEBSOCKETS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    qcoro
    GIT_REPOSITORY https://github.com/qcoro/qcoro.git
    GIT_TAG        v0.11.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(qcoro)
endif()
```

- [ ] **Step 2: Link the new libs into gitgui_ui**

In `ui/CMakeLists.txt`, change the final `target_link_libraries` line (line 23) to:

```cmake
target_link_libraries(gitgui_ui PUBLIC
  gitgui_core Qt6::Widgets Qt6::Concurrent QCoro6::Core QCoro6::Concurrent)
```

- [ ] **Step 3: Write the failing smoke test**

Create `tests/ui/test_qcoro_smoke.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QtConcurrent>

#include <qcoro/qcorotask.h>
#include <qcoro/qcorofuture.h>

namespace {
QCoro::Task<int> doubled_on_pool(int n) {
    auto fut = QtConcurrent::run([n]() { return n * 2; });
    co_return co_await qCoro(fut);
}
}  // namespace

class TestQCoroSmoke : public QObject {
    Q_OBJECT
private slots:
    void awaits_a_pool_task() {
        const int result = QCoro::waitFor(doubled_on_pool(21));
        QCOMPARE(result, 42);
    }
};

#include "test_qcoro_smoke.moc"
```

- [ ] **Step 4: Register the smoke test in the build**

In `tests/CMakeLists.txt`, add to the `gitgui_ui_test_sources` list (after `test_smoke.cpp`):

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qcoro_smoke.cpp
```

Then link Concurrent + QCoro into the UI test target. Change the `gitgui_ui_tests` `target_link_libraries` line to:

```cmake
  target_link_libraries(gitgui_ui_tests PRIVATE
    gitgui_ui Qt6::Widgets Qt6::Test Qt6::Concurrent QCoro6::Core QCoro6::Concurrent libgit2package)
```

- [ ] **Step 5: Wire the smoke test into main.cpp**

In `tests/ui/main.cpp`, add the include alongside the others:

```cpp
#include "test_qcoro_smoke.cpp"
```

and add this block inside `main()` (after the `TestUiSmoke` block):

```cpp
    {
        TestQCoroSmoke t;
        status |= QTest::qExec(&t, argc, argv);
    }
```

- [ ] **Step 6: Configure, build, run — verify PASS**

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: build succeeds (QCoro fetched + built once), `gitgui_ui_tests` passes including `awaits_a_pool_task`.

- [ ] **Step 7: Commit**

```bash
git add cmake/Dependencies.cmake ui/CMakeLists.txt tests/CMakeLists.txt tests/ui/test_qcoro_smoke.cpp tests/ui/main.cpp
git commit -m "build(ui): wire QCoro + Qt Concurrent; coroutine smoke test"
```

---

## Task 2: Metatypes header

Declare the Core types that will cross Qt signal boundaries so `QSignalSpy` can capture them by value. Header-only; no test of its own (exercised by every later task).

**Files:**
- Create: `ui/include/gitgui/ui/Metatypes.hpp`

- [ ] **Step 1: Create the header**

```cpp
#pragma once
#include <QMetaType>
#include <vector>
#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"

// Core types carried across Qt signals / captured by QSignalSpy. Q_DECLARE_METATYPE
// must appear at global scope. Call qRegisterMetaType<T>() once per type before use
// (the emitting classes do this in their constructors).
Q_DECLARE_METATYPE(gitgui::StageSelection)
Q_DECLARE_METATYPE(gitgui::DiffResult)
Q_DECLARE_METATYPE(gitgui::CommitRequest)
Q_DECLARE_METATYPE(std::vector<gitgui::FileStatus>)
```

- [ ] **Step 2: Verify it compiles**

```bash
cmake --build build -j --target gitgui_ui
```

Expected: PASS (no new translation unit yet; this just confirms the header parses when included later — a no-op build is fine).

- [ ] **Step 3: Commit**

```bash
git add ui/include/gitgui/ui/Metatypes.hpp
git commit -m "feat(ui): declare Qt metatypes for Core diff/status types"
```

---

## Task 3: AsyncRepo — open + status + serialization

The async wrapper. This task ships `open` and `status` plus the `shared_ptr<Impl>` + mutex machinery; the remaining ops are mechanical repeats in Task 4.

**Files:**
- Create: `ui/include/gitgui/ui/AsyncRepo.hpp`
- Create: `ui/src/AsyncRepo.cpp`
- Modify: `ui/CMakeLists.txt`
- Create: `tests/ui/test_async_repo.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/ui/main.cpp`

- [ ] **Step 1: Write the header**

`ui/include/gitgui/ui/AsyncRepo.hpp`:

```cpp
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <qcoro/qcorotask.h>

#include "gitgui/GitRepo.hpp"
#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"
#include "gitgui/GitError.hpp"

namespace gitgui::ui {

// Async wrapper over a single GitRepo. Each call runs on Qt's global thread pool
// via QtConcurrent and is exposed as a co_await-able QCoro task. A per-repo mutex
// (held inside the worker lambda) serializes pool access so two awaited ops never
// touch the same git_repository concurrently — satisfying Core's one-owner rule.
//
// Move-only. The GitRepo + mutex live behind a shared_ptr so in-flight work stays
// valid even if the AsyncRepo is destroyed before the task completes.
class AsyncRepo {
public:
    static gitgui::Expected<AsyncRepo> open(const std::filesystem::path& path);

    AsyncRepo(AsyncRepo&&) noexcept = default;
    AsyncRepo& operator=(AsyncRepo&&) noexcept = default;
    AsyncRepo(const AsyncRepo&) = delete;
    AsyncRepo& operator=(const AsyncRepo&) = delete;
    ~AsyncRepo();

    QCoro::Task<gitgui::Expected<std::vector<gitgui::FileStatus>>> status();
    QCoro::Task<gitgui::Expected<gitgui::DiffResult>> diff(
        gitgui::DiffTarget target, std::filesystem::path file);
    QCoro::Task<gitgui::Expected<void>> stage(gitgui::StageSelection sel);
    QCoro::Task<gitgui::Expected<void>> unstage(gitgui::StageSelection sel);
    QCoro::Task<gitgui::Expected<void>> discard(gitgui::StageSelection sel);
    QCoro::Task<gitgui::Expected<std::string>> commit(gitgui::CommitRequest req);

private:
    struct Impl;
    explicit AsyncRepo(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<Impl> impl_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_async_repo.cpp`. The helper builds a repo with one committed file, then modifies it so `status()` has something to report.

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>

#include <git2.h>
#include <qcoro/qcorotask.h>

#include "gitgui/ui/AsyncRepo.hpp"
#include "gitgui/FileStatus.hpp"

using gitgui::ui::AsyncRepo;

namespace {
// Repo with one committed file "a.txt", then locally modified (1 unstaged change).
std::filesystem::path make_dirty_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-ar-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);

    { std::ofstream(dir / "a.txt") << "one\n"; }
    git_index* idx = nullptr;
    git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt");
    git_index_write(idx);
    git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr;
    git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid;
    git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, "init", tree, 0);
    git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    git_repository_free(raw);

    { std::ofstream(dir / "a.txt") << "one\ntwo\n"; }   // unstaged modification
    git_libgit2_shutdown();
    return dir;
}
}  // namespace

class TestAsyncRepo : public QObject {
    Q_OBJECT
private slots:
    void open_missing_fails() {
        auto r = AsyncRepo::open("/no/such/gitgui-async-repo");
        QVERIFY(!r.has_value());
    }

    void status_runs_on_pool_and_reports_change() {
        const auto dir = make_dirty_repo();
        auto repo = AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        auto result = QCoro::waitFor(repo->status());
        QVERIFY(result.has_value());
        QCOMPARE(static_cast<int>(result->size()), 1);
        QCOMPARE((*result)[0].path, std::filesystem::path("a.txt"));
        QVERIFY(gitgui::has_flag((*result)[0].flags, gitgui::StatusFlag::WtModified));

        std::filesystem::remove_all(dir);
    }
};

#include "test_async_repo.moc"
```

- [ ] **Step 3: Register test, run — verify it FAILS to link/compile**

Add `test_async_repo.cpp` to `gitgui_ui_test_sources` in `tests/CMakeLists.txt`, add `AsyncRepo.cpp` + header to `ui/CMakeLists.txt` sources list, and wire `#include "test_async_repo.cpp"` + a `TestAsyncRepo` qExec block into `tests/ui/main.cpp`.

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: FAIL — undefined references to `AsyncRepo::open` / `AsyncRepo::status` (no .cpp yet).

- [ ] **Step 4: Write the implementation**

`ui/src/AsyncRepo.cpp`:

```cpp
#include "gitgui/ui/AsyncRepo.hpp"

#include <mutex>
#include <utility>

#include <QtConcurrent>
#include <qcoro/qcorofuture.h>

namespace gitgui::ui {

struct AsyncRepo::Impl {
    explicit Impl(gitgui::GitRepo r) : repo(std::move(r)) {}
    gitgui::GitRepo repo;
    std::mutex mutex;  // serializes pool access to the non-thread-safe GitRepo
};

AsyncRepo::~AsyncRepo() = default;

gitgui::Expected<AsyncRepo> AsyncRepo::open(const std::filesystem::path& path) {
    auto r = gitgui::GitRepo::open(path);
    if (!r) return std::unexpected(r.error());
    return AsyncRepo(std::make_shared<Impl>(std::move(*r)));
}

QCoro::Task<gitgui::Expected<std::vector<gitgui::FileStatus>>> AsyncRepo::status() {
    auto impl = impl_;
    auto fut = QtConcurrent::run([impl]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.status();
    });
    co_return co_await qCoro(fut);
}

// diff/stage/unstage/discard/commit are added in Task 4.

}  // namespace gitgui::ui
```

Add `AsyncRepo` to `ui/CMakeLists.txt` (inside the `add_library(gitgui_ui ...)` list):

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AsyncRepo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/AsyncRepo.hpp
```

- [ ] **Step 5: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: PASS — `open_missing_fails` and `status_runs_on_pool_and_reports_change`.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gitgui/ui/AsyncRepo.hpp ui/src/AsyncRepo.cpp ui/CMakeLists.txt tests/ui/test_async_repo.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): AsyncRepo with QCoro status + per-repo serialization guard"
```

---

## Task 4: AsyncRepo — diff / stage / unstage / discard / commit

Fill in the remaining five task methods, each the same shape as `status`: copy the `shared_ptr`, lock inside the lambda, await the future. Test the round-trip stage→status and commit.

**Files:**
- Modify: `ui/src/AsyncRepo.cpp`
- Modify: `tests/ui/test_async_repo.cpp`

- [ ] **Step 1: Write the failing tests**

Append two slots to `TestAsyncRepo` in `tests/ui/test_async_repo.cpp` (before the closing `};`):

```cpp
    void stage_whole_file_then_status_shows_staged() {
        const auto dir = make_dirty_repo();
        auto repo = AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        auto staged = QCoro::waitFor(repo->stage(gitgui::StageSelection{.path = "a.txt"}));
        QVERIFY(staged.has_value());

        auto result = QCoro::waitFor(repo->status());
        QVERIFY(result.has_value());
        QCOMPARE(static_cast<int>(result->size()), 1);
        QVERIFY(gitgui::has_flag((*result)[0].flags, gitgui::StatusFlag::IndexModified));

        std::filesystem::remove_all(dir);
    }

    void commit_after_staging_clears_status() {
        const auto dir = make_dirty_repo();
        auto repo = AsyncRepo::open(dir);
        QVERIFY(repo.has_value());

        QCoro::waitFor(repo->stage(gitgui::StageSelection{.path = "a.txt"}));
        auto oid = QCoro::waitFor(repo->commit(gitgui::CommitRequest{.message = "second"}));
        QVERIFY(oid.has_value());
        QVERIFY(!oid->empty());

        auto result = QCoro::waitFor(repo->status());
        QVERIFY(result.has_value());
        QCOMPARE(static_cast<int>(result->size()), 0);

        std::filesystem::remove_all(dir);
    }
```

Note: `make_dirty_repo` does not set `user.name`/`user.email` config, but `commit` uses `git_signature_default`, which reads repo/global config. To keep the test self-contained, extend `make_dirty_repo` to set identity right after `git_repository_init`:

```cpp
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
```

(Place this block immediately after the `git_repository_init` call and before writing `a.txt`.)

- [ ] **Step 2: Run — verify FAIL**

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: FAIL — undefined references to `AsyncRepo::stage` / `AsyncRepo::commit`.

- [ ] **Step 3: Implement the five methods**

In `ui/src/AsyncRepo.cpp`, replace the `// diff/stage/unstage/discard/commit are added in Task 4.` comment with:

```cpp
QCoro::Task<gitgui::Expected<gitgui::DiffResult>> AsyncRepo::diff(
    gitgui::DiffTarget target, std::filesystem::path file) {
    auto impl = impl_;
    auto fut = QtConcurrent::run([impl, target, file = std::move(file)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.diff(target, file);
    });
    co_return co_await qCoro(fut);
}

QCoro::Task<gitgui::Expected<void>> AsyncRepo::stage(gitgui::StageSelection sel) {
    auto impl = impl_;
    auto fut = QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.stage(sel);
    });
    co_return co_await qCoro(fut);
}

QCoro::Task<gitgui::Expected<void>> AsyncRepo::unstage(gitgui::StageSelection sel) {
    auto impl = impl_;
    auto fut = QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.unstage(sel);
    });
    co_return co_await qCoro(fut);
}

QCoro::Task<gitgui::Expected<void>> AsyncRepo::discard(gitgui::StageSelection sel) {
    auto impl = impl_;
    auto fut = QtConcurrent::run([impl, sel = std::move(sel)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.discard(sel);
    });
    co_return co_await qCoro(fut);
}

QCoro::Task<gitgui::Expected<std::string>> AsyncRepo::commit(gitgui::CommitRequest req) {
    auto impl = impl_;
    auto fut = QtConcurrent::run([impl, req = std::move(req)]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.commit(req);
    });
    co_return co_await qCoro(fut);
}
```

- [ ] **Step 4: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: PASS — all four `TestAsyncRepo` slots.

- [ ] **Step 5: Commit**

```bash
git add ui/src/AsyncRepo.cpp tests/ui/test_async_repo.cpp
git commit -m "feat(ui): AsyncRepo diff/stage/unstage/discard/commit"
```

---

## Task 5: DashboardModel async refresh

Add `refreshAsync` (parallel fan-out, each worker opens its own `GitRepo`) and a `refreshed()` signal. The synchronous `refresh()` stays for back-compat; row layout/roles are unchanged.

**Files:**
- Modify: `ui/include/gitgui/ui/DashboardModel.hpp`
- Modify: `ui/src/DashboardModel.cpp`
- Create: `tests/ui/test_dashboard_async.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/ui/main.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_dashboard_async.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <filesystem>
#include <fstream>

#include <git2.h>
#include <qcoro/qcorotask.h>

#include "gitgui/ui/DashboardModel.hpp"
#include "gitgui/ProjectStore.hpp"

using gitgui::ui::DashboardModel;

namespace {
std::filesystem::path make_repo_with_untracked() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-da-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    { std::ofstream(dir / "new.txt") << "x\n"; }  // 1 untracked change
    git_libgit2_shutdown();
    return dir;
}
}  // namespace

class TestDashboardAsync : public QObject {
    Q_OBJECT
private slots:
    void refresh_async_fans_out_and_reports() {
        const auto good = make_repo_with_untracked();
        std::vector<gitgui::RepoRef> repos = {
            gitgui::RepoRef{.path = good.generic_string(), .alias = "good"},
            gitgui::RepoRef{.path = "/no/such/gitgui-dash", .alias = "gone"},
        };

        DashboardModel model;
        QSignalSpy spy(&model, &DashboardModel::refreshed);
        QCoro::waitFor(model.refreshAsync(repos));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.rowCount(), 2);

        const auto idx0 = model.index(0);
        QCOMPARE(model.data(idx0, DashboardModel::ChangeCountRole).toInt(), 1);
        QCOMPARE(model.data(idx0, DashboardModel::MissingRole).toBool(), false);

        const auto idx1 = model.index(1);
        QCOMPARE(model.data(idx1, DashboardModel::MissingRole).toBool(), true);

        std::filesystem::remove_all(good);
    }
};

#include "test_dashboard_async.moc"
```

- [ ] **Step 2: Register + run — verify FAIL**

Add `test_dashboard_async.cpp` to `gitgui_ui_test_sources` and wire the include + `TestDashboardAsync` qExec block into `tests/ui/main.cpp`.

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: FAIL — `DashboardModel` has no `refreshAsync` / `refreshed`.

- [ ] **Step 3: Add the declaration**

In `ui/include/gitgui/ui/DashboardModel.hpp`, add `<qcoro/qcorotask.h>` to the includes, then inside the class after `void refresh(...)`:

```cpp
    // Recompute rows in parallel: one pool task per repo, each opening its OWN
    // GitRepo (no shared state). Emits refreshed() when all results are gathered.
    QCoro::Task<void> refreshAsync(std::vector<gitgui::RepoRef> repos);

signals:
    void refreshed();
```

(Add a `signals:` section if the class doesn't already have one — it currently does not, so insert it before the `private:` section.)

- [ ] **Step 4: Implement**

In `ui/src/DashboardModel.cpp`, add includes at the top:

```cpp
#include <QtConcurrent>
#include <qcoro/qcorofuture.h>
#include <vector>
```

and add the method (the worker returns the model's private `Row`, which is copyable):

```cpp
QCoro::Task<void> DashboardModel::refreshAsync(std::vector<gitgui::RepoRef> repos) {
    std::vector<QFuture<Row>> futures;
    futures.reserve(repos.size());
    for (const auto& r : repos) {
        futures.push_back(QtConcurrent::run([r]() -> Row {
            Row row{
                .alias = QString::fromStdString(r.alias),
                .path = QString::fromStdString(r.path),
                .changeCount = 0,
                .missing = false,
            };
            auto repo = gitgui::GitRepo::open(std::filesystem::path(r.path));
            if (!repo) {
                row.missing = true;
            } else if (auto status = repo->status()) {
                row.changeCount = static_cast<int>(status->size());
            } else {
                row.missing = true;
            }
            return row;
        }));
    }

    std::vector<Row> rows;
    rows.reserve(futures.size());
    for (auto& f : futures) {
        rows.push_back(co_await qCoro(f));
    }

    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    emit refreshed();
}
```

- [ ] **Step 5: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: PASS — `refresh_async_fans_out_and_reports`.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gitgui/ui/DashboardModel.hpp ui/src/DashboardModel.cpp tests/ui/test_dashboard_async.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): DashboardModel::refreshAsync parallel fan-out"
```

---

## Task 6: DiffView widget

A widget that renders a `DiffResult` as one selectable row per line and builds a `StageSelection` from the selected lines. Pure widget — no async, no git.

**Files:**
- Create: `ui/include/gitgui/ui/DiffView.hpp`
- Create: `ui/src/DiffView.cpp`
- Modify: `ui/CMakeLists.txt`
- Create: `tests/ui/test_diff_view.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/ui/main.cpp`

- [ ] **Step 1: Write the header**

`ui/include/gitgui/ui/DiffView.hpp`:

```cpp
#pragma once
#include <QWidget>
#include <filesystem>
#include <optional>

#include "gitgui/Diff.hpp"

class QListWidget;

namespace gitgui::ui {

// Renders a DiffResult as one row per diff line. The user selects lines and
// triggers stage/unstage/discard; the view builds a StageSelection covering the
// selected lines (constrained to a single hunk — the hunk of the first selected
// line) and emits it. Context lines are shown but ignored when building selection.
class DiffView : public QWidget {
    Q_OBJECT
public:
    explicit DiffView(QWidget* parent = nullptr);

    void setDiff(const gitgui::DiffResult& result, const std::filesystem::path& file);
    void clear();

    // Build a selection from the currently selected non-context lines.
    // nullopt when nothing stageable is selected.
    std::optional<gitgui::StageSelection> currentSelection() const;

public slots:
    void requestStage();
    void requestUnstage();
    void requestDiscard();

signals:
    void stageRequested(const gitgui::StageSelection& sel);
    void unstageRequested(const gitgui::StageSelection& sel);
    void discardRequested(const gitgui::StageSelection& sel);

private:
    QListWidget* lines_;
    std::filesystem::path file_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_diff_view.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QListWidget>

#include "gitgui/ui/DiffView.hpp"
#include "gitgui/ui/Metatypes.hpp"

using gitgui::ui::DiffView;

namespace {
gitgui::DiffResult two_added_one_context() {
    gitgui::DiffHunk h;
    h.oldStart = 1; h.oldLines = 1; h.newStart = 1; h.newLines = 3;
    h.lines = {
        {gitgui::DiffLineOrigin::Added,   -1, 1, "alpha", false},
        {gitgui::DiffLineOrigin::Added,   -1, 2, "beta",  false},
        {gitgui::DiffLineOrigin::Context,  1, 3, "gamma", false},
    };
    return gitgui::DiffResult{.hunks = {h}};
}
}  // namespace

class TestDiffView : public QObject {
    Q_OBJECT
private slots:
    void renders_one_row_per_line() {
        DiffView view;
        view.setDiff(two_added_one_context(), "f.txt");
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);
    }

    void selecting_lines_builds_selection_and_emits() {
        DiffView view;
        view.setDiff(two_added_one_context(), "f.txt");
        auto* list = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
        list->item(0)->setSelected(true);
        list->item(1)->setSelected(true);

        QSignalSpy spy(&view, &DiffView::stageRequested);
        view.requestStage();

        QCOMPARE(spy.count(), 1);
        const auto sel = spy.at(0).at(0).value<gitgui::StageSelection>();
        QCOMPARE(sel.path, std::filesystem::path("f.txt"));
        QVERIFY(sel.hunkIndex.has_value());
        QCOMPARE(sel.hunkIndex.value(), 0);
        QCOMPARE(static_cast<int>(sel.lineIndices.size()), 2);
        QCOMPARE(sel.lineIndices[0], 0);
        QCOMPARE(sel.lineIndices[1], 1);
    }

    void no_selection_yields_nullopt() {
        DiffView view;
        view.setDiff(two_added_one_context(), "f.txt");
        QVERIFY(!view.currentSelection().has_value());
    }
};

#include "test_diff_view.moc"
```

- [ ] **Step 3: Register + run — verify FAIL**

Add `test_diff_view.cpp` to `gitgui_ui_test_sources`, add `DiffView.cpp`/header to `ui/CMakeLists.txt`, wire include + qExec block into `tests/ui/main.cpp`.

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: FAIL — undefined references to `DiffView`.

- [ ] **Step 4: Implement**

`ui/src/DiffView.cpp` (each row stores its hunk index and within-hunk line index in item data; selection takes the hunk of the first selected non-context line and collects same-hunk non-context lines):

```cpp
#include "gitgui/ui/DiffView.hpp"
#include "gitgui/ui/Metatypes.hpp"

#include <QFont>
#include <QListWidget>
#include <QVBoxLayout>

namespace gitgui::ui {

namespace {
constexpr int HunkRole = Qt::UserRole + 1;
constexpr int LineRole = Qt::UserRole + 2;
constexpr int OriginRole = Qt::UserRole + 3;
}  // namespace

DiffView::DiffView(QWidget* parent)
    : QWidget(parent), lines_(new QListWidget(this)) {
    qRegisterMetaType<gitgui::StageSelection>();
    lines_->setObjectName(QStringLiteral("diffLines"));
    lines_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    lines_->setFont(mono);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(lines_);
}

void DiffView::clear() {
    lines_->clear();
    file_.clear();
}

void DiffView::setDiff(const gitgui::DiffResult& result, const std::filesystem::path& file) {
    lines_->clear();
    file_ = file;
    for (int h = 0; h < static_cast<int>(result.hunks.size()); ++h) {
        const auto& hunk = result.hunks[h];
        for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i) {
            const auto& ln = hunk.lines[i];
            const QChar prefix = ln.origin == gitgui::DiffLineOrigin::Added   ? QChar('+')
                               : ln.origin == gitgui::DiffLineOrigin::Removed ? QChar('-')
                                                                              : QChar(' ');
            auto* item = new QListWidgetItem(prefix + QString::fromStdString(ln.text), lines_);
            item->setData(HunkRole, h);
            item->setData(LineRole, i);
            item->setData(OriginRole, static_cast<int>(ln.origin));
        }
    }
}

std::optional<gitgui::StageSelection> DiffView::currentSelection() const {
    const auto selected = lines_->selectedItems();
    int hunk = -1;
    std::vector<int> lineIdx;
    for (auto* item : selected) {
        const auto origin = static_cast<gitgui::DiffLineOrigin>(item->data(OriginRole).toInt());
        if (origin == gitgui::DiffLineOrigin::Context) continue;  // context not stageable
        const int h = item->data(HunkRole).toInt();
        if (hunk == -1) hunk = h;
        if (h != hunk) continue;  // restrict to the first selected hunk
        lineIdx.push_back(item->data(LineRole).toInt());
    }
    if (hunk == -1 || lineIdx.empty()) return std::nullopt;
    std::sort(lineIdx.begin(), lineIdx.end());
    return gitgui::StageSelection{.path = file_, .hunkIndex = hunk, .lineIndices = std::move(lineIdx)};
}

void DiffView::requestStage() {
    if (auto sel = currentSelection()) emit stageRequested(*sel);
}
void DiffView::requestUnstage() {
    if (auto sel = currentSelection()) emit unstageRequested(*sel);
}
void DiffView::requestDiscard() {
    if (auto sel = currentSelection()) emit discardRequested(*sel);
}

}  // namespace gitgui::ui
```

Add to `ui/CMakeLists.txt` sources:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DiffView.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/DiffView.hpp
```

Add `#include <algorithm>` to `DiffView.cpp` for `std::sort`.

- [ ] **Step 5: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: PASS — three `TestDiffView` slots.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gitgui/ui/DiffView.hpp ui/src/DiffView.cpp ui/CMakeLists.txt tests/ui/test_diff_view.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): DiffView renders diff + builds StageSelection from selection"
```

---

## Task 7: ChangesView widget

Staged/unstaged file lists + embedded `DiffView` + commit message box. Splits a `status()` vector into the two lists, gates the Commit button, and emits `fileSelected` / `commitRequested` plus forwards DiffView's three request signals.

**Files:**
- Create: `ui/include/gitgui/ui/ChangesView.hpp`
- Create: `ui/src/ChangesView.cpp`
- Modify: `ui/CMakeLists.txt`
- Create: `tests/ui/test_changes_view.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/ui/main.cpp`

- [ ] **Step 1: Write the header**

`ui/include/gitgui/ui/ChangesView.hpp`:

```cpp
#pragma once
#include <QWidget>
#include <vector>

#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"

class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace gitgui::ui {

class DiffView;

// Changes tab body: staged + unstaged file lists (left), DiffView (right),
// commit message + button (bottom). A file is listed under "staged" if it has
// any Index* flag and under "unstaged" if it has any Wt* flag (it may appear in
// both). Selecting a file emits fileSelected(path, target); target is IndexVsHead
// for the staged list, WorktreeVsIndex for the unstaged list.
class ChangesView : public QWidget {
    Q_OBJECT
public:
    explicit ChangesView(QWidget* parent = nullptr);

    void setStatus(const std::vector<gitgui::FileStatus>& files);
    void setDiff(const gitgui::DiffResult& result, const std::filesystem::path& file);
    QString commitMessage() const;
    DiffView* diffView() const { return diff_; }

signals:
    void fileSelected(const QString& path, gitgui::DiffTarget target);
    void commitRequested(const gitgui::CommitRequest& req);
    void stageRequested(const gitgui::StageSelection& sel);
    void unstageRequested(const gitgui::StageSelection& sel);
    void discardRequested(const gitgui::StageSelection& sel);

private:
    void updateCommitEnabled();

    QListWidget* staged_;
    QListWidget* unstaged_;
    DiffView* diff_;
    QPlainTextEdit* message_;
    QPushButton* commitButton_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_changes_view.cpp`:

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

#include "gitgui/ui/ChangesView.hpp"
#include "gitgui/ui/Metatypes.hpp"

using gitgui::ui::ChangesView;

namespace {
std::vector<gitgui::FileStatus> mixed_status() {
    return {
        {std::filesystem::path("staged.txt"),   gitgui::StatusFlag::IndexModified},
        {std::filesystem::path("unstaged.txt"), gitgui::StatusFlag::WtModified},
        {std::filesystem::path("both.txt"),
            gitgui::StatusFlag::IndexModified | gitgui::StatusFlag::WtModified},
    };
}
}  // namespace

class TestChangesView : public QObject {
    Q_OBJECT
private slots:
    void splits_status_into_staged_and_unstaged() {
        ChangesView view;
        view.setStatus(mixed_status());
        auto* staged = view.findChild<QListWidget*>(QStringLiteral("stagedList"));
        auto* unstaged = view.findChild<QListWidget*>(QStringLiteral("unstagedList"));
        QVERIFY(staged && unstaged);
        QCOMPARE(staged->count(), 2);    // staged.txt + both.txt
        QCOMPARE(unstaged->count(), 2);  // unstaged.txt + both.txt
    }

    void commit_button_gated_on_message_and_staged() {
        ChangesView view;
        auto* button = view.findChild<QPushButton*>(QStringLiteral("commitButton"));
        auto* message = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
        QVERIFY(button && message);

        view.setStatus(mixed_status());
        QVERIFY(!button->isEnabled());          // staged but no message
        message->setPlainText(QStringLiteral("hello"));
        QVERIFY(button->isEnabled());           // message + staged
        view.setStatus({});                     // nothing staged
        QVERIFY(!button->isEnabled());
    }

    void selecting_unstaged_file_emits_worktree_target() {
        ChangesView view;
        view.setStatus(mixed_status());
        auto* unstaged = view.findChild<QListWidget*>(QStringLiteral("unstagedList"));

        QSignalSpy spy(&view, &ChangesView::fileSelected);
        unstaged->setCurrentRow(0);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).value<gitgui::DiffTarget>(),
                 gitgui::DiffTarget::WorktreeVsIndex);
    }

    void commit_button_emits_request_with_message() {
        ChangesView view;
        view.setStatus(mixed_status());
        auto* button = view.findChild<QPushButton*>(QStringLiteral("commitButton"));
        auto* message = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
        message->setPlainText(QStringLiteral("my commit"));

        QSignalSpy spy(&view, &ChangesView::commitRequested);
        button->click();

        QCOMPARE(spy.count(), 1);
        const auto req = spy.at(0).at(0).value<gitgui::CommitRequest>();
        QCOMPARE(QString::fromStdString(req.message), QStringLiteral("my commit"));
    }
};

#include "test_changes_view.moc"
```

Note: `gitgui::DiffTarget` must be capturable by `QSignalSpy`. Add it to `Metatypes.hpp` in this task (Step 4).

- [ ] **Step 3: Register + run — verify FAIL**

Add `test_changes_view.cpp` to `gitgui_ui_test_sources`, add `ChangesView.cpp`/header to `ui/CMakeLists.txt`, wire include + qExec block into `tests/ui/main.cpp`.

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: FAIL — undefined references to `ChangesView`.

- [ ] **Step 4: Declare DiffTarget metatype**

Add to `ui/include/gitgui/ui/Metatypes.hpp` (after the existing declarations):

```cpp
Q_DECLARE_METATYPE(gitgui::DiffTarget)
```

- [ ] **Step 5: Implement**

`ui/src/ChangesView.cpp`:

```cpp
#include "gitgui/ui/ChangesView.hpp"
#include "gitgui/ui/DiffView.hpp"
#include "gitgui/ui/Metatypes.hpp"

#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

namespace gitgui::ui {

namespace {
constexpr int PathRole = Qt::UserRole + 1;

bool any_index_flag(gitgui::StatusFlag f) {
    using gitgui::has_flag; using gitgui::StatusFlag;
    return has_flag(f, StatusFlag::IndexNew) || has_flag(f, StatusFlag::IndexModified) ||
           has_flag(f, StatusFlag::IndexDeleted);
}
bool any_wt_flag(gitgui::StatusFlag f) {
    using gitgui::has_flag; using gitgui::StatusFlag;
    return has_flag(f, StatusFlag::WtNew) || has_flag(f, StatusFlag::WtModified) ||
           has_flag(f, StatusFlag::WtDeleted);
}
}  // namespace

ChangesView::ChangesView(QWidget* parent)
    : QWidget(parent),
      staged_(new QListWidget(this)),
      unstaged_(new QListWidget(this)),
      diff_(new DiffView(this)),
      message_(new QPlainTextEdit(this)),
      commitButton_(new QPushButton(QStringLiteral("Commit"), this)) {
    qRegisterMetaType<gitgui::CommitRequest>();
    qRegisterMetaType<gitgui::StageSelection>();
    qRegisterMetaType<gitgui::DiffTarget>();

    staged_->setObjectName(QStringLiteral("stagedList"));
    unstaged_->setObjectName(QStringLiteral("unstagedList"));
    message_->setObjectName(QStringLiteral("commitMessage"));
    commitButton_->setObjectName(QStringLiteral("commitButton"));
    commitButton_->setEnabled(false);

    auto* leftLayout = new QVBoxLayout;
    leftLayout->addWidget(staged_, 1);
    leftLayout->addWidget(unstaged_, 1);
    leftLayout->addWidget(message_);
    leftLayout->addWidget(commitButton_);
    auto* left = new QWidget(this);
    left->setLayout(leftLayout);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(left);
    splitter->addWidget(diff_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(staged_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        emit fileSelected(staged_->item(row)->data(PathRole).toString(),
                          gitgui::DiffTarget::IndexVsHead);
    });
    connect(unstaged_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        emit fileSelected(unstaged_->item(row)->data(PathRole).toString(),
                          gitgui::DiffTarget::WorktreeVsIndex);
    });
    connect(message_, &QPlainTextEdit::textChanged, this, &ChangesView::updateCommitEnabled);
    connect(commitButton_, &QPushButton::clicked, this, [this]() {
        emit commitRequested(gitgui::CommitRequest{.message = commitMessage().toStdString()});
    });

    connect(diff_, &DiffView::stageRequested, this, &ChangesView::stageRequested);
    connect(diff_, &DiffView::unstageRequested, this, &ChangesView::unstageRequested);
    connect(diff_, &DiffView::discardRequested, this, &ChangesView::discardRequested);
}

void ChangesView::setStatus(const std::vector<gitgui::FileStatus>& files) {
    staged_->clear();
    unstaged_->clear();
    for (const auto& f : files) {
        const QString path = QString::fromStdString(f.path.generic_string());
        if (any_index_flag(f.flags)) {
            auto* item = new QListWidgetItem(path, staged_);
            item->setData(PathRole, path);
        }
        if (any_wt_flag(f.flags)) {
            auto* item = new QListWidgetItem(path, unstaged_);
            item->setData(PathRole, path);
        }
    }
    updateCommitEnabled();
}

void ChangesView::setDiff(const gitgui::DiffResult& result, const std::filesystem::path& file) {
    diff_->setDiff(result, file);
}

QString ChangesView::commitMessage() const {
    return message_->toPlainText();
}

void ChangesView::updateCommitEnabled() {
    commitButton_->setEnabled(!message_->toPlainText().trimmed().isEmpty() &&
                              staged_->count() > 0);
}

}  // namespace gitgui::ui
```

Add to `ui/CMakeLists.txt` sources:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ChangesView.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gitgui/ui/ChangesView.hpp
```

- [ ] **Step 6: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: PASS — four `TestChangesView` slots.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gitgui/ui/ChangesView.hpp ui/src/ChangesView.cpp ui/include/gitgui/ui/Metatypes.hpp ui/CMakeLists.txt tests/ui/test_changes_view.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): ChangesView staged/unstaged lists + commit gating"
```

---

## Task 8: RepoController async slots

Swap the synchronous `GitRepo` for `AsyncRepo` and add coroutine slots that `co_await` it, emitting Qt signals. **Every coroutine slot takes its arguments BY VALUE** (see the critical rules at the top).

**Files:**
- Modify: `ui/include/gitgui/ui/RepoController.hpp`
- Modify: `ui/src/RepoController.cpp`
- Modify: `tests/ui/test_repo_controller.cpp`

- [ ] **Step 1: Rewrite the header**

`ui/include/gitgui/ui/RepoController.hpp`:

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <optional>
#include <vector>

#include <qcoro/qcorotask.h>

#include "gitgui/ui/AsyncRepo.hpp"
#include "gitgui/Diff.hpp"
#include "gitgui/FileStatus.hpp"

namespace gitgui::ui {

// Holds the active repository for a window and drives it asynchronously. open()
// is synchronous (cheap); all git work runs through AsyncRepo on the thread pool.
// Coroutine slots take args BY VALUE so they survive a co_await suspension.
class RepoController : public QObject {
    Q_OBJECT
public:
    explicit RepoController(QObject* parent = nullptr);

    bool isOpen() const { return repo_.has_value(); }
    QString path() const { return path_; }

public slots:
    void open(const QString& path);
    QCoro::Task<void> refreshStatus();
    QCoro::Task<void> refreshDiff(QString path, gitgui::DiffTarget target);
    QCoro::Task<void> stage(gitgui::StageSelection sel);
    QCoro::Task<void> unstage(gitgui::StageSelection sel);
    QCoro::Task<void> discard(gitgui::StageSelection sel);
    QCoro::Task<void> commit(gitgui::CommitRequest req);

signals:
    void repoOpened(const QString& path);
    void repoFailed(const QString& path, const QString& message);
    void statusChanged(const std::vector<gitgui::FileStatus>& files);
    void diffReady(const QString& path, const gitgui::DiffResult& result);
    void committed(const QString& oid);
    void operationFailed(const QString& message);

private:
    std::optional<AsyncRepo> repo_;
    QString path_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 2: Write the failing tests**

Replace the body of `tests/ui/test_repo_controller.cpp` with the following (keeps the two existing open tests, adds async ones; `make_repo` is replaced by a dirty-repo helper):

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <filesystem>
#include <fstream>

#include <git2.h>
#include <qcoro/qcorotask.h>

#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/Metatypes.hpp"

using gitgui::ui::RepoController;

namespace {
std::filesystem::path make_empty_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-rc-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

// Repo with a committed a.txt then a local modification (1 unstaged change).
std::filesystem::path make_dirty_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-rcd-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    { std::ofstream(dir / "a.txt") << "one\n"; }
    git_index* idx = nullptr; git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt"); git_index_write(idx);
    git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid;
    git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, "init", tree, 0);
    git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    git_repository_free(raw);
    { std::ofstream(dir / "a.txt") << "one\ntwo\n"; }
    git_libgit2_shutdown();
    return dir;
}
}  // namespace

class TestRepoController : public QObject {
    Q_OBJECT
private slots:
    void open_existing_repo_succeeds() {
        const auto dir = make_empty_repo();
        RepoController controller;
        QSignalSpy ok(&controller, &RepoController::repoOpened);
        QSignalSpy bad(&controller, &RepoController::repoFailed);
        controller.open(QString::fromStdString(dir.generic_string()));
        QCOMPARE(ok.count(), 1);
        QCOMPARE(bad.count(), 0);
        QVERIFY(controller.isOpen());
        std::filesystem::remove_all(dir);
    }

    void open_missing_repo_fails() {
        RepoController controller;
        QSignalSpy ok(&controller, &RepoController::repoOpened);
        QSignalSpy bad(&controller, &RepoController::repoFailed);
        controller.open(QStringLiteral("/no/such/gitgui-repo"));
        QCOMPARE(ok.count(), 0);
        QCOMPARE(bad.count(), 1);
        QVERIFY(!controller.isOpen());
    }

    void refresh_status_emits_changes() {
        const auto dir = make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::statusChanged);
        QCoro::waitFor(controller.refreshStatus());
        QCOMPARE(spy.count(), 1);
        const auto files = spy.at(0).at(0).value<std::vector<gitgui::FileStatus>>();
        QCOMPARE(static_cast<int>(files.size()), 1);
        std::filesystem::remove_all(dir);
    }

    void stage_then_status_shows_staged() {
        const auto dir = make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::statusChanged);
        QCoro::waitFor(controller.stage(gitgui::StageSelection{.path = "a.txt"}));
        QVERIFY(spy.count() >= 1);  // stage chains a refreshStatus
        const auto files = spy.at(spy.count() - 1).at(0).value<std::vector<gitgui::FileStatus>>();
        QCOMPARE(static_cast<int>(files.size()), 1);
        QVERIFY(gitgui::has_flag(files[0].flags, gitgui::StatusFlag::IndexModified));
        std::filesystem::remove_all(dir);
    }

    void refresh_diff_emits_diff_ready() {
        const auto dir = make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::diffReady);
        QCoro::waitFor(controller.refreshDiff(QStringLiteral("a.txt"),
                                              gitgui::DiffTarget::WorktreeVsIndex));
        QCOMPARE(spy.count(), 1);
        const auto result = spy.at(0).at(1).value<gitgui::DiffResult>();
        QVERIFY(!result.hunks.empty());
        std::filesystem::remove_all(dir);
    }
};

#include "test_repo_controller.moc"
```

- [ ] **Step 3: Run — verify FAIL**

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: FAIL — `RepoController` has no `refreshStatus` / `stage` / `refreshDiff` etc.

- [ ] **Step 4: Implement**

`ui/src/RepoController.cpp`:

```cpp
#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/Metatypes.hpp"

#include <filesystem>

namespace gitgui::ui {

RepoController::RepoController(QObject* parent) : QObject(parent) {
    qRegisterMetaType<std::vector<gitgui::FileStatus>>();
    qRegisterMetaType<gitgui::DiffResult>();
    qRegisterMetaType<gitgui::StageSelection>();
    qRegisterMetaType<gitgui::CommitRequest>();
}

void RepoController::open(const QString& path) {
    auto result = AsyncRepo::open(std::filesystem::path(path.toStdString()));
    if (!result) {
        repo_.reset();
        path_.clear();
        emit repoFailed(path, QString::fromStdString(result.error().message));
        return;
    }
    repo_.emplace(std::move(*result));
    path_ = path;
    emit repoOpened(path);
}

QCoro::Task<void> RepoController::refreshStatus() {
    if (!repo_) co_return;
    auto result = co_await repo_->status();
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit statusChanged(*result);
}

QCoro::Task<void> RepoController::refreshDiff(QString path, gitgui::DiffTarget target) {
    if (!repo_) co_return;
    auto result = co_await repo_->diff(target, std::filesystem::path(path.toStdString()));
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit diffReady(path, *result);
}

QCoro::Task<void> RepoController::stage(gitgui::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->stage(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::unstage(gitgui::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->unstage(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::discard(gitgui::StageSelection sel) {
    if (!repo_) co_return;
    auto result = co_await repo_->discard(sel);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}

QCoro::Task<void> RepoController::commit(gitgui::CommitRequest req) {
    if (!repo_) co_return;
    auto result = co_await repo_->commit(req);
    if (!result) {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit committed(QString::fromStdString(*result));
    co_await refreshStatus();
}

}  // namespace gitgui::ui
```

- [ ] **Step 5: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: PASS — all five `TestRepoController` slots.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gitgui/ui/RepoController.hpp ui/src/RepoController.cpp tests/ui/test_repo_controller.cpp
git commit -m "feat(ui): RepoController async slots (status/diff/stage/unstage/discard/commit)"
```

---

## Task 9: MainWindow wiring — Changes + Dashboard tabs

Replace the two placeholder tabs with live widgets and wire repo selection. Selecting a repo in the sidebar opens it in a per-window `RepoController` that drives the `ChangesView`; activating a project refreshes the `DashboardModel`.

**Files:**
- Modify: `ui/include/gitgui/ui/ProjectSidebar.hpp`
- Modify: `ui/src/ProjectSidebar.cpp`
- Modify: `ui/include/gitgui/ui/MainWindow.hpp`
- Modify: `ui/src/MainWindow.cpp`
- Modify: `tests/ui/test_main_window.cpp`

- [ ] **Step 1: Add repoSelected to ProjectSidebar**

In `ui/include/gitgui/ui/ProjectSidebar.hpp`, add a signal:

```cpp
signals:
    void openInNewWindowRequested(const QString& projectId);
    void repoSelected(const QString& repoPath);
```

In `ui/src/ProjectSidebar.cpp`, after the existing `switcher_` connect, add (the repo list model is `RepoListModel` with `PathRole`):

```cpp
    connect(repoList_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (!current.isValid()) return;
                emit repoSelected(current.data(RepoListModel::PathRole).toString());
            });
```

`repoList_->setModel(...)` is called in the constructor before this, so `selectionModel()` is valid. `RepoListModel` is already included.

- [ ] **Step 2: Write the failing test**

Replace `tests/ui/test_main_window.cpp` with the version below — keeps the two existing tests and adds: the Changes tab is a `ChangesView`, and selecting a repo opens it in the controller. The repo must be a real on-disk git repo so `open` succeeds.

```cpp
#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTabWidget>
#include <filesystem>

#include <git2.h>

#include "gitgui/ProjectStore.hpp"
#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/ChangesView.hpp"

using gitgui::Project;
using gitgui::ProjectStore;
using gitgui::RepoRef;
using gitgui::ui::MainWindow;

namespace {
std::filesystem::path make_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-mw-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}
}  // namespace

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void show_project_activates_and_lists_repos() {
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = "/home/u/api", .alias = "api"} }});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));
        QCOMPARE(win.currentProjectId(), QStringLiteral("id-a"));
        QCOMPARE(win.controller()->repos()->rowCount(), 1);
        auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 3);
    }

    void open_in_new_window_propagates_upward() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));
        QSignalSpy spy(&win, &MainWindow::openInNewWindowRequested);
        auto* sidebar = win.findChild<gitgui::ui::ProjectSidebar*>();
        QVERIFY(sidebar != nullptr);
        sidebar->requestOpenInNewWindow();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("id-a"));
    }

    void changes_tab_is_a_changes_view() {
        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        MainWindow win(&store);
        QVERIFY(win.findChild<gitgui::ui::ChangesView*>() != nullptr);
    }

    void selecting_repo_opens_it_in_controller() {
        const auto dir = make_repo();
        ProjectStore store;
        store.projects().push_back(Project{
            .id = "id-a", .name = "Work",
            .repos = { RepoRef{.path = dir.generic_string(), .alias = "r"} }});
        MainWindow win(&store);
        win.showProject(QStringLiteral("id-a"));

        auto* sidebar = win.findChild<gitgui::ui::ProjectSidebar*>();
        QSignalSpy spy(&win, &MainWindow::repoOpened);
        emit sidebar->repoSelected(QString::fromStdString(dir.generic_string()));

        QCOMPARE(spy.count(), 1);
        std::filesystem::remove_all(dir);
    }
};

#include "test_main_window.moc"
```

- [ ] **Step 3: Run — verify FAIL**

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: FAIL — `MainWindow` has no `ChangesView`, no `repoOpened` signal.

- [ ] **Step 4: Update MainWindow header**

`ui/include/gitgui/ui/MainWindow.hpp` — add a `RepoController`, a `DashboardModel`, and a `repoOpened` relay signal:

```cpp
#pragma once
#include <QMainWindow>
#include <QString>

namespace gitgui { class ProjectStore; }

namespace gitgui::ui {

class ProjectController;
class ProjectSidebar;
class RepoController;
class ChangesView;
class DashboardModel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(gitgui::ProjectStore* store, QWidget* parent = nullptr);

    ProjectController* controller() const { return controller_; }
    QString currentProjectId() const;

    void showProject(const QString& projectId);

signals:
    void openInNewWindowRequested(const QString& projectId);
    void repoOpened(const QString& path);

private:
    ProjectController* controller_;
    ProjectSidebar* sidebar_;
    RepoController* repoController_;
    ChangesView* changesView_;
    DashboardModel* dashboardModel_;
};

}  // namespace gitgui::ui
```

- [ ] **Step 5: Update MainWindow implementation**

`ui/src/MainWindow.cpp`:

```cpp
#include "gitgui/ui/MainWindow.hpp"
#include "gitgui/ui/ProjectController.hpp"
#include "gitgui/ui/ProjectSidebar.hpp"
#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/ChangesView.hpp"
#include "gitgui/ui/DashboardModel.hpp"

#include <QDockWidget>
#include <QLabel>
#include <QListView>
#include <QTabWidget>

namespace gitgui::ui {

MainWindow::MainWindow(gitgui::ProjectStore* store, QWidget* parent)
    : QMainWindow(parent),
      controller_(new ProjectController(store, this)),
      sidebar_(new ProjectSidebar(controller_, this)),
      repoController_(new RepoController(this)),
      changesView_(new ChangesView(this)),
      dashboardModel_(new DashboardModel(this)) {
    setWindowTitle(QStringLiteral("GitGUI"));

    auto* dock = new QDockWidget(QStringLiteral("Projects"), this);
    dock->setObjectName(QStringLiteral("projectsDock"));
    dock->setWidget(sidebar_);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("mainTabs"));
    tabs->addTab(changesView_, QStringLiteral("Changes"));
    tabs->addTab(new QLabel(QStringLiteral("History — Plan 4")), QStringLiteral("History"));
    auto* dashboardView = new QListView(this);
    dashboardView->setObjectName(QStringLiteral("dashboardList"));
    dashboardView->setModel(dashboardModel_);
    tabs->addTab(dashboardView, QStringLiteral("Dashboard"));
    setCentralWidget(tabs);

    connect(sidebar_, &ProjectSidebar::openInNewWindowRequested,
            this, &MainWindow::openInNewWindowRequested);

    // Selecting a repo opens it asynchronously and refreshes the Changes tab.
    connect(sidebar_, &ProjectSidebar::repoSelected, this, [this](const QString& path) {
        repoController_->open(path);
    });
    connect(repoController_, &RepoController::repoOpened, this, [this](const QString& path) {
        emit repoOpened(path);
        repoController_->refreshStatus();
    });

    // Async wiring between controller and ChangesView.
    connect(repoController_, &RepoController::statusChanged,
            changesView_, &ChangesView::setStatus);
    connect(repoController_, &RepoController::diffReady, this,
            [this](const QString& path, const gitgui::DiffResult& result) {
                changesView_->setDiff(result, std::filesystem::path(path.toStdString()));
            });
    connect(changesView_, &ChangesView::fileSelected, this,
            [this](const QString& path, gitgui::DiffTarget target) {
                repoController_->refreshDiff(path, target);
            });
    connect(changesView_, &ChangesView::stageRequested,
            repoController_, &RepoController::stage);
    connect(changesView_, &ChangesView::unstageRequested,
            repoController_, &RepoController::unstage);
    connect(changesView_, &ChangesView::discardRequested,
            repoController_, &RepoController::discard);
    connect(changesView_, &ChangesView::commitRequested,
            repoController_, &RepoController::commit);

    // Activating a project refreshes the dashboard from its repos.
    // activeRepos() is added to ProjectController in Step 6 of this task.
    connect(controller_, &ProjectController::projectActivated, this,
            [this](const QString&) {
                dashboardModel_->refreshAsync(controller_->activeRepos());
            });
}

QString MainWindow::currentProjectId() const {
    return controller_->activeProjectId();
}

void MainWindow::showProject(const QString& projectId) {
    controller_->activate(projectId);
}

}  // namespace gitgui::ui
```

`activeRepos()` is a new `ProjectController` helper added in Step 6 (returns the active project's repos, or empty). Add `#include <filesystem>` and `#include <vector>` to `MainWindow.cpp`.

- [ ] **Step 6: Add ProjectController::activeRepos**

In `ui/include/gitgui/ui/ProjectController.hpp`, add the include `#include <vector>` and `#include "gitgui/ProjectStore.hpp"`, then declare in the public section:

```cpp
    const std::vector<gitgui::RepoRef>& activeRepos() const;
```

In `ui/src/ProjectController.cpp`, implement it by looking up the active project in the store (mirror however `activate` finds projects; the store exposes `projects()`):

```cpp
const std::vector<gitgui::RepoRef>& ProjectController::activeRepos() const {
    static const std::vector<gitgui::RepoRef> kEmpty;
    for (const auto& p : store_->projects()) {
        if (QString::fromStdString(p.id) == activeId_) return p.repos;
    }
    return kEmpty;
}
```

(Verify the `Project` field names against `core/include/gitgui/ProjectStore.hpp` — `id` and `repos` per the existing tests.)

- [ ] **Step 7: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_ui_tests --output-on-failure
```

Expected: PASS — all four `TestMainWindow` slots, and the full UI suite still green.

- [ ] **Step 8: Run the whole suite (Core + UI)**

```bash
ctest --test-dir build --output-on-failure
```

Expected: every test passes (Core Catch2 + UI QtTest).

- [ ] **Step 9: Commit**

```bash
git add ui/include/gitgui/ui/ProjectSidebar.hpp ui/src/ProjectSidebar.cpp ui/include/gitgui/ui/MainWindow.hpp ui/src/MainWindow.cpp ui/include/gitgui/ui/ProjectController.hpp ui/src/ProjectController.cpp tests/ui/test_main_window.cpp
git commit -m "feat(ui): wire Changes + Dashboard tabs to async RepoController/DashboardModel"
```

---

## Self-Review notes (for the executor)

- **Spec coverage:** Core ops were Plan 3a; this plan covers the async layer (Tasks 3–5), Changes UI (Tasks 6–7), and wiring (Tasks 8–9), matching the design's "Async Layer" + "Changes Tab UI" sections.
- **Whole-file staging from the file lists:** Tasks 6–9 wire line/hunk staging via the DiffView. Whole-file stage/unstage (clicking a file and staging all of it) is reachable by selecting all of a hunk's lines; a dedicated "stage whole file" button is **deferred** — note it as a follow-up if the reviewer wants it in-scope.
- **Coroutine lifetime:** if any reviewer flags that fire-and-forget coroutine slots could be destroyed early, the mitigation is already in place — `AsyncRepo` work is anchored by `shared_ptr<Impl>` and `QCoro::Task` root coroutines run to completion detached. Tests use `QCoro::waitFor` to make this deterministic.
- **`std::vector<gitgui::FileStatus>` as a metatype** requires `Q_DECLARE_METATYPE` (Task 2) — without it `QSignalSpy` capture fails to compile.
```

---

## Outcome

Introduced `AsyncRepo` (QtConcurrent + QCoro, per-repo serialization), async `DashboardModel::refreshAsync`, the `ChangesView`/`DiffView` widgets, and coroutine slots on `RepoController`. The async/threading model now lives in [`spec/engineering`](../spec/engineering/engineering.md).
