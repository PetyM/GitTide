# Plan 31 — Stash viewing & management

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-06-30 |
| **Status** | `planned` |
| **Spec** | [`spec/product/product.md §Stash`](../spec/product/product.md#stash), [`spec/engineering/engineering.md §Stash management`](../spec/engineering/engineering.md), decision [D44](../decisions.md) |
| **Depends on** | Plan 29 (menu bar — already wires `stashChanges`/`popStash`); the existing `stashSave`/`stashPop`/`stashCount` primitives |

**Goal:** Expose git's stash **stack** in the GUI — a collapsible *Stashes* panel
in the Changes tab that lists every entry, previews a selected entry's diff in the
shared diff panel, and offers per-entry Apply / Pop / Drop plus a Clear-all.

**Architecture:** Pure-git stack ops land on `GitRepo` in `core/` over the
libgit2 `git_stash_*` family (`Expected<T>`, no Qt). A new `StashEntry` core
struct (index, message, hex `oid`) crosses the boundary. `AsyncRepo` wraps each
as a `QCoro::Task`; `RepoController` runs them on the worker repo and refreshes
status + the stash list. A new `StashListModel` (`QAbstractListModel`) feeds QML;
`RepoViewModel` owns it behind a `stashes` property and exposes invokables.
**Preview reuses the existing commit-diff path**: a stash entry *is* a commit, so
`commitFiles(oid)` + `commitDiff(oid, file)` (stash tree vs its base parent =
`git stash show`) populate the read-only `commitFiles`/`commitDiff` models — no
new core diff method. A `stashPreviewActive` flag flips ChangesPane into a
read-only preview mode.

**Tech stack:** C++23, libgit2 (`git_stash_foreach` / `git_stash_apply` /
`git_stash_pop` / `git_stash_drop`), Catch2 (core), Qt 6 Quick + QtTest (ui),
QCoro coroutines, `QAbstractListModel`.

## Global constraints

- **No Qt in `core/`** and **libgit2 stays private to `core/`** — `StashEntry` is
  plain `std`; the public header exposes no libgit2 type. See
  [`spec/engineering/engineering.md`](../spec/engineering/engineering.md).
- **Errors are values:** core returns `Expected<T>`; conflicts surface as a
  `GitError`, never an exception. On apply/pop conflict the stash is **preserved**
  (D44) — libgit2 already does not drop on failure.
- New `core/` sources → none (edits only). New `ui/` sources
  (`stashlistmodel.*`, `StashPanel.qml`) → `ui/CMakeLists.txt`. New tests → the
  matching list in `tests/CMakeLists.txt`.
- **Must keep passing:** existing `[stash]` core test, `test_repocontroller_stash`,
  the `AppMenuBar` `stashItem`/`popStashItem` wiring (Plan 29), and
  `stashAvailable`/`stashCountChanged`.
- **TDD, frequent commits, one logical change per commit.** Conventional-commit
  subjects (`feat(core)` / `feat(ui)` / `test(...)`). Allman braces, `m_` members,
  lowercase filenames — `.clang-format` conforms.

---

## Task 1: Core — `stashList()` enumerates the stack

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (add struct + method near the existing stash methods, ~line 209)
- Modify: `core/src/gitrepo.cpp` (impl near `stashCount`, ~line 1944)
- Test: `tests/test_git_repo_stash.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct StashEntry
  {
      size_t      index;   // stash@{index}; 0 == newest
      std::string message; // e.g. "WIP on master: <subject>"
      std::string oid;     // 40-char hex of the stash commit
  };
  Expected<std::vector<StashEntry>> stashList() const; // newest first (index 0..n)
  ```

- [ ] **Step 1: Write the failing test**

Append to `tests/test_git_repo_stash.cpp`:

```cpp
TEST_CASE("stashList returns entries newest-first with message and oid", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stashList().value().empty());

    tmp.writeFile("a.txt", "first\n");
    REQUIRE(repo->stashSave("one").value() == true);
    tmp.writeFile("a.txt", "second\n");
    REQUIRE(repo->stashSave("two").value() == true);

    auto list = repo->stashList();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    // Newest is stash@{0}.
    REQUIRE((*list)[0].index == 0);
    REQUIRE((*list)[1].index == 1);
    REQUIRE((*list)[0].message.find("two") != std::string::npos);
    REQUIRE((*list)[1].message.find("one") != std::string::npos);
    REQUIRE((*list)[0].oid.size() == 40);
}
```

- [ ] **Step 2: Run it; verify it fails to compile** (`stashList` undeclared).

Run: `cmake --build build --target gittide_core_tests 2>&1 | head`
Expected: error — `no member named 'stashList'`.

- [ ] **Step 3: Declare in `gitrepo.hpp`** (after `stashCount`, line ~217):

```cpp
/// One entry on the stash stack. `index` is the stash@{index} slot (0 = newest),
/// `message` is git's stash message, `oid` is the 40-char hex of the stash commit
/// (a commit whose diff against its first parent is the stashed change set).
struct StashEntry
{
    std::size_t index;
    std::string message;
    std::string oid;
};

/// Enumerate the stash stack, newest first (index 0..n-1). Empty when no stashes.
/// Errors only on a libgit2 failure.
Expected<std::vector<StashEntry>> stashList() const;
```

`StashEntry` goes at namespace scope in `gitrepo.hpp` (alongside the other public
structs), not nested in the class — mirror how `MergeOutcome` is declared.

- [ ] **Step 4: Implement in `gitrepo.cpp`** (after `stashCount`):

```cpp
Expected<std::vector<StashEntry>> GitRepo::stashList() const
{
    std::vector<StashEntry> entries;
    int rc = git_stash_foreach(
        m_repo,
        [](size_t index, const char* message, const git_oid* oid, void* payload) -> int
        {
            char hex[GIT_OID_HEXSZ + 1] = {0};
            git_oid_tostr(hex, sizeof(hex), oid);
            static_cast<std::vector<StashEntry>*>(payload)->push_back(
                StashEntry{index, message ? message : "", hex});
            return 0;
        },
        &entries);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return entries;
}
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build --parallel && ctest --test-dir build -R "\[stash\]" --output-on-failure`
Expected: PASS (both `[stash]` cases).

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_stash.cpp
git commit -m "feat(core): add GitRepo::stashList enumerating the stash stack"
```

---

## Task 2: Core — apply / pop / drop / clear at an index

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (after `stashList`)
- Modify: `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_stash.cpp`

**Interfaces:**
- Produces:
  ```cpp
  Expected<void> stashApplyAt(std::size_t index); // apply, keep on stack
  Expected<void> stashPopAt(std::size_t index);   // apply + drop
  Expected<void> stashDrop(std::size_t index);     // remove without applying
  Expected<void> stashClear();                     // drop all (high → low)
  ```
  On an apply/pop **conflict** these return a `GitError` and leave the stash on
  the stack (D44).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_git_repo_stash.cpp`:

```cpp
TEST_CASE("stashApplyAt applies a chosen entry and keeps it", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "older\n");
    REQUIRE(repo->stashSave("older").value());
    tmp.writeFile("a.txt", "newer\n");
    REQUIRE(repo->stashSave("newer").value());

    // Apply the OLDER entry (index 1); it stays on the stack.
    REQUIRE(repo->stashApplyAt(1).has_value());
    REQUIRE(tmp.readFile("a.txt") == "older\n");
    REQUIRE(repo->stashCount().value() == 2);
}

TEST_CASE("stashPopAt applies and drops a chosen entry", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "x\n");
    REQUIRE(repo->stashSave("x").value());
    REQUIRE(repo->stashPopAt(0).has_value());
    REQUIRE(tmp.readFile("a.txt") == "x\n");
    REQUIRE(repo->stashCount().value() == 0);
}

TEST_CASE("stashDrop removes an entry without applying it", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "drop-me\n");
    REQUIRE(repo->stashSave("drop").value());
    REQUIRE(repo->stashDrop(0).has_value());
    REQUIRE(repo->stashCount().value() == 0);
    REQUIRE(tmp.readFile("a.txt") == "orig\n"); // not applied
}

TEST_CASE("stashClear empties the whole stack", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "1\n"); REQUIRE(repo->stashSave("1").value());
    tmp.writeFile("a.txt", "2\n"); REQUIRE(repo->stashSave("2").value());
    REQUIRE(repo->stashClear().has_value());
    REQUIRE(repo->stashCount().value() == 0);
}

TEST_CASE("stashPopAt conflict preserves the stash", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "stashed\n");
    REQUIRE(repo->stashSave("s").value());
    // Recreate a conflicting working change before popping.
    tmp.writeFile("a.txt", "conflicting\n");

    auto r = repo->stashPopAt(0);
    REQUIRE_FALSE(r.has_value());            // reported
    REQUIRE(repo->stashCount().value() == 1); // and preserved
}
```

> `TempRepo::readFile` exists alongside `writeFile`/`commitAll`; if a helper is
> missing, add the trivial reader to `tests/support/temprepo.*` in this task.

- [ ] **Step 2: Run; verify it fails** (undeclared methods).

Run: `cmake --build build --target gittide_core_tests 2>&1 | head`
Expected: `no member named 'stashApplyAt'`.

- [ ] **Step 3: Declare in `gitrepo.hpp`** (after `stashList`):

```cpp
/// Apply stash@{index} onto the working tree, keeping it on the stack. Errors
/// (and preserves the stash) on conflict.
Expected<void> stashApplyAt(std::size_t index);
/// Apply stash@{index} and drop it on success. Errors (and preserves the stash)
/// on conflict — never drops a stash it could not cleanly apply.
Expected<void> stashPopAt(std::size_t index);
/// Drop stash@{index} without applying it.
Expected<void> stashDrop(std::size_t index);
/// Drop every entry on the stack (high index → low so indices stay valid).
Expected<void> stashClear();
```

- [ ] **Step 4: Implement in `gitrepo.cpp`** (after `stashList`):

```cpp
Expected<void> GitRepo::stashApplyAt(std::size_t index)
{
    git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
    int rc = git_stash_apply(m_repo, index, &aopts);
    if (rc < 0)
        return std::unexpected(GitError{rc, "Your changes conflict and the stash was kept"});
    return {};
}

Expected<void> GitRepo::stashPopAt(std::size_t index)
{
    git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
    int rc = git_stash_pop(m_repo, index, &aopts);
    if (rc < 0)
        // libgit2 does not drop on a conflicting pop — the stash is preserved.
        return std::unexpected(GitError{rc, "Your changes conflict and are kept in the stash"});
    return {};
}

Expected<void> GitRepo::stashDrop(std::size_t index)
{
    int rc = git_stash_drop(m_repo, index);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::stashClear()
{
    auto list = stashList();
    if (!list)
        return std::unexpected(list.error());
    // Drop highest index first so the remaining indices stay valid.
    for (auto it = list->rbegin(); it != list->rend(); ++it)
    {
        int rc = git_stash_drop(m_repo, it->index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    return {};
}
```

- [ ] **Step 5: Build and run**

Run: `cmake --build build --parallel && ctest --test-dir build -R "\[stash\]" --output-on-failure`
Expected: PASS (all `[stash]` cases).

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_stash.cpp tests/support/temprepo.hpp tests/support/temprepo.cpp
git commit -m "feat(core): add stash apply/pop/drop/clear by index (conflict preserves stash)"
```

---

## Task 3: AsyncRepo — coroutine wrappers

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp` (near the existing stash decls, ~line 117)
- Modify: `ui/src/asyncrepo.cpp` (after `stashCount`, ~line 551)
- Test: `tests/ui/test_async_repo.cpp`

**Interfaces:**
- Consumes: Task 1–2 `GitRepo` methods.
- Produces:
  ```cpp
  QCoro::Task<gittide::Expected<std::vector<gittide::StashEntry>>> stashList();
  QCoro::Task<gittide::Expected<void>> stashApplyAt(int index);
  QCoro::Task<gittide::Expected<void>> stashPopAt(int index);
  QCoro::Task<gittide::Expected<void>> stashDrop(int index);
  QCoro::Task<gittide::Expected<void>> stashClear();
  ```

- [ ] **Step 1: Write the failing test**

Append to `tests/ui/test_async_repo.cpp` (follow the file's existing
`QCoro::waitFor` / open pattern — match the surrounding tests for repo setup):

```cpp
void TestAsyncRepo::stashListRoundTrips()
{
    TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    AsyncRepo repo;
    QVERIFY(QCoro::waitFor(repo.open(tmp.path())).has_value());

    tmp.writeFile("a.txt", "dirty\n");
    QVERIFY(QCoro::waitFor(repo.stashSave("wip")).value());

    auto list = QCoro::waitFor(repo.stashList());
    QVERIFY(list.has_value());
    QCOMPARE(int(list->size()), 1);
    QCOMPARE(int((*list)[0].index), 0);

    QVERIFY(QCoro::waitFor(repo.stashPopAt(0)).has_value());
    QCOMPARE(QCoro::waitFor(repo.stashCount()).value(), 0);
}
```

Declare `stashListRoundTrips()` as a `private Q_SLOT` in the test class.

- [ ] **Step 2: Run; verify it fails** (`stashList` not a member of `AsyncRepo`).

Run: `cmake --build build --target gittide_ui_tests 2>&1 | head`

- [ ] **Step 3: Declare in `asyncrepo.hpp`** (after `stashCount`):

```cpp
/// Enumerate the stash stack (newest first).
QCoro::Task<gittide::Expected<std::vector<gittide::StashEntry>>> stashList();
/// Apply stash@{index}, keeping it on the stack. Conflict → error, stash kept.
QCoro::Task<gittide::Expected<void>> stashApplyAt(int index);
/// Apply stash@{index} and drop it. Conflict → error, stash kept.
QCoro::Task<gittide::Expected<void>> stashPopAt(int index);
/// Drop stash@{index} without applying it.
QCoro::Task<gittide::Expected<void>> stashDrop(int index);
/// Drop every stash entry.
QCoro::Task<gittide::Expected<void>> stashClear();
```

Ensure `#include "gittide/gitrepo.hpp"` (already included for the repo type)
brings `StashEntry` into scope.

- [ ] **Step 4: Implement in `asyncrepo.cpp`** (after `stashCount`, mirroring it):

```cpp
QCoro::Task<gittide::Expected<std::vector<gittide::StashEntry>>> AsyncRepo::stashList()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashList();
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::stashApplyAt(int index)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, index]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashApplyAt(static_cast<std::size_t>(index));
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::stashPopAt(int index)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, index]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashPopAt(static_cast<std::size_t>(index));
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::stashDrop(int index)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, index]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashDrop(static_cast<std::size_t>(index));
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::stashClear()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.stashClear();
        });
}
```

- [ ] **Step 5: Build and run**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS incl. `stashListRoundTrips`.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/asyncrepo.hpp ui/src/asyncrepo.cpp tests/ui/test_async_repo.cpp
git commit -m "feat(ui): AsyncRepo wrappers for stash list/apply/pop/drop/clear"
```

---

## Task 4: `StashListModel` — QML list model

**Files:**
- Create: `ui/include/gittide/ui/stashlistmodel.hpp`
- Create: `ui/src/stashlistmodel.cpp`
- Modify: `ui/CMakeLists.txt` (add both to the sources list, after `difflinesmodel`)
- Create test: `tests/ui/test_stash_list_model.cpp`
- Modify: `tests/CMakeLists.txt` (add to the ui-tests source list)

**Interfaces:**
- Consumes: `gittide::StashEntry`.
- Produces:
  ```cpp
  class StashListModel : public QAbstractListModel {
      enum Roles { LabelRole = Qt::UserRole + 1, MessageRole, OidRole };
      void setEntries(const std::vector<gittide::StashEntry>& entries);
      Q_INVOKABLE QString oidAt(int row) const;   // "" if out of range
      int count() const;                           // == rowCount()
  };
  ```
  `LabelRole` is `"stash@{<index>}"`; `MessageRole` is the git message;
  `OidRole` is the hex oid.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_stash_list_model.cpp`:

```cpp
#include <QtTest/QtTest>
#include <vector>

#include "gittide/gitrepo.hpp"
#include "gittide/ui/stashlistmodel.hpp"

using gittide::StashEntry;
using gittide::ui::StashListModel;

class TestStashListModel : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void exposesRowsAndRoles()
    {
        StashListModel m;
        QCOMPARE(m.rowCount(), 0);

        std::vector<StashEntry> e{
            {0, "WIP on master: newer", std::string(40, 'a')},
            {1, "WIP on master: older", std::string(40, 'b')},
        };
        m.setEntries(e);

        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.count(), 2);
        auto idx0 = m.index(0, 0);
        QCOMPARE(m.data(idx0, StashListModel::LabelRole).toString(), QString("stash@{0}"));
        QCOMPARE(m.data(idx0, StashListModel::MessageRole).toString(), QString("WIP on master: newer"));
        QCOMPARE(m.oidAt(0), QString(QString(40, 'a')));
        QCOMPARE(m.oidAt(99), QString());
    }
};

QTEST_MAIN(TestStashListModel)
#include "test_stash_list_model.moc"
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** ui-tests list (alongside
  `test_changed_files_model.cpp`):

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_stash_list_model.cpp
```

- [ ] **Step 3: Run; verify it fails** (header not found).

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests 2>&1 | head`

- [ ] **Step 4: Create the header** `ui/include/gittide/ui/stashlistmodel.hpp`:

```cpp
#pragma once
#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "gittide/gitrepo.hpp" // gittide::StashEntry

namespace gittide::ui {

/// QML list model of stash-stack entries. One row per StashEntry, newest first.
/// Exposes a "stash@{n}" label, the git message, and the stash commit oid (used
/// to drive the read-only preview via the commit-diff path). Knows nothing about
/// themes — the QML delegate styles it.
class StashListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        LabelRole = Qt::UserRole + 1, ///< "stash@{<index>}"
        MessageRole,                  ///< git stash message
        OidRole,                      ///< 40-char hex of the stash commit
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const std::vector<gittide::StashEntry>& entries);
    /// Stash commit oid for @p row, or an empty string when out of range.
    Q_INVOKABLE QString oidAt(int row) const;
    int count() const { return static_cast<int>(m_rows.size()); }

private:
    struct Row
    {
        QString label;
        QString message;
        QString oid;
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
```

- [ ] **Step 5: Create the impl** `ui/src/stashlistmodel.cpp`:

```cpp
#include "gittide/ui/stashlistmodel.hpp"

namespace gittide::ui {

int StashListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant StashListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case LabelRole:
        return r.label;
    case MessageRole:
        return r.message;
    case OidRole:
        return r.oid;
    default:
        return {};
    }
}

QHash<int, QByteArray> StashListModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {MessageRole, "message"},
        {OidRole, "oid"},
    };
}

