# Plan 24 — Submodule init / update from the GUI

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [x]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-06-26 |
| **Status** | `done` |
| **Spec** | [`spec/product/2026-06-26-submodule-init-update-design.md`](../spec/product/2026-06-26-submodule-init-update-design.md); updates [`spec/product/product.md` §Submodules](../spec/product/product.md) and [`spec/product/context-menus.md`](../spec/product/context-menus.md) |
| **Depends on** | Plan 21 (live refresh / D35), Plan 16 (context menus) |

**Goal:** Let the user initialise, update (to pinned commit), and deinitialise
git submodules from the GUI — for any repo in the sidebar — and keep the sidebar
tree in sync when submodules change on disk (including external terminal use).

**Architecture:** Reuse existing core (`reinitSubmodule`, `deinitSubmodule`,
`submoduleTree`) and the D35 refresh infra. Add one core method
(`updateSubmodules`, one level). Operations run on **any repo by path** via a
transient `AsyncRepo` in `ProjectController` (the same one-owner pattern
`pollRepos` uses). A new `RepoListModel::applySubmodules` does a *diffing*,
targeted subtree update (no full reset, so expansion/selection survive and a
no-change call is a no-op). Refresh off the active repo's git-dir watch and the
existing 5 s fleet poll.

**Tech stack:** C++23, libgit2 (core, private), Qt 6 Quick + QCoro coroutines
(ui), Catch2 (core tests), QtTest (ui tests).

## Global constraints

- **No Qt in `core/`**; core speaks `std` and returns `Expected<T>`
  (`std::expected<T, GitError>`). See
  [`spec/engineering/engineering.md`](../spec/engineering/engineering.md).
- **One owner per `GitRepo`** — each operation/poll uses its own transient
  handle; never share. `GitRepo` is not thread-safe — every core call goes
  through `AsyncRepo`'s `std::scoped_lock(impl->mutex)`.
- **Paths:** core uses `std::filesystem::path`; convert with `generic_u8string()`
  / `generic_string()`, never `.string()`.
- **`reinitSubmodule` / `deinitSubmodule` take a REPO-RELATIVE path**; the model
  and `submoduleTree()` carry ABSOLUTE paths → the controller converts via
  `std::filesystem::relative(abs, repoRoot)`.
- **Colour from a theme token**, never a hex literal in QML.
- This plan adds **no new C++ source or test files**: core/ui changes touch
  existing translation units, and every test is a new slot on an already-wired
  test class — so **no `CMakeLists.txt` or `tests/ui/main.cpp` edits are
  expected**. The one new file is QML (`SubmoduleContextMenu.qml`) — register it
  wherever the other `*ContextMenu.qml` are listed (qrc/CMake resource). Verify.
- Must keep passing: existing `test_git_repo_submodules` and the merge-conflict
  deinit/reinit flow in `RepoController`.

---

## Task 1: Core — `GitRepo::updateSubmodules()` (one level)

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare next to the other submodule
  methods, ~line 166–179)
- Modify: `core/src/gitrepo.cpp` (implement after `reinitSubmodule`, ~line 1036)
- Test: `tests/test_git_repo_submodules.cpp`

**Interfaces:**
- Produces: `gittide::Expected<void> GitRepo::updateSubmodules();` — initialise/
  update **every direct** submodule of this repo to its pinned commit. One level
  only (no recursion). Stops and returns the first failure.

- [x] **Step 1: Write the failing test.** Append to
  `tests/test_git_repo_submodules.cpp` (Catch2; this file already uses
  `gittide::test::TempRepo` with `writeFile` / `commitAll` / `addSubmodule` — copy
  its existing fixture style). Build a parent with one submodule, force it
  uninitialised via the existing `deinitSubmodule`, then assert
  `updateSubmodules()` brings it back to `Clean`:

```cpp
TEST_CASE("updateSubmodules re-initialises all direct submodules", "[submodule]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "hello\n");
    child.commitAll("seed child");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("seed parent");
    parent.addSubmodule("sub", child.path());
    parent.commitAll("add submodule");

    auto repo = gittide::GitRepo::open(parent.path());
    REQUIRE(repo);

    // Force the submodule to Uninitialized (deinitSubmodule takes a repo-relative path).
    REQUIRE(repo->deinitSubmodule("sub"));
    {
        auto tree = repo->submoduleTree();
        REQUIRE(tree);
        REQUIRE(tree->size() == 1);
        REQUIRE((*tree)[0].status == gittide::SubmoduleStatus::Uninitialized);
    }

    // Update all direct submodules → back to Clean.
    REQUIRE(repo->updateSubmodules());

    auto tree = repo->submoduleTree();
    REQUIRE(tree);
    REQUIRE(tree->size() == 1);
    REQUIRE((*tree)[0].status == gittide::SubmoduleStatus::Clean);
}
```

- [x] **Step 2: Run it, verify it fails.**
  `cmake --build build --parallel && ctest --test-dir build -R submodule --output-on-failure`
  Expected: FAIL — `updateSubmodules` undefined / link error.

- [x] **Step 3: Declare in `gitrepo.hpp`** beside the other submodule methods:

