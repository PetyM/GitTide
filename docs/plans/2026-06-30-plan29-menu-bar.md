# Plan 29 — Title-bar menu bar (File/Edit/View/Repository)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: use superpowers:subagent-driven-development or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-06-30 |
| **Status** | `planned` |
| **Spec** | [`spec/product/app-menu.md` §7](../spec/product/app-menu.md) |
| **Depends on** | [Plan 15 — App menu](2026-06-23-plan15-app-menu.md) |

**Goal:** Add a classic horizontal text menu bar (File · Edit · View ·
Repository) to the title bar, hosting per-repo actions — open repo folder, undo
last commit, discard all, theme quick-switch, merge, rebase, stash/pop. Trim the
app-icon popup to app-level items (Options / About / Quit).

**Architecture:** Mostly menu wiring over the existing
`QML → TitleBar signal → Main.qml → RepoViewModel Q_INVOKABLE → RepoController →
AsyncRepo → GitRepo` chain. Net-new engine: `GitRepo::discardAll()` (hard reset +
remove untracked) and `GitRepo::stashCount()`, surfaced up to two new VM
properties (`dirty`, `stashAvailable`) and four new VM invokables (`discardAll`,
`stashChanges`, `popStash`, `openRepoFolder`). `RebaseTargetDialog` is generalised
into a reusable `BranchPickerDialog`. New QML: `AppMenuBar.qml`, `MenuBarButton.qml`.

**Tech stack:** C++23 core (libgit2), Qt Quick / QML UI, QCoro async, Catch2
(core tests) + Qt Test headless QML (`tests/ui/`, `QT_QPA_PLATFORM=offscreen`).

## Global constraints

- **No Qt in `core/`** — `discardAll`/`stashCount` use only libgit2 + `std`
  ([engineering invariants](../spec/engineering/engineering.md)).
- **Errors are values** — core returns `Expected<T>`; no exceptions across layers.
- **Paths via `generic_u8string()`**, never `.string()`. Use the existing
  `fromGitPath`/`toGitPath` helpers and `workdir()`.
- **Colour from a theme token**, never a hex literal in QML. Reuse existing
  tokens (`theme.surfaceOverlay`, `theme.textPrimary`, `theme.border`, `theme.accent`).
- New `core/` sources are already in `core/CMakeLists.txt` (`gitrepo.cpp`); new
  tests → `tests/CMakeLists.txt`; new QML files → `ui/qml/qml.qrc`.
- **Must keep passing:** the `BranchContextMenu` rebase assertions in
  `tests/ui/test_qml_rebase_entrypoints.cpp`, and all existing core tests. The
  `title_bar_app_menu_has_rebase_item` subtest *will* need updating (Task 8) since
  the rebase item moves out of the icon popup into the Repository menu.
- **Split rename from logic** — Task 6 (rename `RebaseTargetDialog` →
  `BranchPickerDialog`) is its own commit, separate from behavioural changes.

---

## Task 1: Core — `GitRepo::discardAll()`

Full working-tree reset: hard-reset tracked changes to HEAD, then delete
untracked files (git `clean -fd` equivalent). Handles the unborn-HEAD case (no
commit to reset to → just clear the index, then remove untracked).

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare, near `discard` at line 95)
- Modify: `core/src/gitrepo.cpp` (implement, after `discard` ~line 552)
- Test: `tests/test_git_repo_discard.cpp` (append cases)

**Interfaces:**
- Produces: `gittide::Expected<void> GitRepo::discardAll();`

- [ ] **Step 1: Write the failing tests** — append to `tests/test_git_repo_discard.cpp`:

```cpp
TEST_CASE("discardAll resets tracked and deletes untracked", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("tracked.txt", "orig\n");
    tmp.commitAll("init");
    tmp.writeFile("tracked.txt", "changed\n"); // modified tracked
    tmp.writeFile("untracked.txt", "new\n");    // untracked

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->discardAll().has_value());

    REQUIRE(read_file(tmp.path() / "tracked.txt") == "orig\n"); // reset
    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "untracked.txt")); // removed

    auto st = repo->status();
    REQUIRE(st.has_value());
    REQUIRE(st->empty()); // fully clean tree
}

TEST_CASE("discardAll drops a staged new file", "[discard]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("keep.txt", "x\n");
    tmp.commitAll("init");
    tmp.writeFile("staged.txt", "fresh\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"staged.txt", std::nullopt, {}}).has_value());
    REQUIRE(repo->discardAll().has_value());

    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "staged.txt"));
    REQUIRE(repo->status().value().empty());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `ctest --test-dir build -R gittide_core_tests --output-on-failure`
Expected: FAIL — `discardAll` is not a member of `GitRepo`.

- [ ] **Step 3: Declare in `gitrepo.hpp`** (after line 95, the `discard` declaration):

```cpp
    /// Discard *all* working-tree changes: hard-reset tracked files (staged and
    /// unstaged) to HEAD, then delete every untracked file. Leaves ignored files
    /// alone. On an unborn HEAD there is no commit to reset to, so the index is
    /// cleared instead. Returns an error only on a libgit2 failure.
    Expected<void> discardAll();