void StashListModel::setEntries(const std::vector<gittide::StashEntry>& entries)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(entries.size());
    for (const auto& e : entries)
    {
        m_rows.push_back(Row{
            QStringLiteral("stash@{%1}").arg(e.index),
            QString::fromStdString(e.message),
            QString::fromStdString(e.oid),
        });
    }
    endResetModel();
}

QString StashListModel::oidAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return {};
    return m_rows[static_cast<std::size_t>(row)].oid;
}

} // namespace gittide::ui
```

- [ ] **Step 6: Add to `ui/CMakeLists.txt`** (after the `difflinesmodel` lines):

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/stashlistmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/stashlistmodel.hpp
```

- [ ] **Step 7: Build and run**

Run: `cmake -S . -B build && cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS incl. `TestStashListModel`.

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/stashlistmodel.hpp ui/src/stashlistmodel.cpp ui/CMakeLists.txt tests/ui/test_stash_list_model.cpp tests/CMakeLists.txt
git commit -m "feat(ui): add StashListModel for the stash stack"
```

---

## Task 5: RepoController — stash list refresh + mutating ops

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp` (signal + slots near `popStash`/`refreshStashState`)
- Modify: `ui/src/repocontroller.cpp`
- Test: `tests/ui/test_repocontroller_stash.cpp`

**Interfaces:**
- Consumes: Task 3 AsyncRepo wrappers; existing `refreshStatus`, `refreshStashState`.
- Produces:
  ```cpp
  // signal
  void stashListReady(std::vector<gittide::StashEntry> entries);
  // slots
  QCoro::Task<void> applyStashAt(int index);
  QCoro::Task<void> popStashAt(int index);
  QCoro::Task<void> dropStash(int index);
  QCoro::Task<void> clearStashes();
  ```
  `refreshStashState()` now also fetches `stashList()` and emits `stashListReady`.

- [ ] **Step 1: Write the failing test**

Append to `tests/ui/test_repocontroller_stash.cpp` (match the file's existing
controller + signal-spy setup):

```cpp
void TestRepoControllerStash::dropRemovesChosenEntry()
{
    TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto [ctrl, repo] = makeController(tmp.path()); // existing helper in this file

    QSignalSpy listSpy(ctrl.get(), &RepoController::stashListReady);

    tmp.writeFile("a.txt", "x\n");
    QCoro::waitFor(ctrl->stashChanges());            // creates a stash + emits list
    QVERIFY(listSpy.count() >= 1);
    auto entries = qvariant_cast<std::vector<gittide::StashEntry>>(listSpy.last().at(0));
    QCOMPARE(int(entries.size()), 1);

    QCoro::waitFor(ctrl->dropStash(0));
    QCOMPARE(QCoro::waitFor(repo->stashCount()).value(), 0);
}

