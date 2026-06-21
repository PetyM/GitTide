# QML Plan 5 — Submodule Tree in Sidebar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render git submodules recursively (arbitrary depth) in the QML sidebar repo tree, each with a pinned short OID and a clean/dirty/uninitialized status dot, per migration-design §3.3.

**Architecture:** Core gains a recursive `GitRepo::submoduleTree()` returning per-node OID + status (the git logic that currently leaks into the UI model moves back into `core/`). `RepoListModel` becomes an arbitrary-depth tree mapping that output. `Sidebar.qml`'s delegate branches on `isSubmodule` to draw the glyph, OID, status dot, guide rail, elbow, and inter-repo divider from the mockup.

**Tech Stack:** C++23, libgit2 (PRIVATE to core), Qt 6 Quick/Qml, Catch2 (core tests), Qt Test (UI tests), CMake ≥ 3.28.

**Design spec:** [`docs/superpowers/specs/2026-06-19-qml-submodule-tree-design.md`](../specs/2026-06-19-qml-submodule-tree-design.md)
**Visual reference:** `docs/superpowers/specs/mockups/gittide-qml-mainwindow.html` (`.repo`, `.sub`, `.subtree`, `.reposep`).

## Global Constraints

- **No Qt in `core/`** — Tasks touching `core/` add no Qt includes.
- **libgit2 and nlohmann/json stay PRIVATE to `core/`** — no public header includes `git2.h`; `submodule.hpp` is pure `std`.
- **Core speaks `std`** — `std::string` (UTF-8), `std::filesystem::path`, `Expected<T> = std::expected<T, GitError>`; no exceptions across layers.
- **One owner per `GitRepo`** — move-only, not thread-safe; recursion opens child repos sequentially.
- **Paths via `generic_u8string()` / `toGitPath()` / `fromGitPath()`, never `.string()`.** Never build git command strings; use the libgit2 API.
- **Colour comes from a theme token, never a hex literal in a component** — QML reads `theme.<token>`; the only literals allowed are the 0.7 / 0.55 alpha multipliers.
- **TDD** — write the failing test first. New `core/` sources → `core/CMakeLists.txt` + the relevant `tests/CMakeLists.txt` list. New `ui/` tests → `gittide_ui_test_sources` in `tests/CMakeLists.txt` **and** a `#include` + `QTest::qExec` block in `tests/ui/main.cpp` (miss either and it silently runs zero tests).
- **Code style:** Allman braces, `m_` members, lowercase file names, `.clang-format`. Split a rename from content changes into two commits.
- **Commands:** configure `cmake -S . -B build`; build `cmake --build build --parallel`; core tests `ctest --test-dir build --output-on-failure -R gittide_core_tests` (or a Catch tag filter); UI tests `ctest --test-dir build --output-on-failure -R gittide_ui_tests` (headless via `QT_QPA_PLATFORM=offscreen`, already wired).

---

## File Structure

**New files**
- `core/include/gittide/submodule.hpp` — `SubmoduleStatus` enum + `SubmoduleNode` struct (pure `std`, no Qt/git2).
- `tests/test_git_repo_submodules.cpp` — Catch2 tests for `submoduleTree()`.

**Modified files**
- `tests/support/temprepo.hpp` / `.cpp` — add `addSubmodule()` + `updateSubmodulesRecursive()` helpers.
- `core/include/gittide/gitrepo.hpp` — `#include "gittide/submodule.hpp"`; declare `submoduleTree()`; remove `submodules()` (Task 3).
- `core/src/gitrepo.cpp` — implement `submoduleTree()`; remove `submodules()` (Task 3).
- `ui/include/gittide/ui/repolistmodel.hpp` — recursive `Node`; new roles.
- `ui/src/repolistmodel.cpp` — arbitrary-depth `index/parent/rowCount/data`; map `submoduleTree()`.
- `ui/qml/Sidebar.qml` — delegate branches on `isSubmodule`; glyph/OID/dot/guide-rail/elbow/divider; expand-by-default.
- `tests/test_temp_repo.cpp` — assert the new helpers build a submodule.
- `tests/ui/test_repo_list_model.cpp` — recursive rows + new roles.
- `tests/ui/test_qml_shell.cpp` — shell loads with a submodule-bearing model.
- `tests/CMakeLists.txt` — register `test_git_repo_submodules.cpp`.
- Living spec: `docs/spec/design/design.md`, `docs/spec/product/product.md` (Task 6).

---

### Task 1: `TempRepo` submodule test infrastructure

First submodule test infra in the repo. Adds two helpers so later tasks can build nested submodule fixtures.

**Files:**
- Modify: `tests/support/temprepo.hpp`
- Modify: `tests/support/temprepo.cpp`
- Test: `tests/test_temp_repo.cpp`

**Interfaces:**
- Produces:
  - `void TempRepo::addSubmodule(std::string_view name, const std::filesystem::path& childRepoPath)` — adds `childRepoPath` as a submodule at relative path `name`, cloning it into the working tree (initialised). Does **not** commit; caller commits via `commitAll`.
  - `void TempRepo::updateSubmodulesRecursive()` — clones+checks out every not-yet-initialised submodule in this repo and, depth-first, in each checked-out submodule (materialises nested submodules that a non-recursive clone left uninitialised).

- [ ] **Step 1: Write the failing test**

Add to `tests/test_temp_repo.cpp` (append a new `TEST_CASE`; keep existing includes — add `#include <git2.h>` and `#include "gittide/pathutil.hpp"` if absent):