```cpp
/// Initialise/update every DIRECT submodule of this repo to its pinned
/// commit (one level — does not recurse into nested submodules). Mirrors
/// `git submodule update --init` without `--recursive`. Returns the first
/// failure encountered; submodules processed before it stay updated.
Expected<void> updateSubmodules();
```

- [x] **Step 4: Implement in `gitrepo.cpp`** after `reinitSubmodule`. Collect
  names inside the `foreach` (mutating the working tree *inside* the callback is
  unsafe — `submoduleTree` already documents this), then look up + update each
  afterwards, reusing the exact options `reinitSubmodule` uses:

```cpp
Expected<void> GitRepo::updateSubmodules()
{
    // Collect direct submodule names first; updating inside git_submodule_foreach
    // is unsafe while libgit2 holds the submodule cache (see submoduleTree).
    std::vector<std::string> names;
    auto cb = [](git_submodule*, const char* name, void* pl) -> int
    {
        static_cast<std::vector<std::string>*>(pl)->emplace_back(name ? name : "");
        return 0;
    };
    int rc = git_submodule_foreach(m_repo, cb, &names);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    for (const std::string& name : names)
    {
        git_submodule* sm = nullptr;
        rc                = git_submodule_lookup(&sm, m_repo, name.c_str());
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_submodule, decltype(&git_submodule_free)> guard(sm, git_submodule_free);

        git_submodule_update_options opts    = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
        opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;
        rc                                   = git_submodule_update(sm, /*init=*/1, &opts);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    return {};
}
```

- [x] **Step 5: Run the test, verify it passes.**
  `ctest --test-dir build -R submodule --output-on-failure` → PASS. No new
  warnings.

- [x] **Step 6: Commit.**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_submodules.cpp
git commit -m "feat(core): updateSubmodules() — init/update all direct submodules one level"
```

---

## Task 2: Async — `updateSubmodules()` + `submoduleTree()` wrappers

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp` (beside `reinitSubmodule`, ~125)
- Modify: `ui/src/asyncrepo.cpp` (beside `reinitSubmodule`, ~520–529)

**Interfaces:**
- Produces:
  - `QCoro::Task<gittide::Expected<void>> AsyncRepo::updateSubmodules();`
  - `QCoro::Task<gittide::Expected<std::vector<gittide::SubmoduleNode>>> AsyncRepo::submoduleTree();`
- These are thin pass-throughs; they are exercised by Task 4's controller test
  (no standalone test — matches how `reinitSubmodule`/`deinitSubmodule` wrappers
  are tested through their callers).

- [x] **Step 1: Declare in `asyncrepo.hpp`** (add `#include "gittide/submodule.hpp"`
  if not already transitively available):

```cpp
/// Initialise/update every direct submodule to its pinned commit (one level).
QCoro::Task<gittide::Expected<void>> updateSubmodules();

/// Enumerate this repo's recursive submodule tree off the UI thread.
QCoro::Task<gittide::Expected<std::vector<gittide::SubmoduleNode>>> submoduleTree();
```

- [x] **Step 2: Implement in `asyncrepo.cpp`** following the existing
  `reinitSubmodule` shape exactly:

```cpp
QCoro::Task<gittide::Expected<void>> AsyncRepo::updateSubmodules()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.updateSubmodules();
        });
}

QCoro::Task<gittide::Expected<std::vector<gittide::SubmoduleNode>>> AsyncRepo::submoduleTree()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.submoduleTree();
        });
}
```

- [x] **Step 3: Build to verify it compiles.**
  `cmake --build build --parallel` → no errors, no new warnings.

- [x] **Step 4: Commit.**

```bash
git add ui/include/gittide/ui/asyncrepo.hpp ui/src/asyncrepo.cpp
git commit -m "feat(ui): AsyncRepo wrappers for updateSubmodules + submoduleTree"
```

---

## Task 3: Model — `applySubmodules()` diffing refresh + busy/owner roles

**Files:**
- Modify: `ui/include/gittide/ui/repolistmodel.hpp`
- Modify: `ui/src/repolistmodel.cpp`
- Test: `tests/ui/test_repo_list_model.cpp` — **add a new private slot** to the
  existing `TestRepoListModel` class (the UI tests use a custom runner; each
  `test_*.cpp` is `#include`-d into `tests/ui/main.cpp` and the class is already
  registered + `RUN()`-invoked there — so **no CMake or main.cpp edit is needed**).

**Interfaces:**
- Produces on `RepoListModel`:
  - `void applySubmodules(const QString& repoPath, const std::vector<gittide::SubmoduleNode>& subs);`
    — replace the submodule children of the top-level repo node whose `path ==
    repoPath`, **only when they differ** (a structurally identical call is a
    no-op, emitting nothing). Preserves the top-level row's own expansion.
  - `void setSubmoduleBusy(const QString& submodulePath, bool busy);` — toggle a
    per-row spinner flag (matched by absolute path) and emit `dataChanged`.
  - New roles: `BusyRole`, `OwnerRepoPathRole` (the path of the nearest top-level
    ancestor — what the controller needs as the repo handle).
- Consumes: `gittide::SubmoduleNode` (`core`), existing `Node` tree.