void TestRepoControllerStash::popConflictReportsAndKeeps()
{
    TempRepo tmp;
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("init");
    auto [ctrl, repo] = makeController(tmp.path());
    QSignalSpy failSpy(ctrl.get(), &RepoController::operationFailed);

    tmp.writeFile("a.txt", "stashed\n");
    QCoro::waitFor(ctrl->stashChanges());
    tmp.writeFile("a.txt", "conflicting\n");

    QCoro::waitFor(ctrl->popStashAt(0));
    QCOMPARE(failSpy.count(), 1);                    // reported
    QCOMPARE(QCoro::waitFor(repo->stashCount()).value(), 1); // preserved
}
```

> Use the exact helper names already present in `test_repocontroller_stash.cpp`
> (e.g. its controller factory and `makeController`); the snippet above shows
> intent — align identifiers with the file. Register
> `std::vector<gittide::StashEntry>` with `qRegisterMetaType` if the signal-spy
> cast needs it (do it in the test's `initTestCase`, mirroring how other vector
> payloads are registered).

- [ ] **Step 2: Run; verify it fails** (`stashListReady` / `dropStash` missing).

Run: `cmake --build build --target gittide_ui_tests 2>&1 | head`

- [ ] **Step 3: Declare the signal + slots in `repocontroller.hpp`**

In `signals:` near `stashCountChanged` (line ~180):

```cpp
/// Emitted whenever the stash list is refreshed (on open, after a git-dir change,
/// and after any stash op). Feeds the VM's StashListModel. Newest first.
void stashListReady(std::vector<gittide::StashEntry> entries);
```

In the public slots near `popStash` (line ~62):

```cpp
/// Apply stash@{index}, keeping it; refreshes status + stash list. Conflict →
/// operationFailed, stash preserved.
QCoro::Task<void> applyStashAt(int index);
/// Pop stash@{index} (apply + drop); refreshes status + stash list. Conflict →
/// operationFailed, stash preserved.
QCoro::Task<void> popStashAt(int index);
/// Drop stash@{index}; refreshes the stash list.
QCoro::Task<void> dropStash(int index);
/// Clear the whole stack; refreshes the stash list.
QCoro::Task<void> clearStashes();
```

Add `#include "gittide/gitrepo.hpp"` to the header if `StashEntry` isn't already
visible (it is needed for the signal type).