```cpp
TEST_CASE("TempRepo::addSubmodule registers and clones a child repository", "[temprepo]")
{
    gittide::test::TempRepo child;
    child.writeFile("readme.md", "child\n");
    child.commitAll("child init");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "parent\n");
    parent.commitAll("parent init");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add libchild submodule");

    // .gitmodules exists and the submodule working tree was checked out.
    REQUIRE(std::filesystem::exists(parent.path() / ".gitmodules"));
    REQUIRE(std::filesystem::exists(parent.path() / "libchild" / "readme.md"));

    // libgit2 sees exactly one submodule named "libchild".
    git_repository* raw = nullptr;
    REQUIRE(git_repository_open(&raw, gittide::toGitPath(parent.path()).c_str()) == 0);
    git_submodule* sm = nullptr;
    REQUIRE(git_submodule_lookup(&sm, raw, "libchild") == 0);
    git_submodule_free(sm);
    git_repository_free(raw);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel 2>&1 | head -30`
Expected: FAIL — compile error, `addSubmodule` is not a member of `TempRepo`.

- [ ] **Step 3: Implement the helpers**

In `tests/support/temprepo.hpp`, after `setIdentity(...)`:

```cpp
    // Add childRepoPath as a submodule at repo-relative path `name`, cloning it
    // into the working tree (initialised). Does not commit; caller commits.
    void addSubmodule(std::string_view name, const std::filesystem::path& childRepoPath);

    // Clone+checkout every uninitialised submodule, depth-first, so nested
    // submodules a non-recursive clone left bare become real working trees.
    void updateSubmodulesRecursive();
```

In `tests/support/temprepo.cpp`, add after `setIdentity(...)` (the file already includes `<git2.h>` and `"gittide/pathutil.hpp"`):

```cpp
void TempRepo::addSubmodule(std::string_view name, const std::filesystem::path& childRepoPath)
{
    // libgit2 clones local paths via a file:// URL.
    const std::string url     = "file://" + childRepoPath.generic_string();
    const std::string subName = std::string(name);

    git_submodule* sm = nullptr;
    check(git_submodule_add_setup(&sm, m_repo, url.c_str(), subName.c_str(), /*use_gitlink=*/1),
          "git_submodule_add_setup failed");

    git_repository* subRepo = nullptr;
    check(git_submodule_clone(&subRepo, sm, nullptr), "git_submodule_clone failed");
    git_repository_free(subRepo);

    check(git_submodule_add_finalize(sm), "git_submodule_add_finalize failed");
    git_submodule_free(sm);
}

namespace {
// Clone+checkout uninitialised submodules of `repo`, then recurse into each.
void updateRecursive(git_repository* repo)
{
    struct Payload
    {
        std::vector<std::string> names;
    } payload;

    git_submodule_foreach(
        repo,
        [](git_submodule* /*sm*/, const char* name, void* pl) -> int
        {
            static_cast<Payload*>(pl)->names.emplace_back(name);
            return 0;
        },
        &payload);

    for (const auto& n : payload.names)
    {
        git_submodule* sm = nullptr;
        if (git_submodule_lookup(&sm, repo, n.c_str()) != 0)
            continue;
        git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
        git_submodule_update(sm, /*init=*/1, &opts); // best-effort
        git_repository* sub = nullptr;
        if (git_submodule_open(&sub, sm) == 0)
        {
            updateRecursive(sub);
            git_repository_free(sub);
        }
        git_submodule_free(sm);
    }
}
} // namespace

void TempRepo::updateSubmodulesRecursive()
{
    updateRecursive(m_repo);
}
```

Add `#include <vector>` and `#include <string>` to `temprepo.cpp` if not already present.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R gittide_core_tests 2>&1 | tail -20`
Expected: PASS, including "TempRepo::addSubmodule registers and clones a child repository".

- [ ] **Step 5: Commit**

```bash
git add tests/support/temprepo.hpp tests/support/temprepo.cpp tests/test_temp_repo.cpp
git commit -m "test(core): TempRepo submodule helpers (add + recursive update)"
```

---

### Task 2: Core — `submoduleTree()` with OID + status

**Files:**
- Create: `core/include/gittide/submodule.hpp`
- Modify: `core/include/gittide/gitrepo.hpp`
- Modify: `core/src/gitrepo.cpp`
- Create + register: `tests/test_git_repo_submodules.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TempRepo::addSubmodule`, `TempRepo::updateSubmodulesRecursive` (Task 1).
- Produces:
  - `gittide::SubmoduleStatus` (`Clean` / `Dirty` / `Uninitialized`).
  - `gittide::SubmoduleNode { std::filesystem::path path; std::string name; std::string shortOid; SubmoduleStatus status; std::vector<SubmoduleNode> children; }`.
  - `Expected<std::vector<SubmoduleNode>> GitRepo::submoduleTree() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_git_repo_submodules.cpp`:

```cpp
#include "gittide/gitrepo.hpp"
#include "gittide/submodule.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using gittide::GitRepo;
using gittide::SubmoduleStatus;

TEST_CASE("SubmoduleNode defaults are empty/clean", "[submodules]")
{
    gittide::SubmoduleNode n{};
    REQUIRE(n.name.empty());
    REQUIRE(n.shortOid.empty());
    REQUIRE(n.children.empty());
    REQUIRE(n.status == SubmoduleStatus::Clean);
}