```

- [ ] **Step 4: Implement in `gitrepo.cpp`** (insert after the `discard` function, ~line 552):

```cpp
Expected<void> GitRepo::discardAll()
{
    // 1. Reset tracked changes (staged + unstaged) back to HEAD.
    if (git_repository_head_unborn(m_repo) == 1)
    {
        // No commit yet: clear the index so nothing stays staged.
        git_index* index = nullptr;
        int rc           = git_repository_index(&index, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index, decltype(&git_index_free)> idx_guard(index, git_index_free);
        git_index_clear(index);
        rc = git_index_write(index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    else
    {
        git_object* head = nullptr;
        int rc           = git_revparse_single(&head, m_repo, "HEAD");
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_object, decltype(&git_object_free)> head_guard(head, git_object_free);

        rc = git_reset(m_repo, head, GIT_RESET_HARD, nullptr);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }

    // 2. Delete untracked files (git clean -fd equivalent). Hard reset leaves
    //    untracked files in place, so enumerate and remove them explicitly.
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show               = GIT_STATUS_SHOW_WORKDIR_ONLY;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    git_status_list* raw = nullptr;
    int rc               = git_status_list_new(&raw, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_status_list, decltype(&git_status_list_free)> list(raw, git_status_list_free);

    const std::filesystem::path wd = workdir();
    size_t n                       = git_status_list_entrycount(list.get());
    for (size_t i = 0; i < n; ++i)
    {
        const git_status_entry* e = git_status_byindex(list.get(), i);
        if (!(e->status & GIT_STATUS_WT_NEW))
            continue;
        const git_diff_file* nf = e->index_to_workdir ? &e->index_to_workdir->new_file : nullptr;
        if (!nf || !nf->path)
            continue;
        std::error_code ec;
        std::filesystem::remove(wd / fromGitPath(nf->path), ec); // best-effort
    }
    return {};
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_core_tests --output-on-failure`
Expected: PASS (both new cases + all existing `[discard]` cases).

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_discard.cpp
git commit -m "feat(core): add GitRepo::discardAll (hard reset + clean untracked)"
```

---

## Task 2: Core — `GitRepo::stashCount()`

Count entries on the stash stack so the UI can enable/disable "Pop latest stash".

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare, near `stashPop` at line 207)
- Modify: `core/src/gitrepo.cpp` (implement, near the other stash functions)
- Test: `tests/test_git_repo_stash.cpp` (new) + register in `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `gittide::Expected<int> GitRepo::stashCount() const;`

- [ ] **Step 1: Write the failing test** — create `tests/test_git_repo_stash.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

TEST_CASE("stashCount reflects the stash stack", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    REQUIRE(repo->stashCount().value() == 0);

    tmp.writeFile("a.txt", "dirty\n");
    REQUIRE(repo->stashSave("wip").value() == true); // a stash was created
    REQUIRE(repo->stashCount().value() == 1);

    REQUIRE(repo->stashPop().has_value());
    REQUIRE(repo->stashCount().value() == 0);
}
```

- [ ] **Step 2: Register the test** — add to `tests/CMakeLists.txt` after the
`test_git_repo_discard.cpp` line (≈ line 15):

```cmake
  test_git_repo_stash.cpp
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --parallel`
Expected: FAIL — `stashCount` is not a member of `GitRepo`.

- [ ] **Step 4: Declare in `gitrepo.hpp`** (after line 207, the `stashPop` declaration):

```cpp
    /// Number of entries currently on the stash stack (stash@{0}, {1}, …).
    /// Zero when the stack is empty. Errors only on a libgit2 failure.
    Expected<int> stashCount() const;
```

- [ ] **Step 5: Implement in `gitrepo.cpp`** (after `stashPop`; `<git2/stash.h>` is already included):

```cpp
Expected<int> GitRepo::stashCount() const
{
    int count  = 0;
    int rc     = git_stash_foreach(
        m_repo,
        [](size_t, const char*, const git_oid*, void* payload) -> int
        {
            ++*static_cast<int*>(payload);
            return 0;
        },
        &count);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return count;
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_core_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_stash.cpp tests/CMakeLists.txt
git commit -m "feat(core): add GitRepo::stashCount"
```

---

## Task 3: Bridge + VM — `discardAll` and the `dirty` property

Thread `discardAll` through `AsyncRepo → RepoController → RepoViewModel`, and add
a `dirty` property (working tree has changes) to drive enable/disable.

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp` + `ui/src/asyncrepo.cpp`
- Modify: `ui/include/gittide/ui/repocontroller.hpp` + `ui/src/repocontroller.cpp`
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp`
- Test: `tests/ui/test_repocontroller_discard_all.cpp` (new) + register

**Interfaces:**
- Consumes: `GitRepo::discardAll()` (Task 1)
- Produces:
  - `QCoro::Task<gittide::Expected<void>> AsyncRepo::discardAll();`
  - `QCoro::Task<void> RepoController::discardAll();` (refreshes status on success)
  - `Q_INVOKABLE void RepoViewModel::discardAll();`
  - `Q_PROPERTY(bool dirty READ dirty NOTIFY changed)` on `RepoViewModel`

- [ ] **Step 1: Write the failing test** — create `tests/ui/test_repocontroller_discard_all.cpp`,
mirroring `tests/ui/test_repocontroller_undo.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <qcorotask.h>

#include "gittide/ui/repocontroller.hpp"
#include "support/temprepo.hpp"

using namespace gittide::ui;

class TestRepoControllerDiscardAll : public QObject
{
    Q_OBJECT
private slots:
    void discard_all_clears_tracked_and_untracked()
    {
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");
        tmp.writeFile("a.txt", "dirty\n");   // modified tracked
        tmp.writeFile("b.txt", "new\n");      // untracked

        RepoController ctrl;
        QSignalSpy statusSpy(&ctrl, &RepoController::statusChanged);
        ctrl.open(tmp.path());
        QVERIFY(QTest::qWaitFor([&] { return ctrl.isOpen(); }, 2000));

        QCoro::waitFor(ctrl.discardAll());

        // Final statusChanged carries an empty list (clean tree).
        QVERIFY(QTest::qWaitFor([&] {
            return !statusSpy.isEmpty()
                && statusSpy.last().at(0)
                       .value<std::vector<gittide::FileStatus>>().empty();
        }, 2000));
        QVERIFY(QFile::exists(QString::fromStdString((tmp.path() / "a.txt").generic_u8string())));
        QVERIFY(!QFile::exists(QString::fromStdString((tmp.path() / "b.txt").generic_u8string())));
    }
};

QTEST_MAIN(TestRepoControllerDiscardAll)
#include "test_repocontroller_discard_all.moc"
```

> Note: confirm the `QCoro::waitFor` / `QTEST_MAIN` shape against the existing
> `tests/ui/test_repocontroller_undo.cpp`; copy its includes/harness exactly if
> it differs (e.g. a shared `main.cpp` vs `QTEST_MAIN`). The UI test target uses
> `ui/main.cpp`, so register the source and match that file's pattern (no
> `QTEST_MAIN`, plain `QObject` test class with `private slots`).

- [ ] **Step 2: Register the test** — add to `tests/CMakeLists.txt` in the
`gittide_ui_test_sources` list (after line 81, the undo test):

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_discard_all.cpp
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `RepoController::discardAll` undeclared.

- [ ] **Step 4: Add `AsyncRepo::discardAll`** — declare in `asyncrepo.hpp` (after
`discard`, line 49):

```cpp
    QCoro::Task<gittide::Expected<void>> discardAll();
```

Implement in `asyncrepo.cpp` (after `AsyncRepo::discard`, ~line 95), mirroring it:

```cpp
QCoro::Task<gittide::Expected<void>> AsyncRepo::discardAll()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.discardAll();
        });
}
```

- [ ] **Step 5: Add `RepoController::discardAll`** — declare in `repocontroller.hpp`
(after `discard`, line 57):

```cpp
    QCoro::Task<void> discardAll();
```

Implement in `repocontroller.cpp` (model on `RepoController::discard`):

```cpp
QCoro::Task<void> RepoController::discardAll()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    WatchMute                mute(m_watcher);
    auto result = co_await m_repo->discardAll();
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
}
```

- [ ] **Step 6: Add `RepoViewModel::discardAll` + `dirty`** — in `repoviewmodel.hpp`:

Property (after the `repoOpen` property, ~line 36):

```cpp
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
```

Getter declaration (near `repoOpen()`, line 103) and invokable (after
`discardFile`, line 244):

```cpp
    bool dirty() const;
    // ...
    Q_INVOKABLE void discardAll();
```

In `repoviewmodel.cpp` (place `dirty()` near `repoOpen()`, `discardAll()` near
`discardFile()`):

```cpp
bool RepoViewModel::dirty() const
{
    return m_files->rowCount(QModelIndex()) > 0;
}

void RepoViewModel::discardAll()
{
    QCoro::connect(m_controller->discardAll(), this, [] {});
}
```

> `m_files` is the `ChangedFilesModel*` returned by `changedFiles()` (line 120).
> `changed()` already fires on every status refresh (`onStatus`), so `dirty`
> re-evaluates automatically — no extra notify wiring.

- [ ] **Step 7: Run to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add ui/include ui/src tests/ui/test_repocontroller_discard_all.cpp tests/CMakeLists.txt
git commit -m "feat(ui): wire discardAll through AsyncRepo/controller/VM; add dirty property"
```

---

## Task 4: Bridge + VM — stash save / pop + `stashAvailable`

Expose stash save and pop to the VM (core + AsyncRepo already have
`stashSave`/`stashPop`), and add a `stashAvailable` property fed by a new
`stashCountChanged(int)` controller signal, refreshed in the cascade and after
stash ops.

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp` + `.cpp`
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` + `.cpp`
- Test: `tests/ui/test_repocontroller_stash.cpp` (new) + register

**Interfaces:**
- Consumes: `AsyncRepo::stashSave/stashPop` (exist), `AsyncRepo::stashCount` (added here)
- Produces:
  - `QCoro::Task<gittide::Expected<int>> AsyncRepo::stashCount();`
  - `RepoController::stashChanges()`, `popStash()`, `refreshStashState()`, signal `stashCountChanged(int)`
  - `RepoViewModel::stashChanges()`, `popStash()`, `Q_PROPERTY(bool stashAvailable …)`

- [ ] **Step 1: Write the failing test** — create `tests/ui/test_repocontroller_stash.cpp`
(match the harness of `tests/ui/test_repocontroller_undo.cpp`):

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <qcorotask.h>

#include "gittide/ui/repocontroller.hpp"
#include "support/temprepo.hpp"

using namespace gittide::ui;

class TestRepoControllerStash : public QObject
{
    Q_OBJECT
private slots:
    void stash_then_pop_round_trips_and_reports_count()
    {
        gittide::test::TempRepo tmp;
        tmp.writeFile("a.txt", "orig\n");
        tmp.commitAll("init");
        tmp.writeFile("a.txt", "dirty\n");

        RepoController ctrl;
        QSignalSpy countSpy(&ctrl, &RepoController::stashCountChanged);
        ctrl.open(tmp.path());
        QVERIFY(QTest::qWaitFor([&] { return ctrl.isOpen(); }, 2000));

        QCoro::waitFor(ctrl.stashChanges());
        QVERIFY(QTest::qWaitFor([&] {
            return !countSpy.isEmpty() && countSpy.last().at(0).toInt() == 1;
        }, 2000));

        QCoro::waitFor(ctrl.popStash());
        QVERIFY(QTest::qWaitFor([&] {
            return countSpy.last().at(0).toInt() == 0;
        }, 2000));
        // Changes restored after pop.
        QFile f(QString::fromStdString((tmp.path() / "a.txt").generic_u8string()));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("dirty\n"));
    }
};