- [ ] **Step 4: Implement in `repocontroller.cpp`**

Extend `refreshStashState` (line ~246) to also emit the list:

```cpp
QCoro::Task<void> RepoController::refreshStashState()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto count = co_await m_repo->stashCount();
    if (!self || !count)
        co_return;
    emit stashCountChanged(*count);

    auto list = co_await m_repo->stashList();
    if (!self || !list)
        co_return;
    emit stashListReady(*list);
}
```

Add the four mutating slots after `popStash` (mirror its `WatchMute` +
report-on-error + refresh cascade shape):

```cpp
QCoro::Task<void> RepoController::applyStashAt(int index)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    WatchMute                mute(m_watcher);
    auto result = co_await m_repo->stashApplyAt(index);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message)); // stash preserved
        co_return;
    }
    co_await refreshStatus();
    co_await refreshStashState();
}

QCoro::Task<void> RepoController::popStashAt(int index)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    WatchMute                mute(m_watcher);
    auto result = co_await m_repo->stashPopAt(index);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message)); // stash preserved
        co_return;
    }
    co_await refreshStatus();
    co_await refreshStashState();
}

QCoro::Task<void> RepoController::dropStash(int index)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->stashDrop(index);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStashState();
}

QCoro::Task<void> RepoController::clearStashes()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto result = co_await m_repo->stashClear();
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshStashState();
}
```