TEST_CASE("submoduleTree reports a clean direct submodule with a 7-char pinned oid", "[submodules]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "x\n");
    child.commitAll("child");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add submodule");

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());
    auto tree = repo->submoduleTree();
    REQUIRE(tree.has_value());
    REQUIRE(tree->size() == 1);

    const auto& sub = (*tree)[0];
    REQUIRE(sub.name == "libchild");
    REQUIRE(sub.path == parent.path() / "libchild");
    REQUIRE(sub.shortOid.size() == 7);
    REQUIRE(sub.status == SubmoduleStatus::Clean);
    REQUIRE(sub.children.empty());
}

TEST_CASE("submoduleTree flags a working-tree edit as dirty", "[submodules]")
{
    gittide::test::TempRepo child;
    child.writeFile("a.txt", "x\n");
    child.commitAll("child");

    gittide::test::TempRepo parent;
    parent.writeFile("top.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add submodule");

    // Modify the submodule's checked-out working tree.
    std::ofstream(parent.path() / "libchild" / "a.txt", std::ios::binary) << "x\nmore\n";

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());
    auto tree = repo->submoduleTree();
    REQUIRE(tree.has_value());
    REQUIRE(tree->size() == 1);
    REQUIRE((*tree)[0].status == SubmoduleStatus::Dirty);
}

TEST_CASE("submoduleTree recurses and reports an uninitialised nested submodule", "[submodules]")
{
    // grandchild -> child(has grandchild submodule) -> parent(has child submodule)
    gittide::test::TempRepo grand;
    grand.writeFile("g.txt", "g\n");
    grand.commitAll("grand");

    gittide::test::TempRepo child;
    child.writeFile("c.txt", "c\n");
    child.commitAll("child");
    child.addSubmodule("libgrand", grand.path());
    child.commitAll("child adds grand");

    gittide::test::TempRepo parent;
    parent.writeFile("p.txt", "p\n");
    parent.commitAll("parent");
    parent.addSubmodule("libchild", child.path()); // non-recursive: grand left bare
    parent.commitAll("parent adds child");

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());
    auto tree = repo->submoduleTree();
    REQUIRE(tree.has_value());
    REQUIRE(tree->size() == 1);

    const auto& lvl1 = (*tree)[0];
    REQUIRE(lvl1.name == "libchild");
    REQUIRE(lvl1.status == SubmoduleStatus::Clean);
    REQUIRE(lvl1.children.size() == 1);

    const auto& lvl2 = lvl1.children[0];
    REQUIRE(lvl2.name == "libgrand");
    REQUIRE(lvl2.status == SubmoduleStatus::Uninitialized);
    REQUIRE(lvl2.shortOid.empty());
    REQUIRE(lvl2.children.empty());
}
```

- [ ] **Step 2: Register the test + run to verify it fails**

In `tests/CMakeLists.txt`, add to the `add_executable(gittide_core_tests …)` source list (after `test_git_repo_reset.cpp`):

```cmake
  test_git_repo_submodules.cpp
```

Run: `cmake -S . -B build && cmake --build build --parallel 2>&1 | tail -25`
Expected: FAIL — `gittide/submodule.hpp` not found / `submoduleTree` not a member of `GitRepo`.

- [ ] **Step 3: Create the type header**

Create `core/include/gittide/submodule.hpp`:

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

// Working state of a submodule relative to what the superproject pins.
enum class SubmoduleStatus
{
    Clean,         // initialised; working tree matches the pinned commit, no local changes
    Dirty,         // initialised; working-tree change, or checked-out commit != pinned
    Uninitialized, // listed in .gitmodules but not checked out (no working dir)
};

// One node in a repository's recursive submodule tree.
struct SubmoduleNode
{
    std::filesystem::path      path;                          // absolute working-dir path
    std::string                name;                          // .gitmodules name (UTF-8)
    std::string                shortOid;                      // pinned gitlink commit, 7 hex; "" if Uninitialized
    SubmoduleStatus            status = SubmoduleStatus::Clean;
    std::vector<SubmoduleNode> children;                      // recursive; empty if Uninitialized or leaf
};

} // namespace gittide
```

- [ ] **Step 4: Declare `submoduleTree()` in the public header**

In `core/include/gittide/gitrepo.hpp`, add the include near the other gittide includes (after `#include "gittide/graph.hpp"`):

```cpp
#include "gittide/submodule.hpp"
```

Then, directly below the existing `submodules()` declaration (keep `submodules()` for now — Task 3 removes it), add:

```cpp
    // Recursively enumerates submodules (depth-first), opening each initialised
    // submodule as its own repository to descend. Each node carries the pinned
    // short OID and a clean/dirty/uninitialised status; uninitialised nodes have
    // no children. See SubmoduleStatus / SubmoduleNode.
    Expected<std::vector<SubmoduleNode>> submoduleTree() const;
```

- [ ] **Step 5: Implement `submoduleTree()`**

In `core/src/gitrepo.cpp`, add after the existing `submodules()` definition (the file already includes `<git2.h>`, `"gittide/pathutil.hpp"`, and has `lastGitError` + `fromGitPath` in scope):