// match the existing UI test harness (uses ui/main.cpp — no QTEST_MAIN)
#include "test_repocontroller_stash.moc"
```

- [ ] **Step 2: Register the test** — add to `tests/CMakeLists.txt`:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_stash.cpp
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `RepoController::stashChanges` / `stashCountChanged` undeclared.

- [ ] **Step 4: Add `AsyncRepo::stashCount`** — declare in `asyncrepo.hpp` (after
`stashPop`, line 120):

```cpp
    /// Number of entries on the stash stack.
    QCoro::Task<gittide::Expected<int>> stashCount();
```

Implement in `asyncrepo.cpp` (after `AsyncRepo::stashPop`, ~line 529):

```cpp
QCoro::Task<gittide::Expected<int>> AsyncRepo::stashCount()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashCount();
        });
}
```

- [ ] **Step 5: Add controller slots + signal** — in `repocontroller.hpp`,
declare slots (after `discardAll` from Task 3) and a private helper + signal:

```cpp
    // public slots:
    QCoro::Task<void> stashChanges(); // stash the working tree (no message)
    QCoro::Task<void> popStash();     // pop the most-recent stash

    // signals:
    void stashCountChanged(int count);

    // private:
    QCoro::Task<void> refreshStashState(); // emits stashCountChanged
