# Plan 11 — Fleet fetch-all across a project

> **For agentic workers:** REQUIRED SUB-SKILL: use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task, test-first.
> Each task's steps use checkbox (`- [ ]`) syntax; tick them as you go.

| | |
|--|--|
| **Date** | 2026-06-22 |
| **Status** | `planned` |
| **Spec** | [`spec/product/product.md` §Syncing → Fleet fetch](../spec/product/product.md), [`spec/engineering/engineering.md` §Fleet fetch-all](../spec/engineering/engineering.md) |
| **Depends on** | per-repo sync (shipped: `AsyncRepo::fetch`/`syncStatus`, `RepoController` auth flow), project/repo models |

**Goal:** Fetch every repository in the active project at once, in parallel,
with per-repo status shown inline in the sidebar (spinner → up-to-date / updated
with a refreshed *behind* badge / failed) and an aggregate summary on the project
header. Fetch only — no working-tree changes.

**Architecture:** Orchestration lives in `ProjectController` (it owns the active
project + `RepoListModel`), not `RepoController` (single active repo). `fetchAll()`
fans out one **fresh** `AsyncRepo` per non-missing top-level repo — each its own
`git_repository` handle, so the one-owner invariant (#5) holds — and each fetch
runs on Qt's global thread pool (parallelism bounded by the pool). Per-repo
results drive transient state on `RepoListModel` via new roles. Credentials are
held at the controller level and prompted once on demand, reusing the existing
`isAuthError` classification (extracted to a shared helper in Task 1).

**Tech stack:** C++23, Qt6 (Quick/QML, Concurrent, Test), QCoro coroutines,
libgit2 (private to core), Catch2 + QtTest.

## Global constraints

- Invariants must hold ([`spec/engineering/engineering.md`](../spec/engineering/engineering.md)):
  **no Qt in `core/`**; **libgit2/nlohmann private to core**; **one owner per
  `GitRepo`** (each fleet fetch opens its own `AsyncRepo`, never shares the active
  one); **errors are values** (`Expected<T>`); **paths via `generic_u8string()`**;
  **colour from a theme token**, never a hex literal in QML.
- New `ui/` sources → [`ui/CMakeLists.txt`](../../ui/CMakeLists.txt). New UI tests
  take **TWO edits**: add to `gittide_ui_test_sources` in
  [`tests/CMakeLists.txt`](../../tests/CMakeLists.txt) **and** add `#include` +
  `RUN(...)` in [`tests/ui/main.cpp`](../../tests/ui/main.cpp) — miss either and it
  compiles but runs zero tests.
- **No network in tests** — use a local bare remote (`TempRepo::addBareRemote` /
  `pushBranch` / `cloneFrom`). Real auth (the credential prompt) is verified
  manually; tests cover only the offline plumbing.
- Existing tests must keep passing: `TestProjectController`, `TestRepoListModel`,
  `TestRepoController`, `TestAsyncRepo`.
- TDD: write the failing test first, run it red, implement, run it green, commit.

Build / run a single UI test:

```bash
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests "fetchAll_*"
```

(`gittide_ui_tests` is one aggregated binary; pass a QtTest slot-name glob to
filter. Catch2 core tests: `ctest --test-dir build -R '<case-name>'`.)

---

## Task 1: Extract `isAuthError` into a shared UI helper

DRY: `isAuthError` is currently a file-local function in
[`ui/src/repocontroller.cpp:17`](../../ui/src/repocontroller.cpp). The fleet path
needs the same classification, so lift it to a shared, unit-tested helper before
reusing it.

**Files:**
- Create: `ui/include/gittide/ui/autherror.hpp`
- Create: `ui/src/autherror.cpp`
- Modify: `ui/src/repocontroller.cpp` (delete the local copy, include the header)
- Modify: `ui/CMakeLists.txt` (add `src/autherror.cpp` to the `ui` sources list)
- Test: `tests/ui/test_auth_error.cpp` (+ register in `tests/CMakeLists.txt` and
  `tests/ui/main.cpp`)

**Interfaces:**
- Produces: `bool gittide::ui::isAuthError(const gittide::GitError& e);`
  — true when `e.code == -16` (libgit2 `GIT_EAUTH`) or `e.message` contains
  `"authentication"` or `"401"`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_auth_error.cpp`:

```cpp
#include <QtTest/QtTest>

#include "gittide/giterror.hpp"
#include "gittide/ui/autherror.hpp"

using gittide::GitError;
using gittide::ui::isAuthError;

class TestAuthError : public QObject
{
    Q_OBJECT
private slots:
    void eauth_code_is_auth() { QVERIFY(isAuthError(GitError{.code = -16, .message = "x"})); }
    void message_authentication_is_auth() { QVERIFY(isAuthError(GitError{.code = -1, .message = "remote authentication required"})); }
    void message_401_is_auth() { QVERIFY(isAuthError(GitError{.code = -1, .message = "unexpected http status code: 401"})); }
    void other_error_is_not_auth() { QVERIFY(!isAuthError(GitError{.code = -3, .message = "reference not found"})); }
};

#include "test_auth_error.moc"
```

Register it (both edits):
- `tests/CMakeLists.txt` — add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_auth_error.cpp`
  to `gittide_ui_test_sources`.
- `tests/ui/main.cpp` — add `#include "test_auth_error.cpp"` near the other
  includes and `RUN(TestAuthError);` in `main()`.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — `gittide/ui/autherror.hpp` does not exist yet.

- [ ] **Step 3: Create the header and implementation**

`ui/include/gittide/ui/autherror.hpp`:

```cpp
#pragma once
#include "gittide/giterror.hpp"

namespace gittide::ui {

/// True when a GitError denotes an authentication failure. Compares the libgit2
/// code numerically (GIT_EAUTH == -16; libgit2 is private to core, so this layer
/// never includes git2.h) with a best-effort message-substring fallback for
/// build configurations that remap the code.
bool isAuthError(const gittide::GitError& e);

} // namespace gittide::ui
```

`ui/src/autherror.cpp`:

```cpp
#include "gittide/ui/autherror.hpp"

namespace gittide::ui {

bool isAuthError(const gittide::GitError& e)
{
    return e.code == -16
        || e.message.find("authentication") != std::string::npos
        || e.message.find("401") != std::string::npos;
}

} // namespace gittide::ui
```

Add `src/autherror.cpp` to the `ui` library source list in `ui/CMakeLists.txt`
(next to `src/repocontroller.cpp`).

- [ ] **Step 4: Replace the local copy in `repocontroller.cpp`**

Delete the anonymous-namespace `isAuthError` (lines ~12–24, the `namespace { ...
bool isAuthError(...) ... }` block) and add the include near the other ui
includes at the top of the file:

```cpp
#include "gittide/ui/autherror.hpp"
```

The three existing call sites (`if (isAuthError(r.error()))`) now resolve to
`gittide::ui::isAuthError` via the namespace — no call-site edits needed.

- [ ] **Step 5: Run tests to verify green**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests "*"`
Expected: PASS — `TestAuthError` runs 4 slots; `TestRepoController` still passes.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/autherror.hpp ui/src/autherror.cpp ui/src/repocontroller.cpp ui/CMakeLists.txt tests/ui/test_auth_error.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "refactor(ui): extract isAuthError into a shared, tested helper"
```

---

## Task 2: Per-repo fetch state + sync counts on `RepoListModel`

Add the transient per-repo state the sidebar renders during a fleet fetch.

**Files:**
- Modify: `ui/include/gittide/ui/repolistmodel.hpp`
- Modify: `ui/src/repolistmodel.cpp`
- Test: `tests/ui/test_repo_list_model.cpp` (existing — add slots)

**Interfaces:**
- Produces (on `RepoListModel`):
  - `enum class FetchState { Idle, Running, UpToDate, Updated, Failed };` (`Q_ENUM`)
  - new roles `FetchStateRole`, `FetchErrorRole`, `AheadRole`, `BehindRole`
    (QML names `fetchState`, `fetchError`, `ahead`, `behind`)
  - `void resetFetchStates();` — every top-level repo → `Idle`, counts 0, error cleared
  - `void setFetchState(int rootRow, FetchState state, const QString& error = {});`
  - `void setSyncCounts(int rootRow, int ahead, int behind);`
  - `int topLevelCount() const;` — number of root repos (so the controller can
    iterate rows; row `i` corresponds to `activeRepos()[i]`)

  Each mutator emits `dataChanged` for the affected root index across the new
  roles. Out-of-range rows are a safe no-op.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_repo_list_model.cpp` (inside the existing test class):

```cpp
void fetchState_roundtrips_and_resets()
{
    using gittide::ui::RepoListModel;
    RepoListModel m;
    m.setRepos({gittide::RepoRef{.path = "/home/u/api"}, gittide::RepoRef{.path = "/home/u/web"}});
    QCOMPARE(m.topLevelCount(), 2);

    const QModelIndex i0 = m.index(0, 0);
    // Defaults.
    QCOMPARE(m.data(i0, RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Idle));

    QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
    m.setFetchState(0, RepoListModel::FetchState::Running);
    QCOMPARE(m.data(i0, RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Running));
    QCOMPARE(spy.count(), 1);

    m.setSyncCounts(0, 1, 3);
    QCOMPARE(m.data(i0, RepoListModel::AheadRole).toInt(), 1);
    QCOMPARE(m.data(i0, RepoListModel::BehindRole).toInt(), 3);

    m.setFetchState(0, RepoListModel::FetchState::Failed, QStringLiteral("boom"));
    QCOMPARE(m.data(i0, RepoListModel::FetchErrorRole).toString(), QStringLiteral("boom"));

    m.resetFetchStates();
    QCOMPARE(m.data(i0, RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Idle));
    QCOMPARE(m.data(i0, RepoListModel::BehindRole).toInt(), 0);
}

void setFetchState_out_of_range_is_noop()
{
    using gittide::ui::RepoListModel;
    RepoListModel m;
    m.setRepos({gittide::RepoRef{.path = "/home/u/api"}});
    m.setFetchState(5, RepoListModel::FetchState::Running); // must not crash
    QCOMPARE(m.topLevelCount(), 1);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — `FetchState`, `FetchStateRole`, `topLevelCount`,
`setFetchState`, `setSyncCounts`, `resetFetchStates` are undefined.

- [ ] **Step 3: Extend the header**

In `ui/include/gittide/ui/repolistmodel.hpp`, add the enum and roles inside the
`class RepoListModel`:

```cpp
    enum class FetchState
    {
        Idle,
        Running,
        UpToDate,
        Updated,
        Failed,
    };
    Q_ENUM(FetchState)

    enum Roles
    {
        PathRole = Qt::UserRole + 1,
        MissingRole,
        IsSubmoduleRole,
        ShortOidRole,
        StatusRole,
        FetchStateRole,
        FetchErrorRole,
        AheadRole,
        BehindRole,
    };
```

Add the public mutators (after `firstRepoPath()`):

```cpp
    int  topLevelCount() const;
    void resetFetchStates();
    void setFetchState(int rootRow, FetchState state, const QString& error = {});
    void setSyncCounts(int rootRow, int ahead, int behind);
```

Add the fields to `struct Node` (after `status`):

```cpp
        FetchState fetchState = FetchState::Idle;
        QString    fetchError;
        int        ahead  = 0;
        int        behind = 0;
```

- [ ] **Step 4: Implement in the .cpp**

In `ui/src/repolistmodel.cpp`, add `data()` cases (inside the `switch`):

```cpp
    case FetchStateRole:
        return static_cast<int>(node->fetchState);
    case FetchErrorRole:
        return node->fetchError;
    case AheadRole:
        return node->ahead;
    case BehindRole:
        return node->behind;
```

Add to `roleNames()`:

```cpp
    roles[FetchStateRole] = "fetchState";
    roles[FetchErrorRole] = "fetchError";
    roles[AheadRole]      = "ahead";
    roles[BehindRole]     = "behind";
```

Add the mutators (top-level rows only — the fleet never touches submodule rows):

```cpp
int RepoListModel::topLevelCount() const
{
    return static_cast<int>(m_roots.size());
}

void RepoListModel::resetFetchStates()
{
    for (std::size_t i = 0; i < m_roots.size(); ++i)
    {
        Node& n     = *m_roots[i];
        n.fetchState = FetchState::Idle;
        n.fetchError.clear();
        n.ahead  = 0;
        n.behind = 0;
        const QModelIndex idx = createIndex(static_cast<int>(i), 0, &n);
        emit dataChanged(idx, idx, {FetchStateRole, FetchErrorRole, AheadRole, BehindRole});
    }
}

void RepoListModel::setFetchState(int rootRow, FetchState state, const QString& error)
{
    if (rootRow < 0 || rootRow >= static_cast<int>(m_roots.size()))
        return;
    Node& n      = *m_roots[rootRow];
    n.fetchState = state;
    n.fetchError = error;
    const QModelIndex idx = createIndex(rootRow, 0, &n);
    emit dataChanged(idx, idx, {FetchStateRole, FetchErrorRole});
}

void RepoListModel::setSyncCounts(int rootRow, int ahead, int behind)
{
    if (rootRow < 0 || rootRow >= static_cast<int>(m_roots.size()))
        return;
    Node& n  = *m_roots[rootRow];
    n.ahead  = ahead;
    n.behind = behind;
    const QModelIndex idx = createIndex(rootRow, 0, &n);
    emit dataChanged(idx, idx, {AheadRole, BehindRole});
}
```

- [ ] **Step 5: Run tests to verify green**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests "fetchState_*" "setFetchState_*"`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/repolistmodel.hpp ui/src/repolistmodel.cpp tests/ui/test_repo_list_model.cpp
git commit -m "feat(ui): per-repo fetch state + sync counts on RepoListModel"
```

---

## Task 3: `ProjectController::fetchAll` orchestration

Fan out a fresh `AsyncRepo` per repo, fetch in parallel, drive the model and an
aggregate summary. Credential *prompting* is Task 5; here the run uses the
controller's session credentials (default: ssh-agent on, empty userpass).

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp`
- Modify: `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp` (existing — add slots)

**Interfaces:**
- Consumes: `AsyncRepo::open` / `AsyncRepo::fetch(QString, Credentials)` /
  `AsyncRepo::syncStatus()` (Task 0, shipped); `RepoListModel::resetFetchStates` /
  `setFetchState` / `setSyncCounts` / `topLevelCount` (Task 2);
  `gittide::ui::isAuthError` (Task 1).
- Produces (on `ProjectController`):
  - `Q_PROPERTY(bool fetchingAll READ fetchingAll NOTIFY fetchingAllChanged)`
  - `Q_PROPERTY(QString fetchSummary READ fetchSummary NOTIFY fetchingAllChanged)`
  - `Q_INVOKABLE void fetchAll();` — no-op when no active project, no repos, or a
    fetch is already running
  - `signals: void fetchingAllChanged(); void fleetFetchFinished(int ok, int failed); void authRequired();`
  - private `QCoro::Task<void> fetchOne(int row, gittide::RepoRef ref);`
  - private `void finishOneFetch(bool ok);` (counter + finalize)

- [ ] **Step 1: Write the failing test**

Add a fixture helper + slots to `tests/ui/test_project_controller.cpp`. Add these
includes at the top of the file:

```cpp
#include "gittide/ui/asyncrepo.hpp"   // not strictly needed by the test, documents intent
#include "support/temprepo.hpp"
```

Add a private helper inside the test class that returns a working-repo path which
is **behind its origin by 1** (origin has an extra commit our repo hasn't fetched):

```cpp
    // Returns the path of a fresh working repo whose 'origin' is one commit ahead.
    // Kept alive by leaking the TempRepos into a member vector (cleaned in dtor).
    QString makeRepoBehindBy1()
    {
        auto repo = std::make_unique<gittide::test::TempRepo>();
        repo->setIdentity("Test", "test@example.com");
        repo->writeFile("a.txt", "one");
        repo->commitAll("c1");
        const auto bare = repo->addBareRemote("origin");
        repo->pushBranch("origin", "master");

        auto other = std::make_unique<gittide::test::TempRepo>();
        other->cloneFrom(bare);
        other->setIdentity("Other", "o@example.com");
        other->writeFile("a.txt", "two");
        other->commitAll("c2");
        other->pushBranch("origin", "master");

        const QString p = QString::fromStdString(repo->path().generic_string());
        m_temps.push_back(std::move(repo));
        m_temps.push_back(std::move(other));
        return p;
    }

    std::vector<std::unique_ptr<gittide::test::TempRepo>> m_temps;
```

(Place `#include <memory>` at the top if not already present.)

Add the test slots:

```cpp
void fetchAll_updates_behind_repos_and_marks_failures()
{
    using gittide::ui::RepoListModel;

    const QString behindA = makeRepoBehindBy1();
    const QString behindB = makeRepoBehindBy1();

    // A repo with no 'origin' remote -> fetch fails.
    gittide::test::TempRepo noRemote;
    noRemote.setIdentity("N", "n@e.x");
    noRemote.writeFile("x.txt", "x");
    noRemote.commitAll("c1");
    const QString failPath = QString::fromStdString(noRemote.path().generic_string());

    ProjectStore store;
    store.projects().push_back(Project{.id = "p1", .name = "Fleet",
        .repos = {RepoRef{.path = behindA.toStdString()},
                  RepoRef{.path = behindB.toStdString()},
                  RepoRef{.path = failPath.toStdString()}}});

    ProjectController controller(&store);
    controller.activate(QStringLiteral("p1"));

    QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
    QVERIFY(controller.fetchSummary().isEmpty());
    controller.fetchAll();
    QVERIFY(controller.fetchingAll());           // turns on synchronously
    QVERIFY(finished.wait(15000));               // all repos settle

    QCOMPARE(finished.at(0).at(0).toInt(), 2);   // ok
    QCOMPARE(finished.at(0).at(1).toInt(), 1);   // failed
    QVERIFY(!controller.fetchingAll());
    QCOMPARE(controller.fetchSummary(), QStringLiteral("2 fetched, 1 failed"));

    RepoListModel* m = controller.repos();
    QCOMPARE(m->data(m->index(0, 0), RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Updated));
    QCOMPARE(m->data(m->index(0, 0), RepoListModel::BehindRole).toInt(), 1);
    QCOMPARE(m->data(m->index(2, 0), RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Failed));
}

void fetchAll_no_active_project_is_noop()
{
    ProjectStore store;
    ProjectController controller(&store);
    QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
    controller.fetchAll();
    QVERIFY(!controller.fetchingAll());
    QCOMPARE(finished.count(), 0);
}
```

Add a `cleanup()` slot (or extend the existing one) so the temp repos are freed
between slots: `void cleanup() { m_temps.clear(); }`.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — `fetchAll`, `fetchingAll`, `fetchSummary`,
`fleetFetchFinished` are undefined.

- [ ] **Step 3: Declare the API in the header**

In `ui/include/gittide/ui/projectcontroller.hpp`:

Add includes near the top:

```cpp
#include <qcorotask.h>
#include "gittide/sync.hpp"
```

Add the properties after the existing `Q_PROPERTY` lines:

```cpp
    /// True while a project-wide fetch is in flight; QML disables the action and
    /// shows a spinner on the project header.
    Q_PROPERTY(bool fetchingAll READ fetchingAll NOTIFY fetchingAllChanged)
    /// Human-readable result of the last fleet fetch, e.g. "12 fetched, 1 failed".
    Q_PROPERTY(QString fetchSummary READ fetchSummary NOTIFY fetchingAllChanged)
```

Add the getters (in the `public:` accessor block):

```cpp
    bool fetchingAll() const { return m_fetchingAll; }
    QString fetchSummary() const { return m_fetchSummary; }
```

Add the slot (in `public slots:` / `Q_INVOKABLE` area):

```cpp
    // Fetch every non-missing repo in the active project in parallel. No-op when
    // there is no active project, no repos, or a fetch is already running.
    Q_INVOKABLE void fetchAll();
```

Add the signals:

```cpp
    void fetchingAllChanged();
    void fleetFetchFinished(int ok, int failed);
    void authRequired();
```

Add the private members and helpers:

```cpp
    bool                 m_fetchingAll = false;
    QString              m_fetchSummary;
    int                  m_fetchPending = 0;
    int                  m_fetchOk      = 0;
    int                  m_fetchFailed  = 0;
    bool                 m_authPrompted = false;       // emit authRequired at most once per run
    std::vector<int>     m_authFailedRows;             // rows that failed on auth (retried in submitFleetCredentials)
    gittide::Credentials m_sessionCred;

    QCoro::Task<void> fetchOne(int row, gittide::RepoRef ref);
    void              finishOneFetch();                // counter bookkeeping + finalize
```

- [ ] **Step 4: Implement in the .cpp**

In `ui/src/projectcontroller.cpp`, add includes:

```cpp
#include "gittide/ui/asyncrepo.hpp"
#include "gittide/ui/autherror.hpp"
#include <qcorotask.h>
```

Add the implementation (after `cloneRepo`):

```cpp
void ProjectController::fetchAll()
{
    if (m_activeId.isEmpty() || m_fetchingAll)
        return;

    const auto& repos = activeRepos();
    if (repos.empty())
        return;

    m_repoModel->resetFetchStates();
    m_fetchOk      = 0;
    m_fetchFailed  = 0;
    m_authPrompted = false;
    m_authFailedRows.clear();

    // Only fetch non-missing top-level repos. A missing repo is SKIPPED — left in
    // its existing (missing) rendering, not counted as a failure (per spec).
    std::vector<int> rows;
    for (int row = 0; row < static_cast<int>(repos.size()); ++row)
    {
        const std::filesystem::path p(repos[row].path);
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !ec)
            rows.push_back(row);
    }

    if (rows.empty())
        return; // nothing fetchable; leave fetchingAll false, no summary change

    m_fetchPending = static_cast<int>(rows.size());
    m_fetchingAll  = true;
    emit fetchingAllChanged();

    for (int row : rows)
        QCoro::connect(fetchOne(row, repos[row]), this, [] {});
}

QCoro::Task<void> ProjectController::fetchOne(int row, gittide::RepoRef ref)
{
    m_repoModel->setFetchState(row, RepoListModel::FetchState::Running);

    // Each repo gets its OWN handle — the one-owner invariant holds; we never
    // touch the active RepoController's repo. The AsyncRepo lives in this
    // coroutine frame, so it stays valid across every co_await below.
    auto opened = AsyncRepo::open(std::filesystem::path(ref.path));
    if (!opened)
    {
        m_repoModel->setFetchState(row, RepoListModel::FetchState::Failed,
                                   QString::fromStdString(opened.error().message));
        m_fetchFailed++;
        finishOneFetch();
        co_return;
    }
    AsyncRepo repo = std::move(*opened);

    auto fr = co_await repo.fetch(QStringLiteral("origin"), m_sessionCred);
    if (!fr)
    {
        if (gittide::ui::isAuthError(fr.error()))
        {
            m_authFailedRows.push_back(row);
            if (!m_authPrompted)
            {
                m_authPrompted = true;
                emit authRequired();
            }
        }
        m_repoModel->setFetchState(row, RepoListModel::FetchState::Failed,
                                   QString::fromStdString(fr.error().message));
        m_fetchFailed++;
        finishOneFetch();
        co_return;
    }

    // Refresh ahead/behind so the sidebar shows incoming commits. behind > 0 is
    // our first-cut heuristic for "Updated" vs "UpToDate" (fetch itself does not
    // report whether refs moved).
    int behind = 0, ahead = 0;
    if (auto st = co_await repo.syncStatus(); st)
    {
        ahead  = st->ahead;
        behind = st->behind;
        m_repoModel->setSyncCounts(row, ahead, behind);
    }
    m_repoModel->setFetchState(row, behind > 0 ? RepoListModel::FetchState::Updated
                                               : RepoListModel::FetchState::UpToDate);
    m_fetchOk++;
    finishOneFetch();
}

void ProjectController::finishOneFetch()
{
    // fetchOne resumes on the UI thread after each co_await, so this runs
    // single-threaded — plain counters are safe.
    if (--m_fetchPending > 0)
        return;

    m_fetchingAll  = false;
    m_fetchSummary = QStringLiteral("%1 fetched, %2 failed").arg(m_fetchOk).arg(m_fetchFailed);
    emit fetchingAllChanged();
    emit fleetFetchFinished(m_fetchOk, m_fetchFailed);
}
```

> `m_fetchOk` / `m_fetchFailed` are explicit counters incremented in `fetchOne`'s
> success / failure paths. Missing repos are skipped before launch and counted in
> neither — only *attempted* repos appear in the summary.

- [ ] **Step 5: Run tests to verify green**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests "fetchAll_*"`
Expected: PASS — both `fetchAll_*` slots green; existing `TestProjectController`
slots still pass.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): ProjectController::fetchAll fans out a parallel fleet fetch"
```

---

## Task 4: Sidebar UI — Fetch-all action + per-repo status

Wire the entry point and the inline per-repo glyphs/badges in QML.

**Files:**
- Modify: `ui/qml/Sidebar.qml` (header action + summary; repo delegate glyph + behind badge)
- Test: `tests/ui/test_qml_sync.cpp` (existing — add a headless presence/binding check)

**Interfaces:**
- Consumes: context object `projectController` (`fetchAll()`, `fetchingAll`,
  `fetchSummary`); `repoModel` roles `fetchState`, `behind` (Task 2/3).
  `RepoListModel::FetchState` integer values: `Idle=0, Running=1, UpToDate=2,
  Updated=3, Failed=4`.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_qml_sync.cpp` a slot that loads the shell and asserts the
fetch-all button exists and is bound to `fetchingAll`. Follow the existing file's
load pattern (`QQmlApplicationEngine` + `findChild` by `objectName`). Example slot
(adapt to the file's existing helpers/engine setup):

```cpp
void sidebar_exposes_fetchAll_button()
{
    QQmlApplicationEngine engine;
    // ... existing context install used elsewhere in this file ...
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/GitTide/qml/Main.qml")));
    QVERIFY(!engine.rootObjects().isEmpty());

    QObject* root = engine.rootObjects().first();
    QObject* btn  = root->findChild<QObject*>(QStringLiteral("fetchAllButton"));
    QVERIFY(btn != nullptr);
    QVERIFY(btn->property("enabled").toBool()); // not fetching -> enabled
}
```

> If `test_qml_sync.cpp` already has a shared engine/context helper, reuse it
> rather than re-creating the engine. The exact QML URL must match how other
> slots in this file load `Main.qml`.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests "sidebar_exposes_fetchAll_button"`
Expected: FAIL — no child named `fetchAllButton`.

- [ ] **Step 3: Add the header action + summary**

In `ui/qml/Sidebar.qml`, inside the top header `RowLayout` (the one holding
`themeToggle`, near the top of the file), add a fetch-all button + spinner. Use
theme tokens for colour (no hex literals):

```qml
ToolButton {
    objectName: "fetchAllButton"
    text: qsTr("Fetch all")
    enabled: projectController && projectController.activeProjectId.length > 0
             && !projectController.fetchingAll
    onClicked: if (projectController) projectController.fetchAll()
}

BusyIndicator {
    running: projectController && projectController.fetchingAll
    visible: running
    implicitWidth: 16
    implicitHeight: 16
}

Label {
    objectName: "fetchSummary"
    visible: projectController && projectController.fetchSummary.length > 0
             && !projectController.fetchingAll
    text: projectController ? projectController.fetchSummary : ""
    color: theme.textMuted          // existing muted token; adjust to the real token name
    elide: Text.ElideRight
}
```

> Use the muted-text token already used elsewhere in this file (grep the file for
> `theme.` to find the exact property name; do not introduce a hex literal).

- [ ] **Step 4: Add per-repo glyph + behind badge in the repo delegate**

In the repo-row delegate (where `repoPath`, `missing` etc. are already bound),
add a status indicator driven by the `fetchState` role and a behind badge driven
by `behind`. Place it at the trailing edge of the row:

```qml
// Trailing fetch status. fetchState: 0 Idle, 1 Running, 2 UpToDate, 3 Updated, 4 Failed.
BusyIndicator {
    running: model.fetchState === 1
    visible: running
    implicitWidth: 14; implicitHeight: 14
}
Label {
    visible: model.fetchState === 3 && model.behind > 0   // Updated, has incoming
    text: "↓" + model.behind
    color: theme.accent                                   // adjust to real token
    ToolTip.visible: false
}
Label {
    visible: model.fetchState === 2                       // UpToDate
    text: "✓"
    color: theme.textMuted
}
Label {
    visible: model.fetchState === 4                       // Failed
    text: "!"
    color: theme.danger                                   // adjust to real token (state.deleted / error)
    ToolTip.text: model.fetchError
    ToolTip.visible: hovered !== undefined ? hovered : false
}
```

> Match the delegate's existing `model.<role>` access style (some delegates use a
> `required property` declaration instead of `model.x` — follow whatever the file
> already does). Use real theme token names found in the file.

- [ ] **Step 5: Run tests to verify green**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests "sidebar_exposes_fetchAll_button"`
Expected: PASS.

- [ ] **Step 6: Manual check**

Build and run the app (`README.md` §Build & test). Open a project with ≥2 repos
that have an `origin`, click **Fetch all**: rows show a spinner, then ✓ / ↓N /
glyph; the header shows the summary. (Real remotes require network — verify
against repos you can reach.)

- [ ] **Step 7: Commit**

```bash
git add ui/qml/Sidebar.qml tests/ui/test_qml_sync.cpp
git commit -m "feat(ui): fetch-all action + inline per-repo fetch status in the sidebar"
```

---

## Task 5: Prompt-once-on-demand credentials for the fleet

When a repo's fetch fails on auth, prompt once, cache the token at the controller
level, and retry the auth-failed repos. The retry orchestration reuses Task 3's
machinery.

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp`
- Modify: `ui/src/projectcontroller.cpp`
- Modify: `ui/qml/Sidebar.qml` (open the existing `CredentialDialog` on `authRequired`)
- Test: `tests/ui/test_project_controller.cpp` (existing — add a plumbing slot)

**Interfaces:**
- Produces (on `ProjectController`):
  - `Q_INVOKABLE void submitFleetCredentials(const QString& username, const QString& token);`
    — stores the token in the session credentials and re-fetches the rows that
    previously failed on auth. No-op when no rows are pending auth.

- [ ] **Step 1: Write the failing test**

Add to `tests/ui/test_project_controller.cpp`:

```cpp
void submitFleetCredentials_with_no_pending_is_safe_noop()
{
    ProjectStore store;
    store.projects().push_back(Project{.id = "p1", .name = "Fleet"});
    ProjectController controller(&store);
    controller.activate(QStringLiteral("p1"));

    QSignalSpy finished(&controller, &ProjectController::fleetFetchFinished);
    controller.submitFleetCredentials(QStringLiteral("u"), QStringLiteral("t")); // nothing pending
    QVERIFY(!controller.fetchingAll());
    QCOMPARE(finished.count(), 0);
}
```

> End-to-end auth (a real failing-then-succeeding fetch) is **not** unit-tested —
> there is no network in tests and a local `file://` remote needs no auth (see
> Global constraints). This slot covers the no-op plumbing; the prompt→retry path
> is verified manually in Step 5.

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — `submitFleetCredentials` is undefined.

- [ ] **Step 3: Declare + implement**

Header (`public slots:` / `Q_INVOKABLE`):

```cpp
    Q_INVOKABLE void submitFleetCredentials(const QString& username, const QString& token);
```

Implementation (`ui/src/projectcontroller.cpp`):

```cpp
void ProjectController::submitFleetCredentials(const QString& username, const QString& token)
{
    if (m_authFailedRows.empty() || m_fetchingAll)
        return;

    m_sessionCred.username    = username.toStdString();
    m_sessionCred.password    = token.toStdString();
    m_sessionCred.sshUseAgent = true;

    const std::vector<int> retry = std::move(m_authFailedRows);
    m_authFailedRows.clear();
    m_authPrompted = false;

    // These rows were counted as failures in the initial run; we are re-attempting
    // them, so back them out — fetchOne re-counts each into ok or failed.
    m_fetchFailed -= static_cast<int>(retry.size());

    const auto& repos = activeRepos();
    m_fetchPending    = static_cast<int>(retry.size());
    m_fetchingAll     = true;
    emit fetchingAllChanged();

    for (int row : retry)
    {
        if (row >= 0 && row < static_cast<int>(repos.size()))
            QCoro::connect(fetchOne(row, repos[row]), this, [] {});
    }
}
```

> `fetchOne` already increments `m_fetchOk` on success and re-queues auth failures
> into `m_authFailedRows`, so a retry naturally re-prompts if the new token is
> also rejected. `m_fetchOk` accumulates across the initial run and retries, so
> the summary reflects the full fleet.

- [ ] **Step 4: Wire the QML prompt**

In `ui/qml/Sidebar.qml`, connect the controller's `authRequired` to the existing
`CredentialDialog` (the same component the per-repo BranchBar flow uses — grep
the QML for `CredentialDialog` to find its id/usage). On accept, call
`projectController.submitFleetCredentials(username, token)`:

```qml
Connections {
    target: projectController
    function onAuthRequired() { fleetCredentialDialog.open() }
}

CredentialDialog {
    id: fleetCredentialDialog
    onAccepted: projectController.submitFleetCredentials(username, token)
}
```

> Reuse the existing `CredentialDialog` API (property/signal names) exactly as the
> per-repo flow uses it; do not add a second dialog component.

- [ ] **Step 5: Manual check**

Run the app against a private HTTPS remote with no cached token. **Fetch all**:
that repo lands `Failed`, the credential dialog opens once; entering a valid token
re-fetches it (and any other auth-failed repos) to ✓ / ↓N.

- [ ] **Step 6: Run tests + commit**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests "*"`
Expected: PASS (full suite).

```bash
git add ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp ui/qml/Sidebar.qml tests/ui/test_project_controller.cpp
git commit -m "feat(ui): prompt-once-on-demand credentials for fleet fetch-all"
```

---

## Outcome

> Fill in when the plan reaches `done`:
>
> - Shipped: project-wide fetch-all (parallel, inline sidebar status, refreshed
>   behind badges, aggregate summary, prompt-once credentials).
> - Spec updated: `spec/product/product.md` §Syncing → Fleet fetch;
>   `spec/engineering/engineering.md` §Fleet fetch-all (already authored).
> - Code: `ProjectController::fetchAll`/`fetchOne`, `RepoListModel` fetch-state
>   roles, `gittide::ui::isAuthError`, `Sidebar.qml`.
> - Wishlist: move/keep `network-sync.md` note — fleet fetch-all done; fleet
>   pull-all still deferred.