> `drop`/`clear` skip `refreshStatus` because they do not touch the working
> tree — only the stack. `apply`/`pop` rewrite the tree, so both refresh.

- [ ] **Step 5: Register metatype if needed.** If `stashListReady` is the first
  signal carrying `std::vector<gittide::StashEntry>`, register it where other
  payload types are registered (search for `qRegisterMetaType` in
  `repocontroller.cpp` / `asyncrepo.cpp` and add alongside):

```cpp
qRegisterMetaType<std::vector<gittide::StashEntry>>("std::vector<gittide::StashEntry>");
```

- [ ] **Step 6: Build and run**

Run: `cmake --build build --parallel && ctest --test-dir build -R "gittide_ui_tests" --output-on-failure`
Expected: PASS incl. the two new controller cases.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp tests/ui/test_repocontroller_stash.cpp
git commit -m "feat(ui): RepoController stash list refresh + apply/pop/drop/clear"
```

---

## Task 6: RepoViewModel — `stashes` model, invokables, preview state

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Test: `tests/ui/test_repo_view_model.cpp` (or a new `test_repoviewmodel_stash.cpp` added to `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: Task 4 `StashListModel`, Task 5 controller signal/slots, existing
  `selectCommit` / `commitFiles` / `commitDiff`.
- Produces:
  ```cpp
  Q_PROPERTY(StashListModel* stashes READ stashes CONSTANT)
  Q_PROPERTY(bool stashPreviewActive READ stashPreviewActive NOTIFY stashPreviewChanged)
  Q_PROPERTY(QString stashPreviewLabel READ stashPreviewLabel NOTIFY stashPreviewChanged)
  Q_INVOKABLE void previewStash(int row);   // loads commit models from the stash oid
  Q_INVOKABLE void exitStashPreview();
  Q_INVOKABLE void applyStash(int row);
  Q_INVOKABLE void popStashAt(int row);
  Q_INVOKABLE void dropStash(int row);
  Q_INVOKABLE void clearStashes();
  ```

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_repo_view_model.cpp` (follow its VM-construction pattern):

```cpp
void TestRepoViewModel::stashesPopulateAndPreview()
{
    TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");
    auto vm = makeViewModel(tmp.path()); // existing helper

    QCOMPARE(vm->stashes()->rowCount(), 0);
    QVERIFY(!vm->stashPreviewActive());

    tmp.writeFile("a.txt", "dirty\n");
    vm->stashChanges();
    QTRY_COMPARE(vm->stashes()->rowCount(), 1); // QTRY_* spins the event loop

    vm->previewStash(0);
    QTRY_VERIFY(vm->stashPreviewActive());
    QVERIFY(vm->stashPreviewLabel().startsWith("stash@{0}"));
    QTRY_VERIFY(vm->commitFiles()->rowCount() > 0); // stash diff loaded into commit model

    vm->exitStashPreview();
    QVERIFY(!vm->stashPreviewActive());
    QCOMPARE(vm->commitFiles()->rowCount(), 0);
}
```

- [ ] **Step 2: Run; verify it fails** (`stashes()` missing).

- [ ] **Step 3: Header changes in `repoviewmodel.hpp`**

Add the include `#include "gittide/ui/stashlistmodel.hpp"`. Add the properties
near the other model properties; add accessors, invokables, signal, slot, and
members:

```cpp
// properties
Q_PROPERTY(StashListModel* stashes READ stashes CONSTANT)
Q_PROPERTY(bool stashPreviewActive READ stashPreviewActive NOTIFY stashPreviewChanged)
Q_PROPERTY(QString stashPreviewLabel READ stashPreviewLabel NOTIFY stashPreviewChanged)

// public accessors
StashListModel* stashes() const { return m_stashes; }
bool stashPreviewActive() const { return m_stashPreviewActive; }
QString stashPreviewLabel() const { return m_stashPreviewLabel; }

// invokables
Q_INVOKABLE void previewStash(int row);
Q_INVOKABLE void exitStashPreview();
Q_INVOKABLE void applyStash(int row);
Q_INVOKABLE void popStashAt(int row);
Q_INVOKABLE void dropStash(int row);
Q_INVOKABLE void clearStashes();

signals:
void stashPreviewChanged();

private:
void onStashList(const std::vector<gittide::StashEntry>& entries);
// members
StashListModel* m_stashes = nullptr;
bool            m_stashPreviewActive = false;
QString         m_stashPreviewLabel;
```

- [ ] **Step 4: Impl in `repoviewmodel.cpp`**

Construct the model + wire the signal in the constructor (alongside
`m_commitFiles(new ChangedFilesModel(this))` and the existing connects):

```cpp
// in the member-init list
, m_stashes(new StashListModel(this))
// in the constructor body, near the other connect()s
connect(m_controller, &RepoController::stashListReady, this, &RepoViewModel::onStashList);
```

Add the slot + invokables:

```cpp
void RepoViewModel::onStashList(const std::vector<gittide::StashEntry>& entries)
{
    m_stashes->setEntries(entries);
    // If the previewed stash vanished (dropped/popped), leave preview mode.
    if (m_stashPreviewActive && m_stashes->rowCount() == 0)
        exitStashPreview();
}

void RepoViewModel::previewStash(int row)
{
    const QString oid = m_stashes->oidAt(row);
    if (oid.isEmpty())
        return;
    m_stashPreviewActive = true;
    m_stashPreviewLabel  = m_stashes->data(m_stashes->index(row, 0), StashListModel::LabelRole).toString();
    emit stashPreviewChanged();
    selectCommit(oid); // reuses commitFiles/commitDiff (stash commit vs base parent)
}

void RepoViewModel::exitStashPreview()
{
    if (!m_stashPreviewActive)
        return;
    m_stashPreviewActive = false;
    m_stashPreviewLabel.clear();
    m_selectedCommit.clear();
    m_activeCommitFile.clear();
    m_commitFiles->setFiles({});
    m_commitDiff->clear();
    emit selectedCommitChanged();
    emit activeCommitFileChanged();
    emit stashPreviewChanged();
}

void RepoViewModel::applyStash(int row)
{
    QCoro::connect(m_controller->applyStashAt(row), this, [] {});
}

void RepoViewModel::popStashAt(int row)
{
    QCoro::connect(m_controller->popStashAt(row), this, [] {});
}

void RepoViewModel::dropStash(int row)
{
    QCoro::connect(m_controller->dropStash(row), this, [] {});
}

void RepoViewModel::clearStashes()
{
    QCoro::connect(m_controller->clearStashes(), this, [] {});
}
```

In `close()` (and any `reset()` path), clear preview state:

```cpp
m_stashes->setEntries({});
m_stashPreviewActive = false;
m_stashPreviewLabel.clear();
```