```

Implement in `repocontroller.cpp`:

```cpp
QCoro::Task<void> RepoController::stashChanges()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    WatchMute                mute(m_watcher);
    auto result = co_await m_repo->stashSave(QString());
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshStashState();
}

QCoro::Task<void> RepoController::popStash()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    WatchMute                mute(m_watcher);
    auto result = co_await m_repo->stashPop();
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshStashState();
}

QCoro::Task<void> RepoController::refreshStashState()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto count = co_await m_repo->stashCount();
    if (!self || !count)
        co_return;
    emit stashCountChanged(*count);
}
```

- [ ] **Step 6: Refresh stash count in the cascade** — in `repocontroller.cpp`,
add `co_await refreshStashState();` to the tail of `refreshAll()` so the count is
current on open and on any git-dir change.

- [ ] **Step 7: Add VM members** — in `repoviewmodel.hpp`:

Property (after the `dirty` property from Task 3):

```cpp
    Q_PROPERTY(bool stashAvailable READ stashAvailable NOTIFY stashCountChanged)
```

Getter + invokables + a notify signal + private slot + member:

```cpp
    // public:
    bool stashAvailable() const { return m_stashCount > 0; }
    Q_INVOKABLE void stashChanges();
    Q_INVOKABLE void popStash();

    // signals:
    void stashCountChanged();

    // private slot:
    void onStashCount(int count);

    // private:
    int m_stashCount = 0;
```

In `repoviewmodel.cpp` — connect in the constructor (next to the other
`connect(m_controller, …)` calls, ~line 43):

```cpp
    connect(m_controller, &RepoController::stashCountChanged, this, &RepoViewModel::onStashCount);
```

Implementations:

```cpp
void RepoViewModel::onStashCount(int count)
{
    if (m_stashCount == count)
        return;
    m_stashCount = count;
    emit stashCountChanged();
}

void RepoViewModel::stashChanges()
{
    QCoro::connect(m_controller->stashChanges(), this, [] {});
}

void RepoViewModel::popStash()
{
    QCoro::connect(m_controller->popStash(), this, [] {});
}
```

- [ ] **Step 8: Run to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add ui/include ui/src tests/ui/test_repocontroller_stash.cpp tests/CMakeLists.txt
git commit -m "feat(ui): expose stash save/pop + stashAvailable to the VM"
```

---

## Task 5: VM — `openRepoFolder()`

Open the repository root in the OS-native file manager. Per the open-targets
rule (spec §7.4): **folders/repos open in the OS-native handler**; files keep
using the existing `openInEditor`. Distinct from `revealInFileManager`, which
opens a file's *parent* directory.

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` (after `revealInFileManager`, line 246)
- Modify: `ui/src/repoviewmodel.cpp` (after `revealInFileManager`, ~line 1034)

**Interfaces:**
- Produces: `Q_INVOKABLE void RepoViewModel::openRepoFolder();`

> No headless automated test — `QDesktopServices::openUrl` has no observable
> return in the offscreen harness. Verified via the Task 8 QML wiring (item
> triggers the stub) and manual launch.

- [ ] **Step 1: Declare** in `repoviewmodel.hpp` (after line 246):

```cpp
    /// Open the repository root folder in the OS-native file manager.
    Q_INVOKABLE void openRepoFolder();
```

- [ ] **Step 2: Implement** in `repoviewmodel.cpp` (after `revealInFileManager`):

```cpp
void RepoViewModel::openRepoFolder()
{
    const QString root = repoPath();
    if (root.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(root));
}
```

- [ ] **Step 3: Build to verify it compiles**

Run: `cmake --build build --parallel`
Expected: builds clean (`QDesktopServices`/`QUrl` already included — they back
`openInEditor`).

- [ ] **Step 4: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp
git commit -m "feat(ui): add RepoViewModel::openRepoFolder (open repo root natively)"
```