```cpp
namespace {
// Bit set indicating the submodule's index/working tree differs from its pin.
constexpr unsigned kSubmoduleDirtyMask =
    GIT_SUBMODULE_STATUS_INDEX_ADDED | GIT_SUBMODULE_STATUS_INDEX_DELETED |
    GIT_SUBMODULE_STATUS_INDEX_MODIFIED | GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED |
    GIT_SUBMODULE_STATUS_WD_WD_MODIFIED | GIT_SUBMODULE_STATUS_WD_MODIFIED |
    GIT_SUBMODULE_STATUS_WD_UNTRACKED | GIT_SUBMODULE_STATUS_WD_DELETED;

SubmoduleStatus classifySubmodule(unsigned flags)
{
    if (!(flags & GIT_SUBMODULE_STATUS_IN_WD))
        return SubmoduleStatus::Uninitialized;
    if (flags & kSubmoduleDirtyMask)
        return SubmoduleStatus::Dirty;
    return SubmoduleStatus::Clean;
}
} // namespace

Expected<std::vector<SubmoduleNode>> GitRepo::submoduleTree() const
{
    const std::filesystem::path wd = workdir();

    // Collect direct submodules (path/name/oid/status) inside the foreach, then
    // descend afterwards — opening child repositories inside the callback is
    // unsafe while libgit2 holds the submodule cache.
    struct Payload
    {
        std::vector<SubmoduleNode>* out;
        const std::filesystem::path* wd;
        git_repository* repo;
    };
    std::vector<SubmoduleNode> result;
    Payload payload{&result, &wd, m_repo};

    auto cb = [](git_submodule* sm, const char* name, void* pl) -> int
    {
        auto* p = static_cast<Payload*>(pl);

        SubmoduleNode node;
        node.name = name ? name : "";
        if (const char* rel = git_submodule_path(sm))
            node.path = *p->wd / fromGitPath(rel);

        unsigned flags = 0;
        if (git_submodule_status(&flags, p->repo, node.name.c_str(), GIT_SUBMODULE_IGNORE_UNSPECIFIED) == 0)
            node.status = classifySubmodule(flags);

        if (node.status != SubmoduleStatus::Uninitialized)
        {
            if (const git_oid* hid = git_submodule_head_id(sm))
            {
                char hex[GIT_OID_HEXSZ + 1];
                git_oid_tostr(hex, sizeof(hex), hid);
                node.shortOid.assign(hex, hex + 7);
            }
        }

        p->out->push_back(std::move(node));
        return 0;
    };

    const int rc = git_submodule_foreach(m_repo, cb, &payload);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    // Descend into each initialised submodule.
    for (auto& node : result)
    {
        if (node.status == SubmoduleStatus::Uninitialized)
            continue;
        auto child = GitRepo::open(node.path);
        if (!child)
            continue; // a broken child degrades to no children, not a fatal error
        if (auto sub = child->submoduleTree())
            node.children = std::move(*sub);
    }

    return result;
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests 2>&1 | tail -25`
Expected: PASS — all four `[submodules]` cases green; whole core suite green.

- [ ] **Step 7: Commit**

```bash
git add core/include/gittide/submodule.hpp core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_submodules.cpp tests/CMakeLists.txt
git commit -m "feat(core): submoduleTree() — recursive submodules with pinned oid + status"
```

---

### Task 3: `RepoListModel` — arbitrary-depth tree from `submoduleTree()`

Replaces the depth-1 `Row`/`SubRow` shape with a recursive `Node`, routes the model through `submoduleTree()`, and **removes** the now-unused flat `submodules()` from core.

**Files:**
- Modify: `ui/include/gittide/ui/repolistmodel.hpp`
- Modify: `ui/src/repolistmodel.cpp`
- Modify: `core/include/gittide/gitrepo.hpp` (remove `submodules()` decl)
- Modify: `core/src/gitrepo.cpp` (remove `submodules()` def)
- Test: `tests/ui/test_repo_list_model.cpp`

**Interfaces:**
- Consumes: `GitRepo::submoduleTree()`, `gittide::SubmoduleNode`, `gittide::SubmoduleStatus` (Task 2).
- Produces roles (`RepoListModel::Roles`): existing `PathRole` (`repoPath`), `MissingRole` (`missing`); **new** `IsSubmoduleRole` (`isSubmodule`, bool), `ShortOidRole` (`shortOid`, string), `StatusRole` (`status`, int — `0` Clean / `1` Dirty / `2` Uninitialized).

- [ ] **Step 1: Write the failing test**

Append to `tests/ui/test_repo_list_model.cpp` (inside the `private slots:` block). It builds a real parent→child submodule on disk via `TempRepo`, so add includes at the top of the file: `#include "support/temprepo.hpp"` and `#include "gittide/submodule.hpp"`.

```cpp
    void submodule_rows_expose_recursive_children_and_new_roles()
    {
        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("child");

        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("parent");
        parent.addSubmodule("libchild", child.path());
        parent.commitAll("add submodule");

        std::vector<RepoRef> repos{
            RepoRef{.path = parent.path().generic_string(), .alias = "parent"},
        };

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos(repos);

        QCOMPARE(model.rowCount(), 1);
        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.data(top, RepoListModel::IsSubmoduleRole).toBool(), false);
        QCOMPARE(model.rowCount(top), 1); // one submodule child

        const QModelIndex sub = model.index(0, 0, top);
        QVERIFY(sub.isValid());
        QCOMPARE(model.parent(sub), top);
        QCOMPARE(model.data(sub, RepoListModel::IsSubmoduleRole).toBool(), true);
        QCOMPARE(model.data(sub, Qt::DisplayRole).toString(), QStringLiteral("libchild"));
        QCOMPARE(model.data(sub, RepoListModel::ShortOidRole).toString().size(), 7);
        QCOMPARE(model.data(sub, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(gittide::SubmoduleStatus::Clean));
    }
```