- [ ] **Step 5: Build and run**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS incl. `stashesPopulateAndPreview`.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp tests/ui/test_repo_view_model.cpp
git commit -m "feat(ui): RepoViewModel stashes model, preview state, and stash invokables"
```

---

## Task 7: QML — `StashPanel.qml` + Changes-tab preview mode

**Files:**
- Create: `ui/qml/StashPanel.qml`
- Modify: `ui/qml/ChangesPane.qml` (mount the panel; preview-mode conditionals)
- Modify: `ui/qml/DiffView.qml` (preview header + read-only model swap)
- Modify: `ui/CMakeLists.txt` if QML files are listed there (check; Plan 29 added `AppMenuBar.qml`)
- Create test: `tests/ui/test_qml_stash.cpp`
- Modify: `tests/CMakeLists.txt` (add the ui test)

**Interfaces:**
- Consumes: `repoVm.stashes`, `repoVm.stashAvailable`, `repoVm.previewStash`,
  `repoVm.applyStash`, `repoVm.popStashAt`, `repoVm.dropStash`,
  `repoVm.clearStashes`, `repoVm.stashPreviewActive`, `repoVm.stashPreviewLabel`,
  `repoVm.commitFiles`, `repoVm.commitDiff`.

- [ ] **Step 1: Write the failing QML test**

Create `tests/ui/test_qml_stash.cpp` — mirror `test_qml_menu_bar.cpp` (load a
QML harness, drive the model, assert `objectName`d items). It must assert:
1. The `stashPanel` is `visible: false` when `repoVm.stashes.count === 0`.
2. After the model gains a row, the panel shows and `stashList` has one delegate.
3. Clicking a row's `stashApplyButton` calls `repoVm.applyStash` (use a fake/stub
   VM exposing call-recording invokables, as the other `test_qml_*` do), and the
   `stashClearButton` calls `repoVm.clearStashes`.

```cpp
// Skeleton — fill bindings to match the existing test_qml_* harness in this repo.
#include <QtQuickTest/quicktest.h>
QUICK_TEST_MAIN(qml_stash)
```

> Prefer the established C++-driven QtTest harness used by `test_qml_menu_bar.cpp`
> over `quicktest` if that is the repo convention — match the neighbouring tests
> exactly (same fixture, same stub-VM registration). Assert via `objectName`
> lookups: `stashPanel`, `stashList`, `stashApplyButton`, `stashPopButton`,
> `stashDropButton`, `stashClearButton`, `stashPreviewBar`.

- [ ] **Step 2: Register the test** in `tests/CMakeLists.txt`:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_stash.cpp
```

- [ ] **Step 3: Run; verify it fails** (no `StashPanel`, objects absent).

- [ ] **Step 4: Create `ui/qml/StashPanel.qml`**

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Collapsible "Stashes (n)" section for the Changes tab's left column. Lists the
// stash stack; selecting a row previews its diff (right pane); per-row Apply/Pop/
// Drop and a header Clear. Hidden entirely when the stack is empty.
ColumnLayout {
    id: stashPanel
    objectName: "stashPanel"
    spacing: 0
    visible: repoVm && repoVm.stashes && repoVm.stashes.count > 0

    property bool expanded: true

    // ---- Header row: disclosure + title + Clear ----
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 8
        spacing: 6

        Label {
            text: stashPanel.expanded ? "▾" : "▸" // ▾ / ▸
            color: theme.textSecondary
            font.pixelSize: 12
            MouseArea { anchors.fill: parent; onClicked: stashPanel.expanded = !stashPanel.expanded }
        }
        Label {
            Layout.fillWidth: true
            text: repoVm && repoVm.stashes ? ("Stashes (" + repoVm.stashes.count + ")") : "Stashes"
            color: theme.textPrimary
            font.pixelSize: 13
            font.weight: Font.DemiBold
            MouseArea { anchors.fill: parent; onClicked: stashPanel.expanded = !stashPanel.expanded }
        }
        AppButton {
            objectName: "stashClearButton"
            variant: "secondary"
            compact: true
            text: "Clear"
            onClicked: if (repoVm) repoVm.clearStashes()
        }
    }

    // ---- Entry list ----
    ListView {
        id: stashList
        objectName: "stashList"
        visible: stashPanel.expanded
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 180)
        clip: true
        model: repoVm ? repoVm.stashes : null

        delegate: Rectangle {
            width: ListView.view.width
            implicitHeight: 46
            color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    stashList.currentIndex = index
                    if (repoVm) repoVm.previewStash(index)
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 6

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        text: model.label
                        color: theme.textSecondary
                        font.pixelSize: 11
                    }
                    Label {
                        Layout.fillWidth: true
                        text: model.message
                        color: theme.textPrimary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                AppButton {
                    objectName: "stashApplyButton"
                    variant: "secondary"; compact: true; text: "Apply"
                    onClicked: if (repoVm) repoVm.applyStash(index)
                }
                AppButton {
                    objectName: "stashPopButton"
                    variant: "secondary"; compact: true; text: "Pop"
                    onClicked: if (repoVm) repoVm.popStashAt(index)
                }
                AppButton {
                    objectName: "stashDropButton"
                    variant: "danger"; compact: true; text: "Drop"
                    onClicked: if (repoVm) repoVm.dropStash(index)
                }
            }
        }
    }

    // Hairline under the panel.
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border; visible: stashPanel.expanded }
}
```

- [ ] **Step 5: Mount in `ChangesPane.qml`** — add `StashPanel` to the left
  `ColumnLayout`, below the file list / above the commit box:

```qml
StashPanel {
    Layout.fillWidth: true
}
```

- [ ] **Step 6: Preview mode in `DiffView.qml`** — show the stash diff read-only
  and a preview header with Exit. Make the model + header conditional:

```qml
// header label
text: (repoVm && repoVm.stashPreviewActive)
      ? ("Preview: " + repoVm.stashPreviewLabel)
      : (repoVm ? repoVm.activeFile : "")