---

## Task 6: Generalise `RebaseTargetDialog` → `BranchPickerDialog` (rename only)

Pure refactor — no behaviour change. Rename the dialog and parameterise its
prompt + action label so both rebase and merge can reuse it. The rebase route
keeps working unchanged. **Separate commit from any logic.**

**Files:**
- Rename: `ui/qml/RebaseTargetDialog.qml` → `ui/qml/BranchPickerDialog.qml`
- Modify: `ui/qml/qml.qrc` (update the entry)
- Modify: `ui/qml/Main.qml` (instantiate `BranchPickerDialog` for rebase)

**Interfaces:**
- Produces: `BranchPickerDialog` with properties `repo`, `selectedRef`,
  `promptText: string`, `actionLabel: string`, signal `accepted()`.

- [ ] **Step 1: Rename the file** (preserve git history):

```bash
git mv ui/qml/RebaseTargetDialog.qml ui/qml/BranchPickerDialog.qml
```

- [ ] **Step 2: Generalise the contents** — replace `ui/qml/BranchPickerDialog.qml` with:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Reusable local-branch picker. Lists local branches except the current one
// (remote=false, isHead=false); calls accept() (Dialog.accepted) on confirm.
// Read selectedRef in the onAccepted handler. promptText and actionLabel are
// set by the caller (rebase vs merge).
Dialog {
    id: dialog
    objectName: "branchPickerDialog"
    modal: true
    anchors.centerIn: parent
    width: 380
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    /// The RepoViewModel — provides the branches model and currentBranch name.
    property var repo

    /// Prompt shown above the list, e.g. "Rebase main onto:".
    property string promptText: ""

    /// Confirm-button label, e.g. "Rebase" / "Merge".
    property string actionLabel: "OK"

    /// Currently highlighted branch name; empty until the user selects one.
    property string selectedRef: ""

    onOpened: dialog.selectedRef = ""

    background: OverlayCard {}

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: dialog.promptText
            color: theme.textPrimary
            font.pixelSize: 14
            Layout.fillWidth: true
        }

        ListView {
            id: branchList
            objectName: "branchPickerList"
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true
            model: repo ? repo.branches : null

            delegate: ItemDelegate {
                id: branchDelegate
                required property string branchName
                required property bool   isHead
                required property bool   remote
                width: branchList.width
                visible: !branchDelegate.remote && !branchDelegate.isHead
                height: visible ? implicitHeight : 0
                highlighted: dialog.selectedRef === branchDelegate.branchName
                onClicked: dialog.selectedRef = branchDelegate.branchName
                contentItem: Label {
                    text: branchDelegate.branchName
                    color: branchDelegate.highlighted ? theme.accent : theme.textPrimary
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                background: Rectangle {
                    color: branchDelegate.highlighted ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.15)
                                                      : (branchDelegate.hovered ? theme.surfaceRaised : "transparent")
                    radius: 4
                }
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Layout.margins: 16
        Item { Layout.fillWidth: true }
        AppButton {
            variant: "secondary"
            text: "Cancel"
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "branchPickerConfirm"
            variant: "primary"
            text: dialog.actionLabel
            enabled: dialog.selectedRef.length > 0
            onClicked: dialog.accept()
        }
    }
}
```

- [ ] **Step 3: Update `qml.qrc`** — change the `RebaseTargetDialog.qml` entry
(line 48) to:

```xml
    <file>BranchPickerDialog.qml</file>
```

- [ ] **Step 4: Update the rebase instance in `Main.qml`** — replace the
`RebaseTargetDialog { … }` block (lines 211–215) with:

```qml
    BranchPickerDialog {
        id: rebaseTargetDialog
        repo: repoVm
        title: "Rebase branch"
        actionLabel: "Rebase"
        promptText: repoVm ? ("Rebase " + repoVm.currentBranch + " onto:") : "Rebase onto:"
        onAccepted: if (repoVm) repoVm.startRebase(rebaseTargetDialog.selectedRef)
    }
```

- [ ] **Step 5: Run to verify the rebase route still passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `test_qml_rebase_entrypoints` still finds `rebaseMenuItem` and
fires `startRebase`.

- [ ] **Step 6: Commit (rename + generalisation only)**

```bash
git add ui/qml/BranchPickerDialog.qml ui/qml/qml.qrc ui/qml/Main.qml
git commit -m "refactor(ui): generalise RebaseTargetDialog into BranchPickerDialog"
```

---

## Task 7: New QML components — `MenuBarButton` + `AppMenuBar`

A flat themed text button that opens its `AppMenu`, and a `RowLayout` of them.
The bar declares the per-repo signals and re-emits each menu item's action; menus
are populated by `TitleBar` in Task 8 (the bar exposes default-property slots for
the menus).

**Files:**
- Create: `ui/qml/MenuBarButton.qml`
- Create: `ui/qml/AppMenuBar.qml`
- Modify: `ui/qml/qml.qrc`

**Interfaces:**
- Produces: `MenuBarButton { property string label; default property alias menu }`
  pattern and `AppMenuBar` hosting File/Edit/View/Repository buttons.

- [ ] **Step 1: Write a failing QML test** — create
`tests/ui/test_qml_menu_bar.cpp`, mirroring `test_qml_rebase_entrypoints.cpp`'s
`loadMain` + stub. Reuse a minimal repo stub (copy `RebaseEntryStub`, add the new
members below). Assert the four menu-bar buttons exist:

```cpp
// inside a private slot, after loadMain(...):
QObject* bar = root->findChild<QObject*>(QStringLiteral("appMenuBar"));
QVERIFY2(bar != nullptr, "appMenuBar not found in TitleBar");
for (const QString& name : { "menuBtnFile", "menuBtnEdit", "menuBtnView", "menuBtnRepository" })
{
    QObject* btn = bar->findChild<QObject*>(name);
    QVERIFY2(btn != nullptr, qPrintable(name + " button not found"));
}
```

The stub needs these extra members (added once, reused in Task 8):

```cpp
    // properties
    Q_PROPERTY(bool dirty READ dirty CONSTANT)
    Q_PROPERTY(bool stashAvailable READ stashAvailable CONSTANT)
    // getters
    bool dirty() const { return true; }
    bool stashAvailable() const { return true; }
    // invokables
    Q_INVOKABLE void discardAll() { ++m_discardAllCalls; }
    Q_INVOKABLE void stashChanges() { ++m_stashCalls; }
    Q_INVOKABLE void popStash() { ++m_popCalls; }
    Q_INVOKABLE void openRepoFolder() { ++m_openFolderCalls; }
    Q_INVOKABLE void undoLastCommit() { ++m_undoCalls; }
    // counters (public)
    int m_discardAllCalls = 0, m_stashCalls = 0, m_popCalls = 0,
        m_openFolderCalls = 0, m_undoCalls = 0;