- [x] **Step 1: Write the failing test.** Add a private slot to the existing
  `TestRepoListModel` class in `tests/ui/test_repo_list_model.cpp`. Drive the
  model directly with hand-built `gittide::SubmoduleNode`s (no real repo needed for
  the model logic). Assert: applying a changed subtree inserts rows and emits;
  applying an identical subtree emits nothing. (The file already includes
  `gittide/submodule.hpp` and `gittide/projectstore.hpp`.)

```cpp
    void applySubmodules_insertsThenNoOpsWhenUnchanged()
    {
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        // Path need not exist: setRepos builds no children for a missing path.
        model.setRepos({RepoRef{.path = "/tmp/gittide-parent", .alias = "parent"}});
        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.rowCount(top), 0);

        SubmoduleNode sub;
        sub.name     = "sub";
        sub.path     = "/tmp/gittide-parent/sub";
        sub.status   = SubmoduleStatus::Clean;
        sub.shortOid = "abc1234";

        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.applySubmodules(QStringLiteral("/tmp/gittide-parent"), {sub});
        QCOMPARE(model.rowCount(model.index(0, 0)), 1);
        QCOMPARE(inserted.count(), 1);

        const QModelIndex subIdx = model.index(0, 0, model.index(0, 0));
        QCOMPARE(model.data(subIdx, RepoListModel::OwnerRepoPathRole).toString(),
                 QStringLiteral("/tmp/gittide-parent"));

        // Identical apply → no-op: no insert/remove/dataChanged.
        QSignalSpy inserted2(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed2(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed2(&model, &QAbstractItemModel::dataChanged);
        model.applySubmodules(QStringLiteral("/tmp/gittide-parent"), {sub});
        QCOMPARE(inserted2.count(), 0);
        QCOMPARE(removed2.count(), 0);
        QCOMPARE(changed2.count(), 0);

        // Busy flag toggles and emits a dataChanged on the row.
        QSignalSpy busySpy(&model, &QAbstractItemModel::dataChanged);
        model.setSubmoduleBusy(QStringLiteral("/tmp/gittide-parent/sub"), true);
        QCOMPARE(model.data(model.index(0, 0, model.index(0, 0)),
                            RepoListModel::BusyRole).toBool(), true);
        QCOMPARE(busySpy.count(), 1);
    }
```

- [x] **Step 2: Run it, verify it fails.**
  `cmake --build build --parallel` → `applySubmodules` / `OwnerRepoPathRole`
  undefined.

- [x] **Step 3: Add roles + declarations** in `repolistmodel.hpp`. Extend the
  `Roles` enum with `BusyRole` and `OwnerRepoPathRole`, add a `bool busy=false;`
  to `Node`, and declare the two methods (public) plus a private helper:

```cpp
// in enum Roles, after BehindRole:
BusyRole,
OwnerRepoPathRole,

// public methods:
void applySubmodules(const QString& repoPath, const std::vector<gittide::SubmoduleNode>& subs);
void setSubmoduleBusy(const QString& submodulePath, bool busy);

// private:
// True when `subs` matches `node`'s existing submodule children exactly
// (path + status + shortOid, recursively) — lets applySubmodules no-op.
bool submodulesEqual(const Node& node, const std::vector<gittide::SubmoduleNode>& subs) const;
Node* findRoot(const QString& repoPath);   // top-level node by exact path, or nullptr
Node* findByPath(const QString& path);     // any node by exact path, or nullptr
Node* topLevelAncestor(Node* node) const;  // walk parents to the root
```

- [x] **Step 4: Implement in `repolistmodel.cpp`.**

  Wire the new roles in `data()` and `roleNames()`:

```cpp
// data():
case BusyRole:
    return node->busy;
case OwnerRepoPathRole:
    return topLevelAncestor(const_cast<Node*>(node))->path;

// roleNames():
roles[BusyRole]          = "submoduleBusy";
roles[OwnerRepoPathRole] = "ownerRepoPath";
```

  Helpers:

```cpp
RepoListModel::Node* RepoListModel::findRoot(const QString& repoPath)
{
    for (auto& r : m_roots)
        if (r->path == repoPath)
            return r.get();
    return nullptr;
}

RepoListModel::Node* RepoListModel::findByPath(const QString& path)
{
    Node* match = nullptr;
    auto walk = [&](auto&& self, std::vector<std::unique_ptr<Node>>& nodes) -> void
    {
        for (auto& n : nodes)
        {
            if (match) return;
            if (n->path == path) { match = n.get(); return; }
            self(self, n->children);
        }
    };
    walk(walk, m_roots);
    return match;
}

RepoListModel::Node* RepoListModel::topLevelAncestor(Node* node) const
{
    while (node && node->parent)
        node = node->parent;
    return node;
}

bool RepoListModel::submodulesEqual(const Node& node,
                                    const std::vector<gittide::SubmoduleNode>& subs) const
{
    if (node.children.size() != subs.size())
        return false;
    for (std::size_t i = 0; i < subs.size(); ++i)
    {
        const Node& c = *node.children[i];
        const auto& s = subs[i];
        if (!c.isSubmodule
            || c.path != QString::fromStdString(s.path.generic_string())
            || c.status != s.status
            || c.shortOid != QString::fromStdString(s.shortOid)
            || !submodulesEqual(c, s.children))
            return false;
    }
    return true;
}
```

  The refresh itself — no-op when unchanged; otherwise replace the root's
  children wholesale (remove-all then insert-new under that root index, leaving
  the root's own expansion intact):

```cpp
void RepoListModel::applySubmodules(const QString& repoPath,
                                    const std::vector<gittide::SubmoduleNode>& subs)
{
    Node* root = findRoot(repoPath);
    if (!root || submodulesEqual(*root, subs))
        return;

    const QModelIndex rootIdx = createIndex(rowOf(root), 0, root);

    if (!root->children.empty())
    {
        beginRemoveRows(rootIdx, 0, static_cast<int>(root->children.size()) - 1);
        root->children.clear();
        endRemoveRows();
    }
    if (!subs.empty())
    {
        beginInsertRows(rootIdx, 0, static_cast<int>(subs.size()) - 1);
        appendSubmodules(*root, subs);
        endInsertRows();
    }
}

void RepoListModel::setSubmoduleBusy(const QString& submodulePath, bool busy)
{
    Node* n = findByPath(submodulePath);
    if (!n || n->busy == busy)
        return;
    n->busy               = busy;
    const QModelIndex idx = createIndex(rowOf(n), 0, n);
    emit dataChanged(idx, idx, {BusyRole});
}
```

  > Note: `appendSubmodules` already sets `parent` pointers, so nested children
  > inserted here resolve `parent()` correctly.

- [x] **Step 5: Run the test, verify it passes.** The UI tests build into one
  binary; run it (the harness runs `TestRepoListModel` among others):
  `ctest --test-dir build -R ui --output-on-failure` (or run the ui test binary
  directly) → PASS.

- [x] **Step 6: Commit.**

```bash
git add ui/include/gittide/ui/repolistmodel.hpp ui/src/repolistmodel.cpp tests/ui/test_repo_list_model.cpp
git commit -m "feat(ui): RepoListModel.applySubmodules diffing refresh + busy/owner roles"
```

---

## Task 4: Controller — submodule op slots + refresh + failure signal

**Files:**
- Modify: `ui/include/gittide/ui/projectcontroller.hpp`
- Modify: `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp` — **add a private slot** to the
  existing `TestProjectController` class (already registered + `RUN()`-ed in
  `tests/ui/main.cpp`; no CMake/main.cpp edit). It already includes
  `asyncrepo.hpp`, `projectcontroller.hpp`, `repolistmodel.hpp`,
  `support/temprepo.hpp`, `<qcorotask.h>`.

**Interfaces:**
- Produces on `ProjectController` (all `Q_INVOKABLE`):
  - `QCoro::Task<void> refreshSubmodules(QString repoPath);` — open a transient
    `AsyncRepo` for `repoPath`, fetch `submoduleTree()` off-thread, call
    `m_repoModel->applySubmodules(repoPath, tree)`.
  - `QCoro::Task<void> initSubmodule(QString repoPath, QString submodulePath);`
    — `reinitSubmodule(relative)`; on success `refreshSubmodules(repoPath)`.
  - `QCoro::Task<void> updateAllSubmodules(QString repoPath);` —
    `updateSubmodules()`; then `refreshSubmodules(repoPath)`.
  - `QCoro::Task<void> deinitSubmodule(QString repoPath, QString submodulePath);`
    — `deinitSubmodule(relative)`; then `refreshSubmodules(repoPath)`.
  - Signal: `void submoduleOpFailed(const QString& repoPath, const QString& submodulePath, const QString& message);`
- Path rule: `submodulePath` is **absolute**; convert with
  `std::filesystem::relative(submodulePath, repoPath)` before the core call.

- [x] **Step 1: Write the failing test.** Add this slot to
  `TestProjectController`. It builds a parent+submodule on disk, deinits the
  submodule, activates a project containing the parent, then awaits
  `initSubmodule` with `QCoro::waitFor` (the idiom this file already uses for
  `cloneRepo`) and asserts the model row flips `Uninitialized → Clean`.

```cpp
    void initSubmodule_reinitialises_and_refreshes_tree()
    {
        using gittide::SubmoduleStatus;
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("seed child");

        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("seed parent");
        parent.addSubmodule("sub", child.path());
        parent.commitAll("add submodule");
        {
            auto repo = gittide::GitRepo::open(parent.path());
            QVERIFY(repo && repo->deinitSubmodule("sub"));
        }

        const QString repoPath = QString::fromStdString(parent.path().generic_string());
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "p", .name = "P", .repos = {RepoRef{.path = repoPath.toStdString()}}});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("p"));

        RepoListModel* model = controller.repos();
        const QModelIndex top = model->index(0, 0);
        QCOMPARE(model->rowCount(top), 1);
        const QModelIndex sub = model->index(0, 0, top);
        QCOMPARE(model->data(sub, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Uninitialized)); // == 2

        const QString subPath = repoPath + QStringLiteral("/sub");
        QCoro::waitFor(controller.initSubmodule(repoPath, subPath));

        // Subtree was rebuilt; the (new) row is now Clean.
        const QModelIndex sub2 = model->index(0, 0, model->index(0, 0));
        QCOMPARE(model->data(sub2, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Clean));        // == 0
    }
```

- [x] **Step 2: Run it, verify it fails** — `initSubmodule` undefined.

- [x] **Step 3: Declare** in `projectcontroller.hpp`. Add the signal under
  `signals:` and the slots under `public slots:`:

```cpp
// signals:
void submoduleOpFailed(const QString& repoPath, const QString& submodulePath, const QString& message);

// public slots:
Q_INVOKABLE QCoro::Task<void> refreshSubmodules(QString repoPath);
Q_INVOKABLE QCoro::Task<void> initSubmodule(QString repoPath, QString submodulePath);
Q_INVOKABLE QCoro::Task<void> updateAllSubmodules(QString repoPath);
Q_INVOKABLE QCoro::Task<void> deinitSubmodule(QString repoPath, QString submodulePath);
```

  Add includes as needed (`<qcorotask.h>` already present;
  `#include "gittide/ui/asyncrepo.hpp"` and `#include "gittide/ui/repolistmodel.hpp"`
  in the `.cpp`).

- [x] **Step 4: Implement** in `projectcontroller.cpp`. A small helper keeps the
  three op slots DRY:

```cpp
QCoro::Task<void> ProjectController::refreshSubmodules(QString repoPath)
{
    QPointer<ProjectController> self = this;
    const std::filesystem::path p(repoPath.toStdString());
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec)
        co_return;

    auto opened = AsyncRepo::open(p);
    if (!opened)
        co_return;
    AsyncRepo repo = std::move(*opened);
    auto      tree = co_await repo.submoduleTree();
    if (!self || !tree)
        co_return;
    m_repoModel->applySubmodules(repoPath, *tree);
}
```

  ```cpp
  // Shared body for the three mutating ops. `run` performs the core call on a
  // transient handle; busy flag drives the row spinner; refresh + error routing
  // are uniform. submodulePath may be empty (bulk update-all on repoPath).
  QCoro::Task<void> ProjectController::initSubmodule(QString repoPath, QString submodulePath)
  {
      co_await runSubmoduleOp(repoPath, submodulePath,
          [](AsyncRepo& r, std::filesystem::path rel) { return r.reinitSubmodule(std::move(rel)); });
  }

  QCoro::Task<void> ProjectController::deinitSubmodule(QString repoPath, QString submodulePath)
  {
      co_await runSubmoduleOp(repoPath, submodulePath,
          [](AsyncRepo& r, std::filesystem::path rel) { return r.deinitSubmodule(std::move(rel)); });
  }

  QCoro::Task<void> ProjectController::updateAllSubmodules(QString repoPath)
  {
      co_await runSubmoduleOp(repoPath, /*submodulePath=*/QString{},
          [](AsyncRepo& r, std::filesystem::path) { return r.updateSubmodules(); });
  }
  ```

  Declare the private helper in the header and implement it:

```cpp
// projectcontroller.hpp (private):
// op receives (handle, repo-relative submodule path) and returns the task to
// await. relPath is empty for repo-wide ops (updateAllSubmodules).
template <class Op>
QCoro::Task<void> runSubmoduleOp(QString repoPath, QString submodulePath, Op op);
```

```cpp
// projectcontroller.cpp:
template <class Op>
QCoro::Task<void> ProjectController::runSubmoduleOp(QString repoPath, QString submodulePath, Op op)
{
    QPointer<ProjectController> self = this;
    const std::filesystem::path root(repoPath.toStdString());
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec)
        co_return;

    std::filesystem::path rel;
    if (!submodulePath.isEmpty())
    {
        rel = std::filesystem::relative(std::filesystem::path(submodulePath.toStdString()), root, ec);
        if (ec)
            rel = std::filesystem::path(submodulePath.toStdString());
        m_repoModel->setSubmoduleBusy(submodulePath, true);
    }

    auto opened = AsyncRepo::open(root);
    if (!opened)
    {
        if (self && !submodulePath.isEmpty())
            m_repoModel->setSubmoduleBusy(submodulePath, false);
        if (self)
            emit submoduleOpFailed(repoPath, submodulePath, QStringLiteral("could not open repository"));
        co_return;
    }
    AsyncRepo handle = std::move(*opened);
    auto      result = co_await op(handle, rel);
    if (!self)
        co_return;
    if (!submodulePath.isEmpty())
        m_repoModel->setSubmoduleBusy(submodulePath, false);

    if (!result)
    {
        emit submoduleOpFailed(repoPath, submodulePath, QString::fromStdString(result.error().message));
        co_return;
    }
    co_await refreshSubmodules(repoPath);
}
```

  > `setSubmoduleBusy` is matched by the absolute `submodulePath`; the
  > subsequent `applySubmodules` rebuilds that node, so the flag naturally
  > clears on the rebuilt row even though we also clear it explicitly.

  > If template member + `QCoro::Task` causes MOC/linkage friction, fall back to
  > a `std::function<QCoro::Task<gittide::Expected<void>>(AsyncRepo&, std::filesystem::path)>`
  > parameter instead of a template.

- [x] **Step 5: Run the test, verify it passes.**
  `ctest --test-dir build -R ui --output-on-failure` → `TestProjectController`
  passes.

- [x] **Step 6: Commit.**

```bash
git add ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): ProjectController submodule init/update/deinit ops + tree refresh"
```

---

## Task 5: QML — inline init button, submodule menu, repo "Update all"

**Files:**
- Create: `ui/qml/SubmoduleContextMenu.qml`
- Modify: `ui/qml/RepoContextMenu.qml`
- Modify: `ui/qml/Sidebar.qml`
- Modify: `ui/qml/qml.qrc` — add `<file>SubmoduleContextMenu.qml</file>` next to
  the existing `RepoContextMenu.qml` entry (line ~33), else the new component
  won't be in the resource bundle and the app can't load it.

**Interfaces:**
- Consumes: `projectController.initSubmodule/updateAllSubmodules/deinitSubmodule`,
  model roles `ownerRepoPath`, `repoPath`, `submoduleBusy`, `status`,
  `isSubmodule`.

- [x] **Step 1: New `SubmoduleContextMenu.qml`** — mirror `RepoContextMenu.qml`'s
  structure (`AppMenu` + `AppMenuItem`). Items adapt to status (`status`: 0 Clean,
  1 Dirty, 2 Uninitialized):

```qml
import QtQuick
import QtQuick.Controls.Basic

AppMenu {
    id: menu
    objectName: "submoduleContextMenu"

    property string ownerRepoPath: ""
    property string submodulePath: ""
    property int    status: 0
    readonly property bool initialised: status !== 2

    signal initRequested()
    signal updateAllRequested()
    signal deinitRequested()

    AppMenuItem {
        text: menu.initialised ? "Update submodule" : "Initialize submodule"
        onTriggered: menu.initRequested()
    }
    AppMenuItem {
        text: "Update all submodules"
        enabled: menu.initialised
        onTriggered: menu.updateAllRequested()
    }
    AppMenuSeparator {}
    AppMenuItem {
        text: "Deinitialize submodule"
        destructive: true
        enabled: menu.initialised
        onTriggered: menu.deinitRequested()
    }
}
```

- [x] **Step 2: Add "Update all submodules" to `RepoContextMenu.qml`** (top-level
  repos). Add a signal and item before the separator:

```qml
signal updateAllSubmodules()
// …
AppMenuItem {
    text: "Update all submodules"
    onTriggered: menu.updateAllSubmodules()
}
AppMenuSeparator {}
```

- [x] **Step 3: Wire the repo menu in `Sidebar.qml`** where `repoContextMenu` is
  instantiated (~456–461): add
  `onUpdateAllSubmodules: projectController.updateAllSubmodules(repoContextMenu.repoPath)`.

- [x] **Step 4: Add the inline Init button** in the row `contentItem` (after the
  status dot, ~line 309), visible only on greyed/uninit rows:

```qml
// Submodule: inline initialise affordance (uninitialised only).
ToolButton {
    visible: row.uninit && !model.submoduleBusy
    text: "Init"
    font.pixelSize: 10
    onClicked: projectController.initSubmodule(model.ownerRepoPath, model.repoPath)
}
// Spinner while an op runs on this row.
BusyIndicator {
    running: model.submoduleBusy === true
    visible: running
    implicitWidth: 14
    implicitHeight: 14
}
```

  > `model.repoPath` is the submodule node's own (absolute) path;
  > `model.ownerRepoPath` is its top-level repo — exactly the two args
  > `initSubmodule` expects.

- [x] **Step 5: Make submodule rows show the submodule menu.** Change the
  right-click `TapHandler` (~400–408) so submodule rows open the new menu instead
  of bailing:

```qml
TapHandler {
    acceptedButtons: Qt.RightButton
    onTapped: {
        if (row.isSub) {
            submoduleContextMenu.ownerRepoPath = model.ownerRepoPath
            submoduleContextMenu.submodulePath = model.repoPath
            submoduleContextMenu.status        = model.status
            submoduleContextMenu.popup()
        } else {
            repoContextMenu.repoPath = model.repoPath
            repoContextMenu.popup()
        }
    }
}
```

  Instantiate the menu near `repoContextMenu` (~456):

```qml
SubmoduleContextMenu {
    id: submoduleContextMenu
    onInitRequested:      projectController.initSubmodule(ownerRepoPath, submodulePath)
    onUpdateAllRequested: projectController.updateAllSubmodules(submodulePath)
    onDeinitRequested:    projectController.deinitSubmodule(ownerRepoPath, submodulePath)
}
```

  > `updateAllRequested` passes `submodulePath` (the node's own path) as the repo
  > handle — that is the "drill one level deeper" behaviour from the design.

- [x] **Step 6: Route the failure signal to the error overlay.** Find where
  existing controller error signals (e.g. `repoAddFailed`) connect to the
  error/toast overlay in `Main.qml` and connect `submoduleOpFailed` the same way
  (show `message`). Match the existing pattern; do not invent a new overlay.

- [x] **Step 7: Build + manual verify.** `cmake --build build --parallel`, run the
  app on a repo with an uninitialised submodule:
  - greyed row shows **Init**; click → row populates (oid + green dot), spinner
    shows during the op;
  - right-click a submodule → Initialize/Update / Update all / Deinitialize;
  - right-click a top-level repo → **Update all submodules** works;
  - Deinitialize greys the row back.
  No Qt runtime warnings in the console (a warning is a bug).

- [x] **Step 8: Commit.**

```bash
git add ui/qml/SubmoduleContextMenu.qml ui/qml/RepoContextMenu.qml ui/qml/Sidebar.qml ui/qml/Main.qml ui/qml/qml.qrc
git commit -m "feat(ui): submodule init/update/deinit affordances in the sidebar"
```

---

## Task 6: External-change refresh (active watch + fleet poll)

**Files:**
- Modify: `ui/src/projectcontroller.cpp` (`pollRepos`)
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp`
  (surface a structural-change signal)
- Modify: `ui/include/gittide/ui/repocontroller.hpp` + `ui/src/repocontroller.cpp`
  (emit on git-dir refresh)
- Modify: `ui/qml/Main.qml` (connect the signal to `refreshSubmodules`)
- Test: add a slot to `TestProjectController` in
  `tests/ui/test_project_controller.cpp`

**Interfaces:**
- Produces: `RepoController` emits `gitDirRefreshed()` at the end of
  `onWatchGitDir`; `RepoViewModel` re-emits it as `repoStructureChanged()`.
- Consumes: `ProjectController::refreshSubmodules` (Task 4).

- [x] **Step 1: Failing test — poll refreshes a repo's subtree on external
  change.** Add a slot to `TestProjectController`. Activate a project with a
  parent repo whose submodule is initialised; then deinit the submodule *on disk*
  (simulating an external change); start the poll with a fast interval and
  `setWindowActive(true)`; assert the row flips to `Uninitialized`. `pollRepos` is
  private, so drive it via the public timer — construct the controller with a
  short interval and use `QTRY_VERIFY_WITH_TIMEOUT` (the idiom `fetchAll`'s test
  already uses):

```cpp
    void poll_refreshes_submodule_subtree_on_external_change()
    {
        using gittide::SubmoduleStatus;
        using gittide::ui::RepoListModel;

        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("seed child");
        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("seed parent");
        parent.addSubmodule("sub", child.path());     // initialised
        parent.commitAll("add submodule");

        const QString repoPath = QString::fromStdString(parent.path().generic_string());
        ProjectStore store;
        store.projects().push_back(
            Project{.id = "p", .name = "P", .repos = {RepoRef{.path = repoPath.toStdString()}}});

        ProjectController controller(&store, {}, nullptr, /*pollIntervalMs=*/50);
        controller.activate(QStringLiteral("p"));
        RepoListModel* model = controller.repos();
        const QModelIndex sub = model->index(0, 0, model->index(0, 0));
        QCOMPARE(model->data(sub, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Clean));

        // External change: deinit on disk behind the GUI's back.
        {
            auto repo = gittide::GitRepo::open(parent.path());
            QVERIFY(repo && repo->deinitSubmodule("sub"));
        }

        controller.setWindowActive(true);   // starts the poll
        QTRY_VERIFY_WITH_TIMEOUT(
            model->data(model->index(0, 0, model->index(0, 0)),
                        RepoListModel::StatusRole).toInt()
                == static_cast<int>(SubmoduleStatus::Uninitialized),
            5000);
    }
```

- [x] **Step 2: Extend `pollRepos`** — inside the existing per-repo loop, after
  the `setSyncCounts` call, refresh that repo's submodules. Reuse the handle
  already opened in the loop rather than reopening:

```cpp
// pollRepos(), inside the for-loop, after setSyncCounts:
auto tree = co_await repo.submoduleTree();
if (!self)
    co_return;
if (tree)
    m_repoModel->applySubmodules(QString::fromStdString(repos[row].path), *tree);
```

  > `applySubmodules` no-ops when nothing changed, so this adds no UI churn — only
  > one extra `submoduleTree()` per repo per 5 s poll. (Deeper optimisation —
  > skipping the scan when mtimes are unchanged — is out of scope; note it.)

- [x] **Step 3: Emit a structural-change signal from `RepoController`.** Declare
  `void gitDirRefreshed();` and emit it at the end of `onWatchGitDir` (after
  `rearmWatch`):

```cpp
QCoro::Task<void> RepoController::onWatchGitDir()
{
    QPointer<RepoController> self = this;
    WatchMute                mute(m_watcher);
    co_await refreshAll();
    if (!self)
        co_return;
    co_await rearmWatch();
    if (self)
        emit gitDirRefreshed();
}
```

- [x] **Step 4: Re-expose via `RepoViewModel`.** Add signal
  `void repoStructureChanged();`, and in the place where `RepoViewModel` connects
  the controller's other signals, forward it:
  `connect(m_controller, &RepoController::gitDirRefreshed, this, &RepoViewModel::repoStructureChanged);`

- [x] **Step 5: Connect in `Main.qml`.** Near the other `repoVm` connections:

```qml
Connections {
    target: repoVm
    function onRepoStructureChanged() {
        if (repoVm.repoOpen)
            projectController.refreshSubmodules(repoVm.repoPath)
    }
}
```

  This catches a terminal `git submodule update` on the **active** repo
  (`.git/modules/…` write → git-dir watch → `gitDirRefreshed`). Non-active repos
  are covered by the Step 2 poll.

- [x] **Step 6: Run the test, verify it passes.** Build, run the extended test →
  PASS. Manually: open repo A, in a terminal run `git submodule update --init`
  inside it → the sidebar subtree updates within the debounce window.

- [x] **Step 7: Commit.**

```bash
git add ui/src/projectcontroller.cpp ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp ui/qml/Main.qml tests/ui/test_project_controller.cpp
git commit -m "feat(ui): refresh submodule tree on external change (git-dir watch + fleet poll)"
```

---

## Task 7: Spec + docs close-out

**Files:**
- Modify: `docs/spec/product/product.md` (§Submodules)
- Modify: `docs/spec/product/context-menus.md` (remove the deferred row; document
  the submodule menu)
- Modify: `docs/plans/index.md` (add the Plan 24 row)
- Modify: this plan (tick boxes, fill **Outcome**, flip **Status** → `done`)

- [x] **Step 1: Update `product.md` §Submodules.** Replace the deferred-actions
  sentence ("Checkout actions on the parent's submodule pointer remain deferred.")
  with the shipped behaviour: submodules can be **initialised, updated (to the
  pinned commit), and deinitialised** from the sidebar — via an inline *Init*
  button on uninitialised rows, a per-submodule right-click menu, and *Update all
  submodules* on a repo (one level; drill deeper per node). The tree refreshes on
  GUI ops and on external change. Note the still-deferred items (`--remote`
  advance, add/remove submodule, `git submodule sync`).

- [x] **Step 2: Update `context-menus.md`.** Remove the "Submodule context menu"
  row from §6 (Scope exclusions) and add the submodule menu + repo *Update all
  submodules* to the menus table.

- [x] **Step 3: Add the Plan 24 row** to `docs/plans/index.md` (status `done`,
  realises `product · engineering · core`).

- [x] **Step 4: Mark the design doc shipped.** In
  `spec/product/2026-06-26-submodule-init-update-design.md`, flip the Status line
  to `shipped`.

- [x] **Step 5: Full suite green.**
  `ctest --test-dir build --output-on-failure` — all pass, no warnings.

- [x] **Step 6: Fill the Outcome below, tick all boxes, set Status `done`. Commit.**

```bash
git add docs/
git commit -m "docs(plan24): close DoD — submodule init/update shipped; spec + index updated"
```

---

## Outcome

- **Shipped:** GUI submodule init / update / deinit for any repo in the sidebar.
  Inline *Init* button on uninitialised (greyed) rows; per-submodule right-click
  menu (Initialize/Update · Update all · Deinitialize); *Update all submodules*
  on top-level repo rows. "Update all" is one level — drill deeper by invoking it
  on an initialised submodule node. The sidebar tree refreshes on every GUI op and
  on external change (active-repo git-dir watch + 5-second fleet poll, no-opping
  via `applySubmodules` when nothing has changed).
- **Spec updated:**
  - `spec/product/product.md` §Submodules — replaces the deferred sentence with
    the full shipped behaviour and still-deferred list.
  - `spec/product/context-menus.md` §4.4 — adds *Update all submodules* to
    `RepoContextMenu`; new §4.5 documents `SubmoduleContextMenu`; removes the
    "Submodule context menu" deferred row from §6.
  - `spec/product/2026-06-26-submodule-init-update-design.md` — Status flipped
    to `shipped`.
- **Code:**
  - `core/include/gittide/gitrepo.hpp` + `core/src/gitrepo.cpp` —
    `GitRepo::updateSubmodules()` (one-level bulk init/update).
  - `ui/include/gittide/ui/asyncrepo.hpp` + `ui/src/asyncrepo.cpp` —
    `AsyncRepo::updateSubmodules()` + `AsyncRepo::submoduleTree()` wrappers.
  - `ui/include/gittide/ui/repolistmodel.hpp` + `ui/src/repolistmodel.cpp` —
    `RepoListModel::applySubmodules()` diffing refresh, `setSubmoduleBusy()`,
    `BusyRole`, `OwnerRepoPathRole`.
  - `ui/include/gittide/ui/projectcontroller.hpp` + `ui/src/projectcontroller.cpp`
    — `initSubmodule`, `updateAllSubmodules`, `deinitSubmodule`,
    `refreshSubmodules` coroutine slots; `submoduleOpFailed` signal;
    `runSubmoduleOp` private template helper.
  - `ui/include/gittide/ui/repocontroller.hpp` + `ui/src/repocontroller.cpp` —
    `gitDirRefreshed` signal emitted after active-repo git-dir watch fires.
  - `ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp` —
    `repoStructureChanged` re-exposed from `RepoController::gitDirRefreshed`.
  - `ui/qml/SubmoduleContextMenu.qml` — new per-submodule context menu.
  - `ui/qml/Sidebar.qml` — inline Init button, busy spinner, submodule right-click
    wiring, `RepoContextMenu` extended with *Update all submodules*.
  - `ui/qml/RepoContextMenu.qml` — `updateAllSubmodules` signal + menu item.
  - `ui/qml/Main.qml` — `repoStructureChanged → refreshSubmodules` connection;
    `submoduleOpFailed` routed to error overlay.