// the diff ListView model
model: repoVm ? (repoVm.stashPreviewActive ? repoVm.commitDiff : repoVm.diffLines) : null
```

Add an Exit affordance in the header bar (visible only in preview):

```qml
AppButton {
    objectName: "stashPreviewBar"
    variant: "secondary"; compact: true; text: "Exit preview"
    visible: repoVm && repoVm.stashPreviewActive
    onClicked: if (repoVm) repoVm.exitStashPreview()
}
```

> In preview the per-line checkbox column must not show (read-only). The diff
> delegate already keys its checkbox visibility off the editable working diff;
> gate it additionally on `!repoVm.stashPreviewActive` so preview rows render
> plain — mirror how the read-only history detail hides line checkboxes.

When `stashPreviewActive`, the left file list should show the stash's files: bind
the `ChangesPane` `fileList.model` to `repoVm.stashPreviewActive ?
repoVm.commitFiles : repoVm.changedFiles`, and on click call
`repoVm.selectCommitFile(...)` in preview mode vs the normal selection otherwise.
Hide the commit box (`visible: !repoVm.stashPreviewActive`) and the file
checkboxes in preview mode.

- [ ] **Step 7: Build and run the QML test**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS incl. `test_qml_stash`.

- [ ] **Step 8: Manual smoke (optional but recommended).** Launch the app on a
  dirty repo: *Stash all changes* → the panel shows the entry; click it → diff
  previews in the right pane with the *Preview: stash@{0}* header; Apply/Pop/Drop
  and Clear behave; a conflicting pop shows the error banner and keeps the entry.

- [ ] **Step 9: Commit**

```bash
git add ui/qml/StashPanel.qml ui/qml/ChangesPane.qml ui/qml/DiffView.qml ui/CMakeLists.txt tests/ui/test_qml_stash.cpp tests/CMakeLists.txt
git commit -m "feat(ui): StashPanel in the Changes tab with read-only stash preview"
```

---

## Task 8: Close-out — spec truth, plan outcome, wish shipped

**Files:**
- Modify: `docs/plans/index.md` (add the Plan 31 row), this plan's **Outcome**.
- Verify: `docs/spec/product/product.md §Stash` and
  `docs/spec/engineering/engineering.md §Stash management` match what shipped
  (esp. that preview reuses `commitFiles`/`commitDiff`, not a `stashDiff` method).
- Modify: `docs/wishlist/stash-management.md` → `Status: shipped` (+ date), move its
  row to the Shipped table in `docs/wishlist/index.md`, move the file to
  `docs/wishlist/shipped/`.

- [ ] **Step 1: Add the index row** to `docs/plans/index.md`:

```
| [Plan 31 — Stash viewing & management](2026-06-30-plan31-stash-management.md) | 2026-06-30 | done | product · engineering · core · ui |
```

- [ ] **Step 2: Reconcile the spec.** The engineering §Stash management text lists
  a `stashDiff(index)` core method; the implementation instead reuses
  `commitFiles`/`commitDiff` on the stash oid (DRY — a stash *is* a commit). Edit
  that bullet so the spec matches the code (code is ground truth). Keep D44's
  "preview reuses the read-only commitDiff model" — that already matches.

- [ ] **Step 3: Run the full suite green.**

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass, no new warnings.

- [ ] **Step 4: Fill this plan's Outcome, flip the wish, commit.**

```bash
git add docs/
git commit -m "docs: close out Plan 31 (stash viewing & management)"
```

---

## Outcome

Shipped on branch `feat/stash-management` (8 commits, `e9ee475`..`2d11edb`),
all 171 tests green, the dedicated `TestQmlStash` pristine.

- **Shipped:** the user-facing stash stack — a collapsible *Stashes* panel in the
  Changes tab listing every entry (newest first), with per-entry Apply / Pop /
  Drop, a header Clear, and read-only diff preview of a selected entry in the
  shared diff pane. Save stays one-click (untracked included). Apply/pop conflicts
  report and preserve the stash (D44). Preview reuses the existing commit-diff path
  (a stash is a commit) — no new core diff primitive.
- **Spec updated:** [`product` §Stash](../spec/product/product.md#stash),
  [`engineering` §Stash management](../spec/engineering/engineering.md),
  decision [D44](../decisions.md).
- **Code:** `GitRepo::stashList/stashApplyAt/stashPopAt/stashDrop/stashClear`
  (core); `AsyncRepo` wrappers; `StashListModel`; `RepoController` apply/pop/drop/
  clear slots + `stashListReady`; `RepoViewModel` `stashes` + `previewStash`/
  `exitStashPreview` reusing `commitFiles`/`commitDiff`; `ui/qml/StashPanel.qml`
  + stash-preview mode in `DiffView.qml` / `ChangesPane.qml`.
- **Deferred (Minor, for follow-up):** apply/popAt error text says "conflict" for
  any `rc<0` (incl. bad index) — matches the pre-existing `stashPop` pattern;
  `StashEntry` metatype declared in `repocontroller.hpp` rather than
  `metatypes.hpp`; `onStashList` exits preview only when the stack empties (a
  non-last previewed entry being dropped is mitigated in the panel by exiting
  preview before any mutation); the banner QML tests' minimal stubs add 2 lines to
  their pre-existing `[undefined]`-binding noise.