The UI test target does **not** yet compile `support/temprepo.cpp` (it lives only in the `gittide_core_tests` source list). `libgit2package` and the libgit2 include dir are already linked to `gittide_ui_tests`. Add `support/temprepo.cpp` to `gittide_ui_test_sources` in `tests/CMakeLists.txt` (alongside the `${CMAKE_CURRENT_SOURCE_DIR}/ui/…` entries):

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/support/temprepo.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel 2>&1 | tail -25`
Expected: FAIL — `IsSubmoduleRole` / `ShortOidRole` / `StatusRole` are not members of `RepoListModel`.

- [ ] **Step 3: Rewrite the model header**

Replace `ui/include/gittide/ui/repolistmodel.hpp` contents with:

```cpp
#pragma once
#include <QAbstractItemModel>
#include <memory>
#include <vector>

#include "gittide/projectstore.hpp"
#include "gittide/submodule.hpp"

namespace gittide::ui {

class RepoListModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles
    {
        PathRole = Qt::UserRole + 1,
        MissingRole,
        IsSubmoduleRole,
        ShortOidRole,
        StatusRole,
    };

    explicit RepoListModel(QObject* parent = nullptr);

    // QAbstractItemModel overrides
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRepos(const std::vector<gittide::RepoRef>& repos);

private:
    struct Node
    {
        QString                            displayName;
        QString                            path;
        bool                               isSubmodule = false;
        bool                               missing     = false;
        QString                            shortOid;
        gittide::SubmoduleStatus           status = gittide::SubmoduleStatus::Clean;
        Node*                              parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    // Build child Nodes from a submodule subtree, linking parent pointers.
    void appendSubmodules(Node& parent, const std::vector<gittide::SubmoduleNode>& subs);
    // The Node behind an index (nullptr → the invisible root / top-level list).
    Node* nodeFor(const QModelIndex& index) const;
    // Row of `node` within its sibling list.
    int rowOf(const Node* node) const;

    std::vector<std::unique_ptr<Node>> m_roots;
};

} // namespace gittide::ui
```

- [ ] **Step 4: Rewrite the model implementation**

Replace `ui/src/repolistmodel.cpp` contents with:

```cpp
#include "gittide/ui/repolistmodel.hpp"

#include <filesystem>

#include "gittide/gitrepo.hpp"

namespace gittide::ui {

RepoListModel::RepoListModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

void RepoListModel::appendSubmodules(Node& parent, const std::vector<gittide::SubmoduleNode>& subs)
{
    for (const auto& s : subs)
    {
        auto node         = std::make_unique<Node>();
        node->displayName = QString::fromStdString(s.name);
        node->path        = QString::fromStdString(s.path.generic_string());
        node->isSubmodule = true;
        node->missing     = s.status == gittide::SubmoduleStatus::Uninitialized;
        node->shortOid    = QString::fromStdString(s.shortOid);
        node->status      = s.status;
        node->parent      = &parent;
        appendSubmodules(*node, s.children);
        parent.children.push_back(std::move(node));
    }
}

void RepoListModel::setRepos(const std::vector<gittide::RepoRef>& repos)
{
    beginResetModel();
    m_roots.clear();
    for (const auto& r : repos)
    {
        const std::filesystem::path p(r.path);
        std::error_code ec;
        const bool present = std::filesystem::exists(p, ec) && !ec;

        auto root         = std::make_unique<Node>();
        root->displayName = r.alias.empty() ? QString::fromStdString(r.path) : QString::fromStdString(r.alias);
        root->path        = QString::fromStdString(r.path);
        root->isSubmodule = false;
        root->missing     = !present;

        if (present)
        {
            auto repo = gittide::GitRepo::open(p);
            if (repo)
            {
                if (auto tree = repo->submoduleTree())
                    appendSubmodules(*root, *tree);
            }
        }
        m_roots.push_back(std::move(root));
    }
    endResetModel();
}

RepoListModel::Node* RepoListModel::nodeFor(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : nullptr;
}

int RepoListModel::rowOf(const Node* node) const
{
    const auto& siblings = node->parent ? node->parent->children : m_roots;
    for (std::size_t i = 0; i < siblings.size(); ++i)
        if (siblings[i].get() == node)
            return static_cast<int>(i);
    return 0;
}

QModelIndex RepoListModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
        return {};
    const Node* parentNode = nodeFor(parent);
    const auto& siblings   = parentNode ? parentNode->children : m_roots;
    if (row >= static_cast<int>(siblings.size()))
        return {};
    return createIndex(row, 0, siblings[row].get());
}

QModelIndex RepoListModel::parent(const QModelIndex& child) const
{
    const Node* node = nodeFor(child);
    if (!node || !node->parent)
        return {};
    Node* p = node->parent;
    return createIndex(rowOf(p), 0, p);
}

int RepoListModel::rowCount(const QModelIndex& parent) const
{
    const Node* node = nodeFor(parent);
    const auto& siblings = node ? node->children : m_roots;
    return static_cast<int>(siblings.size());
}

int RepoListModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant RepoListModel::data(const QModelIndex& index, int role) const
{
    const Node* node = nodeFor(index);
    if (!node)
        return {};
    switch (role)
    {
    case Qt::DisplayRole:
        return node->displayName;
    case PathRole:
        return node->path;
    case MissingRole:
        return node->missing;
    case IsSubmoduleRole:
        return node->isSubmodule;
    case ShortOidRole:
        return node->shortOid;
    case StatusRole:
        return static_cast<int>(node->status);
    default:
        return {};
    }
}