```

Register `tests/ui/test_qml_menu_bar.cpp` in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `appMenuBar` not found (component doesn't exist yet).

- [ ] **Step 3: Create `MenuBarButton.qml`**:

```qml
import QtQuick
import QtQuick.Controls.Basic

// Flat themed text button in the title-bar menu bar. Opens the AppMenu assigned
// to its `menu` property below the button. Hover tint matches the app-icon button.
Button {
    id: control

    property string label: ""
    property Menu menu: null

    flat: true
    text: control.label
    implicitHeight: 40
    leftPadding: 10
    rightPadding: 10

    contentItem: Label {
        text: control.text
        color: theme.textPrimary
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    background: Rectangle {
        color: (control.hovered || (control.menu && control.menu.opened))
               ? theme.surfaceOverlay : "transparent"
        radius: 4
    }

    onClicked: if (control.menu) control.menu.popup(control, 0, control.height)
}
```

- [ ] **Step 4: Create `AppMenuBar.qml`** — a row of buttons, each owning an
`AppMenu`. It declares all per-repo signals and wires every item; the host binds
the signals in Task 8. Theme items act directly on `appSettings`/`theme`.

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Horizontal text menu bar for the title bar (spec §7). Hosts File / Edit / View
// / Repository. Each button opens its AppMenu. Per-repo items are enabled only
// when a repo is open; destructive items use AppMenuItem.destructive.
RowLayout {
    id: bar
    objectName: "appMenuBar"
    spacing: 2

    // App settings store (themeMode) + repo VM are injected by the host.
    property var appSettings
    property var repo

    // Per-repo action requests — bound by the host (Main.qml via TitleBar).
    signal openRepoFolderRequested()
    signal undoLastCommitRequested()
    signal discardAllRequested()
    signal mergeRequested()
    signal rebaseRequested()
    signal stashRequested()
    signal popStashRequested()

    readonly property bool repoReady: !!repo && repo.repoOpen
    readonly property bool busy: !!repo && (repo.rebaseInProgress || repo.mergeInProgress)

    MenuBarButton {
        objectName: "menuBtnFile"
        label: "File"
        menu: AppMenu {
            objectName: "menuFile"
            AppMenuItem {
                objectName: "openRepoFolderItem"
                text: "Open repository folder"
                enabled: bar.repoReady
                onTriggered: bar.openRepoFolderRequested()
            }
        }
    }

    MenuBarButton {
        objectName: "menuBtnEdit"
        label: "Edit"
        menu: AppMenu {
            objectName: "menuEdit"
            AppMenuItem {
                objectName: "undoLastCommitItem"
                text: "Undo last commit"
                enabled: bar.repoReady && !bar.busy
                onTriggered: bar.undoLastCommitRequested()
            }
            AppMenuItem {
                objectName: "discardAllItem"
                text: "Discard all changes"
                destructive: true
                enabled: bar.repoReady && !!bar.repo && bar.repo.dirty
                onTriggered: bar.discardAllRequested()
            }
        }
    }

    MenuBarButton {
        objectName: "menuBtnView"
        label: "View"
        menu: AppMenu {
            objectName: "menuView"
            Menu {
                title: "Theme"
                AppMenuItem {
                    objectName: "themeSystemItem"
                    text: "System"
                    onTriggered: { theme.setMode(0); if (bar.appSettings) bar.appSettings.themeMode = 0 }
                }
                AppMenuItem {
                    objectName: "themeDarkItem"
                    text: "Dark"
                    onTriggered: { theme.setMode(1); if (bar.appSettings) bar.appSettings.themeMode = 1 }
                }
                AppMenuItem {
                    objectName: "themeLightItem"
                    text: "Light"
                    onTriggered: { theme.setMode(2); if (bar.appSettings) bar.appSettings.themeMode = 2 }
                }
            }
        }
    }

    MenuBarButton {
        objectName: "menuBtnRepository"
        label: "Repository"
        menu: AppMenu {
            objectName: "menuRepository"
            AppMenuItem {
                objectName: "mergeItem"
                text: "Merge into current branch…"
                enabled: bar.repoReady && !bar.busy
                onTriggered: bar.mergeRequested()
            }
            AppMenuItem {
                objectName: "rebaseItem"
                text: "Rebase current branch…"
                enabled: bar.repoReady && !bar.busy
                onTriggered: bar.rebaseRequested()
            }
            MenuSeparator {
                padding: 6
                contentItem: Rectangle { implicitHeight: 1; color: theme.border }
            }
            AppMenuItem {
                objectName: "stashItem"
                text: "Stash all changes"
                enabled: bar.repoReady && !!bar.repo && bar.repo.dirty
                onTriggered: bar.stashRequested()
            }
            AppMenuItem {
                objectName: "popStashItem"
                text: "Pop latest stash"
                enabled: bar.repoReady && !!bar.repo && bar.repo.stashAvailable
                onTriggered: bar.popStashRequested()
            }
        }
    }
}
```

> The `Theme ▸` submenu uses the plain `Menu` title to get the cascade arrow;
> the `theme.setMode` values (0 System · 1 Dark · 2 Light) match `OptionsDialog`
> and the `QSettings` `themeMode` key (app-menu spec §1).

- [ ] **Step 5: Register the components in `qml.qrc`** — add next to the other
title-bar files (near line 41–43):

```xml
    <file>MenuBarButton.qml</file>
    <file>AppMenuBar.qml</file>
```

- [ ] **Step 6: Run the menu-bar test** (it still fails until Task 8 mounts the
bar inside `TitleBar`, but the components must compile). Build now to catch QML
syntax errors:

Run: `cmake --build build --parallel`
Expected: builds clean. (The `appMenuBar`-not-found assertion stays red until
Task 8.)

- [ ] **Step 7: Commit**

```bash
git add ui/qml/MenuBarButton.qml ui/qml/AppMenuBar.qml ui/qml/qml.qrc tests/ui/test_qml_menu_bar.cpp tests/CMakeLists.txt
git commit -m "feat(ui): add AppMenuBar + MenuBarButton components"
```

---

## Task 8: Mount the bar in `TitleBar`, wire `Main.qml`, trim the icon popup

Place `AppMenuBar` after the app icon, trim the icon popup to Options/About/Quit,
forward the new signals, and add the merge picker + discard-all confirm dialogs.

**Files:**
- Modify: `ui/qml/TitleBar.qml`
- Modify: `ui/qml/Main.qml`
- Test: `tests/ui/test_qml_menu_bar.cpp` (extend with trigger assertions)

**Interfaces:**
- Consumes: `AppMenuBar` (Task 7), `BranchPickerDialog` (Task 6),
  `DiscardChangesDialog` (existing), VM invokables (Tasks 3–5).

- [ ] **Step 1: Extend the QML test** — add assertions that triggering items
calls the stub. Example (Repository ▸ Stash, Edit ▸ Discard all, File ▸ Open):

```cpp
void menu_bar_items_invoke_repo_actions()
{
    /* …loadMain with the stub… */
    QObject* bar = root->findChild<QObject*>("appMenuBar");
    QVERIFY(bar);

    QObject* stash = bar->findChild<QObject*>("stashItem");
    QVERIFY(QMetaObject::invokeMethod(stash, "triggered"));
    QTest::qWait(50);
    QCOMPARE(stub.m_stashCalls, 1);

    QObject* openFolder = bar->findChild<QObject*>("openRepoFolderItem");
    QVERIFY(QMetaObject::invokeMethod(openFolder, "triggered"));
    QTest::qWait(50);
    QCOMPARE(stub.m_openFolderCalls, 1);

    // Discard all routes through a confirm dialog; trigger then accept it.
    QObject* discard = bar->findChild<QObject*>("discardAllItem");
    QVERIFY(QMetaObject::invokeMethod(discard, "triggered"));
    QTest::qWait(50);
    QObject* dlg = root->findChild<QObject*>("discardAllDialog");
    QVERIFY2(dlg != nullptr, "discardAllDialog not found");
    QVERIFY(QMetaObject::invokeMethod(dlg, "accept"));
    QTest::qWait(50);
    QCOMPARE(stub.m_discardAllCalls, 1);
}
```

Add `Q_INVOKABLE void startMerge(const QString&)` already exists on the stub; add
a `startMergeCalls` counter if asserting the merge route.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `appMenuBar` / `discardAllDialog` not found (not mounted yet).

- [ ] **Step 3: Trim the icon popup + mount the bar in `TitleBar.qml`** — declare
the new signals (after line 17):

```qml
    signal openRepoFolderRequested()
    signal discardAllRequested()
    signal mergeRequested()
    signal stashRequested()
    signal popStashRequested()
```

First add an injected `appSettings` property to `TitleBar` (it is a `Main.qml`
id, not a global context property like `repoVm`/`theme`, so the bar can't see it
otherwise). Near the existing `isMac` property (line 19):

```qml
    // Injected from Main.qml — the live QSettings store (themeMode etc.).
    property var appSettings
```

Remove the `rebaseMenuItem` and `undoLastCommitMenuItem` from `appMenuPopup`
(lines 114–126) and the now-stranded `MenuSeparator` if it leaves the popup as
Options/About/Quit. The popup becomes:

```qml
            AppMenu {
                id: appMenuPopup
                objectName: "appMenuPopup"

                AppMenuItem {
                    objectName: "optionsMenuItem"
                    text: "Options…"
                    onTriggered: titleBar.optionsRequested()
                }
                AppMenuItem {
                    objectName: "aboutMenuItem"
                    text: "About GitTide"
                    onTriggered: titleBar.aboutRequested()
                }
                MenuSeparator {
                    padding: 6
                    contentItem: Rectangle { implicitHeight: 1; color: theme.border }
                }
                AppMenuItem {
                    objectName: "quitMenuItem"
                    text: "Quit"
                    onTriggered: Qt.quit()
                }
            }
```

Add the bar immediately after the icon `Button` (after line 137, inside the outer
`RowLayout`, before the drag spacer `Item`):

```qml
        AppMenuBar {
            id: appMenuBar
            Layout.leftMargin: 4
            appSettings: appSettings
            repo: repoVm
            onOpenRepoFolderRequested: titleBar.openRepoFolderRequested()
            onUndoLastCommitRequested: titleBar.undoLastCommitRequested()
            onDiscardAllRequested: titleBar.discardAllRequested()
            onMergeRequested: titleBar.mergeRequested()
            onRebaseRequested: titleBar.rebaseRequested()
            onStashRequested: titleBar.stashRequested()
            onPopStashRequested: titleBar.popStashRequested()
        }
```

> `repoVm` and `theme` are global context properties (visible everywhere);
> `appSettings` is a `Main.qml` id, so it is threaded in via the `TitleBar`
> property added above. `AppMenuBar` reads `appSettings`/`repo` as injected
> properties and `theme` directly.

- [ ] **Step 4: Bind the new signals + add dialogs in `Main.qml`** — pass
`appSettings` into the `TitleBar` (add to its property block, near `id: titleBar`):

```qml
            appSettings: appSettings
```

then extend the `TitleBar { … }` handlers (after line 82):

```qml
            onOpenRepoFolderRequested: if (repoVm) repoVm.openRepoFolder()
            onDiscardAllRequested: discardAllDialog.open()
            onMergeRequested: mergeTargetDialog.open()
            onStashRequested: if (repoVm) repoVm.stashChanges()
            onPopStashRequested: if (repoVm) repoVm.popStash()
```

Add the merge picker + discard-all confirm next to `rebaseTargetDialog` (after
line 215):

```qml
    BranchPickerDialog {
        id: mergeTargetDialog
        repo: repoVm
        title: "Merge branch"
        actionLabel: "Merge"
        promptText: repoVm ? ("Merge selected branch into " + repoVm.currentBranch + ":")
                           : "Merge into current branch:"
        onAccepted: if (repoVm) repoVm.startMerge(mergeTargetDialog.selectedRef)
    }
    DiscardChangesDialog {
        id: discardAllDialog
        objectName: "discardAllDialog"
        fileName: "all working-tree changes"
        onAccepted: if (repoVm) repoVm.discardAll()
    }
```

> Reusing `DiscardChangesDialog` yields the message *Discard changes to "all
> working-tree changes"? This cannot be undone.* — acceptable for the first cut.
> If the wording reads oddly, add an optional `bodyText` property to
> `DiscardChangesDialog` in a follow-up; out of scope here.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — menu-bar buttons found, items trigger the stub, discard-all
routes through the confirm dialog.

- [ ] **Step 6: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS — including `test_qml_rebase_entrypoints` (icon popup no longer has
`rebaseMenuItem`; **update that test** if it still asserts the rebase item lives
in `appMenuPopup` — the rebase route now lives in the Repository menu as
`rebaseItem`. Adjust the assertion in `title_bar_app_menu_has_rebase_item` to
look up `rebaseItem` under `appMenuBar` instead, or move the check accordingly).

- [ ] **Step 7: Commit**

```bash
git add ui/qml/TitleBar.qml ui/qml/Main.qml tests/ui/test_qml_menu_bar.cpp tests/ui/test_qml_rebase_entrypoints.cpp
git commit -m "feat(ui): mount AppMenuBar in title bar; wire merge/discard/stash routes"
```

---

## Task 9: Manual verification + close-out

- [ ] **Step 1: Launch and exercise the menu bar** — build and run the app
(`cmake --build build --parallel`, then run the app binary). Confirm against spec §7:
  - File ▸ Open repository folder opens the repo root in the OS file manager.
  - Edit ▸ Undo last commit / Discard all changes (red, asks to confirm).
  - View ▸ Theme ▸ System/Dark/Light switches live.
  - Repository ▸ Merge…/Rebase… open the branch picker; Stash all / Pop latest
    stash enable/disable with tree state.
  - Items disable correctly with no repo open / mid-merge / mid-rebase / clean tree.

- [ ] **Step 2: Fill the plan Outcome** — complete the **Outcome** section below.

- [ ] **Step 3: Add the index row** — in `docs/plans/index.md`, add:

```markdown
| [Plan 29 — Title-bar menu bar](2026-06-30-plan29-menu-bar.md) | 2026-06-30 | done | product · engineering · design |
```

- [ ] **Step 4: Flip the spec Status** — in `docs/spec/product/app-menu.md` §7,
change `Status` from `spec` to the shipped state, and update §7.2's `rebaseItem`
note if any objectName changed during implementation.

- [ ] **Step 5: Commit the close-out**

```bash
git add docs/plans/2026-06-30-plan29-menu-bar.md docs/plans/index.md docs/spec/product/app-menu.md
git commit -m "docs: close out Plan 29 (title-bar menu bar)"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: `spec/product/app-menu.md` §7.
> - Code: `GitRepo::discardAll`/`stashCount`; VM `discardAll`/`stashChanges`/
>   `popStash`/`openRepoFolder` + `dirty`/`stashAvailable`; QML `AppMenuBar`,
>   `MenuBarButton`, `BranchPickerDialog`.