QHash<int, QByteArray> RepoListModel::roleNames() const
{
    auto roles             = QAbstractItemModel::roleNames();
    roles[PathRole]        = "repoPath";
    roles[MissingRole]     = "missing";
    roles[IsSubmoduleRole] = "isSubmodule";
    roles[ShortOidRole]    = "shortOid";
    roles[StatusRole]      = "status";
    return roles;
}

} // namespace gittide::ui
```

- [ ] **Step 5: Remove the retired `submodules()` from core**

In `core/include/gittide/gitrepo.hpp`, delete the two-line `submodules()` declaration:

```cpp
    // Returns absolute paths of direct submodules (from .gitmodules).
    Expected<std::vector<std::filesystem::path>> submodules() const;
```

In `core/src/gitrepo.cpp`, delete the entire `GitRepo::submodules()` definition (the function body from `Expected<std::vector<std::filesystem::path>> GitRepo::submodules() const` through its closing brace).

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R "gittide_ui_tests|gittide_core_tests" 2>&1 | tail -25`
Expected: PASS — the new `submodule_rows_expose_recursive_children_and_new_roles` slot green, existing `RepoListModel` slots still green, core suite green (no remaining `submodules()` reference).

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/repolistmodel.hpp ui/src/repolistmodel.cpp core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/ui/test_repo_list_model.cpp tests/CMakeLists.txt
git commit -m "feat(ui): RepoListModel arbitrary-depth submodule tree via submoduleTree(); drop core submodules()"
```

---

### Task 4: `Sidebar.qml` — submodule delegate (glyph, OID, status dot, guide rail, divider)

**Files:**
- Modify: `ui/qml/Sidebar.qml`
- Test: `tests/ui/test_qml_shell.cpp`

**Interfaces:**
- Consumes: model roles `repoPath`, `missing`, `isSubmodule`, `shortOid`, `status` (Task 3); `theme` tokens (`accent`, `textSecondary`, `textPrimary`, `textMuted`, `stateModified`, `stateAdded`, `border`, `surfaceBase`).
- Status int meaning: `0` Clean, `1` Dirty, `2` Uninitialized.

- [ ] **Step 1: Write the failing test**

Append to `tests/ui/test_qml_shell.cpp` a slot that loads the shell with a submodule-bearing model and verifies the repo tree exists and shows a submodule child row. First inspect the file's existing helper for installing the QML context + model (`installQmlContext` / how it builds a `RepoListModel`); reuse it. The assertion is intentionally modest (headless QML cannot pixel-check the rail): the engine loads with no errors and the model wired into `repoTree` reports a submodule child.

```cpp
    void shell_loads_with_a_submodule_bearing_repo_model()
    {
        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("child");
        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("parent");
        parent.addSubmodule("libchild", child.path());
        parent.commitAll("add submodule");

        gittide::ui::RepoListModel model;
        model.setRepos({gittide::RepoRef{.path = parent.path().generic_string(), .alias = "parent"}});

        QQmlApplicationEngine engine;
        // … mirror this file's existing context-property wiring (theme, repoModel,
        // repoVm, projectController) using `&model` as repoModel …
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.rowCount(top), 1);
        QCOMPARE(model.data(model.index(0, 0, top), gittide::ui::RepoListModel::IsSubmoduleRole).toBool(), true);
    }
```

Add `#include "support/temprepo.hpp"` to the file if absent. (If `test_qml_shell.cpp` already constructs a `RepoListModel` via a shared helper, route through that helper instead of duplicating wiring.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel 2>&1 | tail -20`
Expected: FAIL — compile error (`addSubmodule` undeclared if include missing) or the new slot not yet present. If the QML still loads green, the test is still valuable as a regression guard for the delegate — proceed; it will pass once Step 3 keeps the tree binding intact.

- [ ] **Step 3: Implement the delegate**

In `ui/qml/Sidebar.qml`, set the `TreeView` to expand submodules by default and replace the `delegate:` block with one that branches on `model.isSubmodule`. Replace the existing `TreeView { … }` (lines ~57–109) with:

```qml
        // ---- Repo tree ----
        TreeView {
            id: repoTree
            objectName: "repoTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            visible: sidebar.topLevelCount > 0
            model: repoModel

            // Expand every node as rows arrive so nested submodules show by default.
            onModelChanged: expandRecursively()
            Connections {
                target: repoModel
                function onModelReset() { repoTree.expandRecursively() }
            }

            delegate: TreeViewDelegate {
                id: row
                implicitHeight: 34
                indentation: 16
                onClicked: if (repoVm && !model.isSubmodule) repoVm.open(model.repoPath)

                readonly property bool isSub: model.isSubmodule === true
                readonly property bool uninit: isSub && model.status === 2

                contentItem: RowLayout {
                    spacing: 8

                    // Glyph: repository (◧) vs submodule (❖, accent @0.7).
                    Label {
                        text: row.isSub ? "❖" : "◧"
                        color: row.isSub ? theme.accent : (row.current ? theme.accent : theme.textSecondary)
                        opacity: row.isSub ? 0.7 : 1.0
                        font.pixelSize: row.isSub ? 14 : 15
                    }

                    Label {
                        text: row.isSub
                              ? model.display
                              : (model.repoPath ? model.repoPath.toString().split("/").pop() : "")
                        color: (model.missing || row.uninit) ? theme.textMuted : theme.textPrimary
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Submodule: pinned short OID (mono) — hidden when uninitialised.
                    Label {
                        visible: row.isSub && !row.uninit
                        text: model.shortOid
                        color: theme.textMuted
                        font.family: "monospace"
                        font.pixelSize: 11
                    }

                    // Submodule: status dot (dirty amber / clean green @0.55).
                    Rectangle {
                        visible: row.isSub && !row.uninit
                        implicitWidth: 7
                        implicitHeight: 7
                        radius: 3.5
                        color: model.status === 1 ? theme.stateModified : theme.stateAdded
                        opacity: model.status === 1 ? 1.0 : 0.55
                    }

                    // Repository: missing-on-disk warning.
                    Label {
                        visible: !row.isSub && model.missing === true
                        text: "⚠"
                        color: theme.stateModified
                    }
                }

                background: Rectangle {
                    color: row.current ? theme.surfaceBase : "transparent"
                    radius: 10
                    // Selection accent border (repos) at x=0.
                    Rectangle {
                        visible: row.current && !row.isSub
                        width: 2
                        height: parent.height
                        color: theme.accent
                    }
                    // Submodule guide rail + elbow connector.
                    Rectangle {
                        visible: row.isSub
                        x: row.depth * row.indentation - 8
                        width: 1
                        height: parent.height
                        color: theme.border
                    }
                    Rectangle {
                        visible: row.isSub
                        x: row.depth * row.indentation - 8
                        y: parent.height / 2
                        width: 7
                        height: 1
                        color: theme.border
                    }
                }

                // Right-click → remove-from-project menu (top-level repos only).
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        if (row.isSub)
                            return
                        repoContextMenu.repoPath = model.repoPath
                        repoContextMenu.popup()
                    }
                }
            }
        }
```

> Note on rail geometry: `TreeViewDelegate` exposes `depth` and `indentation`; the rail/elbow x-offsets above approximate the mockup's per-level guide. Confirm the visual at the live demo and nudge the `- 8` offset if the rail does not sit just left of the glyph (carry-forward minor if cosmetics need a tweak).

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests 2>&1 | tail -20`
Expected: PASS — `shell_loads_with_a_submodule_bearing_repo_model` green; full UI suite green; no QML warnings about missing roles in the test log.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/Sidebar.qml tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): submodule rows in sidebar tree — glyph, pinned oid, status dot, guide rail"
```

---

### Task 5: Live-demo visual verification + carry-forward notes

A non-code checkpoint: the headless tests cannot see the rail/elbow/divider or colours. Verify against the mockup and record any cosmetic deltas.

**Files:**
- Modify (if a divider between top-level repos is missing): `ui/qml/Sidebar.qml`

- [ ] **Step 1: Build and launch the QML app**

Run: `cmake -S . -B build -DGITGUI_BUILD_QML=ON && cmake --build build --parallel && ./build/app/gittide_qml_app` (exact target name per `app/CMakeLists.txt`).

- [ ] **Step 2: Open a repo with nested submodules** (e.g. add this very repo if it has submodules, or a libgit2 checkout). Confirm against `mockups/gittide-qml-mainwindow.html`:
  - submodules show `❖` (accent, ~0.7α), name, mono short OID, status dot (amber dirty / green clean);
  - uninitialised submodules are dimmed with no OID/dot;
  - nesting is expanded by default with a per-level guide rail + elbow;
  - top-level repositories read as separated.

- [ ] **Step 3:** If top-level repos do not read as separated, add a `.reposep`-style divider. In `Sidebar.qml`, inside the `background` `Rectangle` of the delegate, add (visible only for non-submodule rows that are not the first root):

```qml
                    // Divider above each top-level repo after the first.
                    Rectangle {
                        visible: !row.isSub && row.row > 0
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 4
                        height: 1
                        color: theme.border
                        opacity: 0.5
                    }
```

- [ ] **Step 4: Re-run the UI suite** to confirm no regression:

Run: `ctest --test-dir build --output-on-failure -R gittide_ui_tests 2>&1 | tail -5`
Expected: PASS.

- [ ] **Step 5: Commit (only if Step 3 changed code)**

```bash
git add ui/qml/Sidebar.qml
git commit -m "feat(ui): divider between top-level repositories in sidebar tree"
```

---

### Task 6: Update the living spec + plan outcome

**Files:**
- Modify: `docs/spec/design/design.md` (Components — sidebar repo tree)
- Modify: `docs/spec/product/product.md` (sidebar / submodules)
- Modify: this plan file (Outcome section)

- [ ] **Step 1: Update `docs/spec/design/design.md`** — in the Components section covering the sidebar/repo tree, document the QML submodule rendering now shipped: recursive depth, `❖` accent glyph (~0.7α), pinned short OID (mono, `textMuted`), status dot (`stateModified` dirty / `stateAdded` @0.55 clean), uninitialised = dimmed/no-OID, per-level guide rail + elbow, divider between top-level repos. Reference the multi-color exception already noted for the graph; the sidebar dot reuses git-state tokens.

- [ ] **Step 2: Update `docs/spec/product/product.md`** — note that submodules appear in the sidebar tree at arbitrary depth with at-a-glance pinned-commit + clean/dirty/uninitialised state; submodule rows are informational (no open/checkout action yet — deferred).

- [ ] **Step 3: Fill this plan's Outcome section** (below) — files touched, commits, and carry-forward minors (e.g. rail x-offset tuned at demo; QML status int magic numbers 0/1/2 mirror `SubmoduleStatus`; submodule actions deferred; async tree build deferred).

- [ ] **Step 4: Commit**

```bash
git add docs/spec/design/design.md docs/spec/product/product.md docs/superpowers/plans/2026-06-19-qml-plan5-submodule-tree.md
git commit -m "docs(spec): QML submodule tree in sidebar (Plan 5 outcome + spec)"
```

---

## Outcome

- **Implemented:** Recursive submodule rendering in the QML sidebar repo tree.
  Core gained `SubmoduleStatus` / `SubmoduleNode` and `GitRepo::submoduleTree()`
  (depth-first, opening each initialised submodule as its own repo; pinned short
  OID from `git_submodule_head_id`, status from `git_submodule_status`). The flat
  depth-1 `submodules()` was retired. `RepoListModel` became an arbitrary-depth
  `QAbstractItemModel` over a `unique_ptr<Node>` tree with new roles
  (`isSubmodule`, `shortOid`, `status`). `Sidebar.qml`'s delegate branches on
  `isSubmodule` for glyph (`❖` accent @0.7 vs `◧`), mono OID, status dot
  (`stateModified` dirty / `stateAdded` @0.55 clean), per-level guide rail +
  elbow, expand-by-default, and a divider between top-level repos.
- **Spec updated:** `docs/spec/design/design.md` (Repo tree rows — submodule
  glyph/OID/dot/rail/divider + sanctioned git-state token reuse);
  `docs/spec/product/product.md` (arbitrary-depth submodules with at-a-glance
  pinned-commit + clean/dirty/uninitialised state; submodule rows informational,
  open/checkout deferred).
- **Code:** `core/include/gittide/submodule.hpp` (new), `core/.../gitrepo.{hpp,cpp}`,
  `ui/.../repolistmodel.{hpp,cpp}`, `ui/qml/Sidebar.qml`,
  `tests/support/temprepo.{hpp,cpp}`, `tests/test_temp_repo.cpp`,
  `tests/test_git_repo_submodules.cpp` (new), `tests/ui/test_repo_list_model.cpp`,
  `tests/ui/test_qml_shell.cpp`, `tests/CMakeLists.txt`.
- **Commits:** `4a68795` TempRepo helpers; `115c939` core `submoduleTree()`;
  `34b6a4b` `RepoListModel` tree + drop core `submodules()`; `61958f5` sidebar
  delegate; `f464fd7` top-level repo divider.
- **Carry-forward minors:**
  - QML status int magic numbers `0/1/2` mirror `SubmoduleStatus` order
    (`Clean/Dirty/Uninitialized`) — kept as ints across the role boundary.
  - Rail/elbow `x` geometry (`depth * indentation - 8`) approximated from the
    mockup; verified to load clean headless + offscreen, but the exact rail offset
    was **not** pixel-verified by a human — nudge `- 8` if it sits wrong on a real
    display.
  - **Deviation from plan Task 3 CMake step:** the UI test sources list is
    `HEADER_FILE_ONLY` (slot files `#include`d into `ui/main.cpp`), so adding
    `support/temprepo.cpp` there would not compile/link it. Instead it was added
    as a real compiled source in `add_executable(gittide_ui_tests …)` and
    `${CMAKE_CURRENT_SOURCE_DIR}` (tests/) was added to that target's include dirs.
  - **Test-runner note:** Catch2 cases are registered individually via
    `catch_discover_tests`; `ctest -R gittide_core_tests` matches nothing — run
    the binary directly (`./build/tests/gittide_core_tests "[submodules]"`).
  - Deferred (per design §7): async tree build, submodule open/checkout actions,
    HEAD-vs-pin distinction beyond the binary dirty flag.

---

## Self-Review

**Spec coverage (against `2026-06-19-qml-submodule-tree-design.md`):**
- §3 Core API (`SubmoduleStatus`, `SubmoduleNode`, `submoduleTree()`, OID = pinned `git_submodule_head_id`, status via `git_submodule_status`, recursion, retire `submodules()`) → Tasks 1–3. ✓
- §4 UI model (recursive `Node`, stable `unique_ptr` storage + `Node*` in `internalPointer`, new roles) → Task 3. ✓
- §5 QML (delegate branch on `isSubmodule`, glyph/OID/dot/guide-rail/elbow/divider, expand-by-default) → Tasks 4–5. ✓
- §6 Testing (`TempRepo::addSubmodule` infra; clean/dirty/uninitialised + nesting core tests; recursive model + roles; QML smoke) → Tasks 1–4. ✓
- §7 Deferred (async; submodule actions; HEAD-vs-pin beyond binary dirty) → documented, not implemented. ✓

**Status int contract:** `0` Clean / `1` Dirty / `2` Uninitialized is consistent across Task 3 (`StatusRole` returns `static_cast<int>(status)`) and Task 4 QML (`model.status === 1` dirty, `=== 2` uninit) — matches `SubmoduleStatus` enum order in Task 2.

**Type/name consistency:** `submoduleTree`, `SubmoduleNode{path,name,shortOid,status,children}`, `SubmoduleStatus{Clean,Dirty,Uninitialized}`, model roles `{PathRole→repoPath, MissingRole→missing, IsSubmoduleRole→isSubmodule, ShortOidRole→shortOid, StatusRole→status}`, and helpers `addSubmodule`/`updateSubmodulesRecursive` are spelled identically across Tasks 1–4.

**Open risk flagged for execution:** the precise `TreeViewDelegate` `depth`/`indentation` rail geometry is verified at the live demo (Task 5); the `gittide_ui_tests` target already links `libgit2package` and Task 3 adds `support/temprepo.cpp` to its sources; libgit2 submodule-API call shapes (`git_submodule_add_setup`/`_clone`/`_add_finalize`/`_update`/`_status`/`_head_id`) are the documented signatures — adjust only if the installed libgit2 header disagrees.
