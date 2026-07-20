# Plan 34 — Submodule detail rows + right-aligned sync

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-20 |
| **Status** | `planned` |
| **Spec** | [`spec/product/2026-07-20-submodule-detail-and-right-align-design.md`](../spec/product/2026-07-20-submodule-detail-and-right-align-design.md) |
| **Depends on** | Plan 33 (repo tree entry redesign), Plan 24 (submodule rows) |

**Goal:** Give initialized submodules the same two-line detail as repos (branch /
dirty count / ahead-behind **vs the pinned commit**), and right-align the
ahead/behind indicators so line-1 dirty badge and line-2 sync form one column —
for both repos and submodules.

**Architecture:** `SubmoduleNode` gains six fields, filled inside
`GitRepo::submoduleTree()` (which already opens each initialized submodule during
recursion) using `head()`/`status()` plus a new `GitRepo::aheadBehind` helper
(`git_graph_ahead_behind`, current HEAD vs pinned commit, run on the submodule's
own repo). Because both the load seed (`setRepos`) and the poll
(`AsyncRepo::submoduleTree`) route through `submoduleTree()`, both get the data
from one path. The UI `Node` fields + QML roles already exist from Plan 33;
`appendSubmodules` / `reconcileChildren` start populating them, and the delegate
gains a two-line submodule branch + a right-aligning spacer.

**Tech stack:** C++23, libgit2 (`git_submodule_wd_id`/`head_id`,
`git_graph_ahead_behind`), Qt 6 (Quick/QML + QtTest), Catch2 (core tests), QCoro.

## Global constraints

- **No Qt in `core/`** ([`spec/engineering/engineering.md`](../spec/engineering/engineering.md)).
  libgit2/nlohmann stay PRIVATE to `core/` — the new `aheadBehind` takes/returns
  `std::string`/`std::pair<int,int>`, never a `git_oid`.
- **Errors are values** — new core method returns `Expected<...>`.
- **Colour comes from a theme token**, never a hex literal in QML.
- **TDD** — failing test first for every task. New core test file → the
  `gittide_core_tests` list in `tests/CMakeLists.txt`. New UI test slots go in
  already-registered files (`tests/ui/test_repo_list_model.cpp`,
  `tests/ui/test_qml_sync.cpp`) — no `main.cpp` edit for those.
- Keep passing: existing `TestRepoListModel`, `TestQmlSync`, and the submodule
  identity-preservation tests (`applySubmodules_inPlaceUpdate_preservesSiblingIdentity`,
  `depth2_submodule_owner_path_and_nontop_apply`). Object names `repoTree` /
  `fetchAllButton` and the `fetchFailedLabel` id unchanged.
- Build: `cmake --build build --parallel`. Test:
  `ctest --test-dir build --output-on-failure` (core = `gittide_core_tests`,
  ui = `gittide_ui_tests`). Ignore the pre-existing unrelated `ECMPoQmToolsTest`
  failure.

---

## Task 1: Core — SubmoduleNode fields, aheadBehind helper, fill in submoduleTree

**Files:**
- Modify: `core/include/gittide/submodule.hpp`
- Modify: `core/include/gittide/gitrepo.hpp` (declare `aheadBehind`)
- Modify: `core/src/gitrepo.cpp` (`aheadBehind` impl; fill fields in `submoduleTree`)
- Create: `tests/core/test_submodule_detail.cpp`
- Modify: `tests/CMakeLists.txt` (add the new file to `gittide_core_tests`)

**Interfaces (produced):**
- `SubmoduleNode` += `std::string branch; bool detached; std::string headShortOid; int dirtyCount; int ahead; int behind;`
- `Expected<std::pair<int, int>> GitRepo::aheadBehind(std::string localOid, std::string baseOid) const;` — {ahead, behind} of `localOid` relative to `baseOid`.

- [ ] **Step 1: Write the failing core tests** — create
  `tests/core/test_submodule_detail.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

TEST_CASE("aheadBehind counts commits between two oids", "[aheadbehind]")
{
    test::TempRepo repo;
    repo.setIdentity("T", "t@e.x");
    repo.writeFile("a.txt", "1");
    repo.commitAll("c1");

    auto g = GitRepo::open(repo.path());
    REQUIRE(g.has_value());
    const std::string o1 = g->head().value().oid;

    repo.writeFile("a.txt", "2");
    repo.commitAll("c2");
    const std::string o2 = g->head().value().oid;

    repo.writeFile("a.txt", "3");
    repo.commitAll("c3");
    const std::string o3 = g->head().value().oid;

    auto ahead = g->aheadBehind(o3, o1); // o3 is 2 past o1
    REQUIRE(ahead.has_value());
    REQUIRE(ahead->first == 2);
    REQUIRE(ahead->second == 0);

    auto behind = g->aheadBehind(o1, o3); // o1 is 2 behind o3
    REQUIRE(behind.has_value());
    REQUIRE(behind->first == 0);
    REQUIRE(behind->second == 2);

    auto equal = g->aheadBehind(o2, o2);
    REQUIRE(equal.has_value());
    REQUIRE(equal->first == 0);
    REQUIRE(equal->second == 0);
}

TEST_CASE("submoduleTree reports submodule branch/dirty/ahead-behind vs pin", "[submodule-detail]")
{
    // Child repo: c1, c2 on master; capture c1's oid.
    test::TempRepo child;
    child.setIdentity("T", "t@e.x");
    child.writeFile("f.txt", "1");
    child.commitAll("c1");
    auto cg = GitRepo::open(child.path());
    REQUIRE(cg.has_value());
    const std::string oid1 = cg->head().value().oid;
    child.writeFile("f.txt", "2");
    child.commitAll("c2"); // child master tip = c2

    // Parent pins the submodule at c2 (child's checked-out HEAD when cloned).
    test::TempRepo parent;
    parent.setIdentity("T", "t@e.x");
    parent.writeFile("top.txt", "p");
    parent.commitAll("base");
    parent.addSubmodule("libc", child.path());
    parent.commitAll("add submodule"); // pin = c2

    auto pg = GitRepo::open(parent.path());
    REQUIRE(pg.has_value());

    auto tree0 = pg->submoduleTree();
    REQUIRE(tree0.has_value());
    REQUIRE(tree0->size() == 1);
    const SubmoduleNode& s0 = (*tree0)[0];
    REQUIRE(s0.ahead == 0);            // freshly added: on the pin
    REQUIRE(s0.behind == 0);
    REQUIRE(s0.dirtyCount == 0);       // clean working tree
    REQUIRE(s0.headShortOid.size() == 7);
    REQUIRE((s0.detached || !s0.branch.empty())); // on a branch or detached, either is valid

    // Move the submodule checkout back to c1 → 1 behind the pin (c2).
    const std::filesystem::path subPath = s0.path;
    auto sg = GitRepo::open(subPath);
    REQUIRE(sg.has_value());
    REQUIRE(sg->checkoutCommit(oid1).has_value());

    auto tree1 = pg->submoduleTree();
    REQUIRE(tree1.has_value());
    const SubmoduleNode& s1 = (*tree1)[0];
    REQUIRE(s1.detached == true);
    REQUIRE(s1.ahead == 0);
    REQUIRE(s1.behind == 1);
    REQUIRE(s1.headShortOid == oid1.substr(0, 7));

    // Dirty the submodule working tree → dirtyCount >= 1.
    {
        std::ofstream(subPath / "f.txt") << "dirty\n";
    }
    auto tree2 = pg->submoduleTree();
    REQUIRE(tree2.has_value());
    REQUIRE((*tree2)[0].dirtyCount >= 1);
}
```

- [ ] **Step 2: Register the test file** — in `tests/CMakeLists.txt`, add to the
  `gittide_core_tests` sources list (next to `core/test_gitrepo_logallrefs.cpp`):

```cmake
  core/test_submodule_detail.cpp
```

- [ ] **Step 3: Run to verify it fails** — `aheadBehind` is undefined and the new
  `SubmoduleNode` fields don't exist.
  Run: `cmake -S . -B build && cmake --build build --parallel`
  Expected: compile error — `'aheadBehind' is not a member of gittide::GitRepo`
  and `'ahead' is not a member of gittide::SubmoduleNode`.

- [ ] **Step 4: Add the SubmoduleNode fields** — in
  `core/include/gittide/submodule.hpp`, extend the struct:

```cpp
struct SubmoduleNode
{
    std::filesystem::path      path;                          // absolute working-dir path
    std::string                name;                          // .gitmodules name (UTF-8)
    std::string                shortOid;                      // pinned gitlink commit, 7 hex; "" if Uninitialized
    SubmoduleStatus            status = SubmoduleStatus::Clean;
    std::string                branch;                        // current branch; "" when detached or uninitialised
    bool                       detached = false;              // submodule HEAD is detached
    std::string                headShortOid;                  // current checked-out commit, 7 hex; "" if uninitialised
    int                        dirtyCount = 0;                // working-tree changed files
    int                        ahead      = 0;                // commits current HEAD is ahead of the pinned commit
    int                        behind     = 0;                // commits current HEAD is behind the pinned commit
    std::vector<SubmoduleNode> children;                      // recursive; empty if Uninitialized or leaf
};
```

- [ ] **Step 5: Declare `aheadBehind`** — in `core/include/gittide/gitrepo.hpp`,
  next to `syncStatus()` (around line 162), add:

```cpp
    /// Commits `localOid` is ahead of / behind `baseOid`, via git_graph_ahead_behind
    /// on this repository. Both OIDs must be reachable here (else an error). Returns
    /// {ahead, behind}. Used for a submodule's current HEAD vs its pinned commit.
    Expected<std::pair<int, int>> aheadBehind(std::string localOid, std::string baseOid) const;
```

  Ensure `<utility>` is included by `gitrepo.hpp` (for `std::pair`); add it if
  the header doesn't already pull it in.

- [ ] **Step 6: Implement `aheadBehind`** — in `core/src/gitrepo.cpp` (e.g. just
  after `syncStatus()`):

```cpp
Expected<std::pair<int, int>> GitRepo::aheadBehind(std::string localOid, std::string baseOid) const
{
    git_oid local{};
    git_oid base{};
    if (int rc = git_oid_fromstr(&local, localOid.c_str()); rc < 0)
        return std::unexpected(lastGitError(rc));
    if (int rc = git_oid_fromstr(&base, baseOid.c_str()); rc < 0)
        return std::unexpected(lastGitError(rc));

    std::size_t ahead = 0;
    std::size_t behind = 0;
    if (int rc = git_graph_ahead_behind(&ahead, &behind, m_repo, &local, &base); rc < 0)
        return std::unexpected(lastGitError(rc));

    return std::pair<int, int>{static_cast<int>(ahead), static_cast<int>(behind)};
}
```

- [ ] **Step 7: Capture the pinned full OID in `submoduleTree`'s foreach** — in
  `core/src/gitrepo.cpp`, extend the `Payload` struct and the callback so the
  full 40-hex pinned commit is collected in a parallel vector aligned with
  `result` (the callback runs while the submodule cache is held, so the OID must
  be read here). Replace the `Payload`/`result`/`cb` region:

```cpp
    struct Payload
    {
        std::vector<SubmoduleNode>* out;
        std::vector<std::string>*   pinnedFull; // full 40-hex pinned oid per node, "" if uninitialised
        const std::filesystem::path* wd;
        git_repository* repo;
    };
    std::vector<SubmoduleNode> result;
    std::vector<std::string>   pinnedFull;
    Payload payload{&result, &pinnedFull, &wd, m_repo};

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

        std::string pinnedHex;
        if (node.status != SubmoduleStatus::Uninitialized)
        {
            if (const git_oid* hid = git_submodule_head_id(sm))
            {
                char hex[GIT_OID_HEXSZ + 1];
                git_oid_tostr(hex, sizeof(hex), hid);
                node.shortOid.assign(hex, hex + 7);
                pinnedHex.assign(hex, hex + GIT_OID_HEXSZ);
            }
        }

        p->out->push_back(std::move(node));
        p->pinnedFull->push_back(std::move(pinnedHex));
        return 0;
    };
```

- [ ] **Step 8: Fill the new fields in the recursion loop** — replace the descend
  loop so it indexes `pinnedFull` and populates branch/detached/headShortOid/
  dirtyCount/ahead/behind from the child repo it already opens:

```cpp
    // Descend into each initialised submodule; fill per-submodule detail from its
    // own repository (branch/dirty/current-oid + ahead/behind of current vs pin).
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        SubmoduleNode& node = result[i];
        if (node.status == SubmoduleStatus::Uninitialized)
            continue;
        auto child = GitRepo::open(node.path);
        if (!child)
            continue; // a broken child degrades to no detail, not a fatal error

        if (auto hs = child->head())
        {
            node.branch       = hs->branch;
            node.detached     = hs->detached;
            node.headShortOid = hs->oid.size() >= 7 ? hs->oid.substr(0, 7) : hs->oid;
            if (!hs->oid.empty() && !pinnedFull[i].empty())
            {
                // current HEAD ahead/behind of the pinned commit, on the submodule repo.
                if (auto ab = child->aheadBehind(hs->oid, pinnedFull[i]))
                {
                    node.ahead  = ab->first;
                    node.behind = ab->second;
                }
                // pinned commit absent (shallow) → aheadBehind fails → leave 0/0.
            }
        }
        if (auto st = child->status())
            node.dirtyCount = static_cast<int>(st->size());

        if (auto sub = child->submoduleTree())
            node.children = std::move(*sub);
    }
```

- [ ] **Step 9: Run the core tests — verify pass**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_core_tests --output-on-failure`
  Expected: `[aheadbehind]` and `[submodule-detail]` PASS; existing core tests
  still PASS.

- [ ] **Step 10: Commit**

```bash
git add core/include/gittide/submodule.hpp core/include/gittide/gitrepo.hpp \
        core/src/gitrepo.cpp tests/core/test_submodule_detail.cpp tests/CMakeLists.txt
git commit -m "feat(core): submoduleTree fills submodule branch/dirty/ahead-behind vs pin"
```

---

## Task 2: UI model — populate + reconcile submodule detail

Copy the new `SubmoduleNode` fields onto the UI `Node` when building submodule
rows, and compare/update them on the incremental poll so a submodule's row
refreshes in place (no subtree collapse). The `Node` fields and QML roles already
exist (Plan 33); submodule rows just start using them. The delegate shows the
**current** checkout, so map `headShortOid` → the `shortOid` role.

**Files:**
- Modify: `ui/src/repolistmodel.cpp` (`appendSubmodules`, `submodulesEqual`, `reconcileChildren`)
- Test: `tests/ui/test_repo_list_model.cpp`

**Interfaces (consumed):** the six new `SubmoduleNode` fields from Task 1; existing
`Node` fields `branch`/`detached`/`dirtyCount`/`ahead`/`behind`/`shortOid` and roles
`BranchRole`/`DetachedRole`/`DirtyCountRole`/`AheadRole`/`BehindRole`/`ShortOidRole`.

- [ ] **Step 1: Write the failing tests** — add two slots to `TestRepoListModel`
  in `tests/ui/test_repo_list_model.cpp`. The first asserts a submodule row now
  carries branch/dirty/ahead via `applySubmodules`; the second asserts an in-place
  update of those fields preserves node identity.

```cpp
    void submodule_row_exposes_branch_dirty_and_ahead()
    {
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos({RepoRef{.path = "/tmp/gittide-root", .alias = "root"}});

        SubmoduleNode sub;
        sub.name         = "libc";
        sub.path         = "/tmp/gittide-root/libc";
        sub.status       = SubmoduleStatus::Dirty;
        sub.shortOid     = "pin1234";       // pinned
        sub.branch       = "";              // detached
        sub.detached     = true;
        sub.headShortOid = "cur5678";       // current checkout
        sub.dirtyCount   = 3;
        sub.ahead        = 2;
        sub.behind       = 0;
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {sub});

        const QModelIndex top = model.index(0, 0);
        const QModelIndex idx = model.index(0, 0, top);
        QVERIFY(idx.isValid());
        QCOMPARE(model.data(idx, RepoListModel::DetachedRole).toBool(), true);
        QCOMPARE(model.data(idx, RepoListModel::DirtyCountRole).toInt(), 3);
        QCOMPARE(model.data(idx, RepoListModel::AheadRole).toInt(), 2);
        QCOMPARE(model.data(idx, RepoListModel::BehindRole).toInt(), 0);
        // The row shows the CURRENT checkout, not the pinned commit.
        QCOMPARE(model.data(idx, RepoListModel::ShortOidRole).toString(), QStringLiteral("cur5678"));
    }

    void applySubmodules_updates_detail_in_place()
    {
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        auto makeSub = [](int ahead, int dirty, const char* head)
        {
            SubmoduleNode s;
            s.name         = "libc";
            s.path         = "/tmp/gittide-root/libc";
            s.status       = SubmoduleStatus::Dirty;
            s.shortOid     = "pin1234";
            s.detached     = true;
            s.headShortOid = head;
            s.dirtyCount   = dirty;
            s.ahead        = ahead;
            return s;
        };

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos({RepoRef{.path = "/tmp/gittide-root", .alias = "root"}});
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {makeSub(1, 1, "aaa0001")});

        const QModelIndex top = model.index(0, 0);
        const QModelIndex idx = model.index(0, 0, top);

        // A branch/pin move: same submodule, new ahead/dirty/current-oid.
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {makeSub(3, 5, "bbb0002")});

        QCOMPARE(removed.count(), 0);   // in-place, not a destructive rebuild
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(model.index(0, 0, top), idx); // same node identity
        QCOMPARE(model.data(idx, RepoListModel::AheadRole).toInt(), 3);
        QCOMPARE(model.data(idx, RepoListModel::DirtyCountRole).toInt(), 5);
        QCOMPARE(model.data(idx, RepoListModel::ShortOidRole).toString(), QStringLiteral("bbb0002"));
    }
```

- [ ] **Step 2: Run to verify it fails**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: FAIL — `applySubmodules` doesn't copy the new fields, so `AheadRole`
  is 0, `ShortOidRole` is `"pin1234"` (pinned, not the current `cur5678`), and the
  in-place update leaves ahead/dirty stale.

- [ ] **Step 3: Add a shared field-copy helper + use it in `appendSubmodules`** —
  in `ui/src/repolistmodel.cpp`, add a small anonymous-namespace helper near the
  top and call it from `appendSubmodules`. It centralises the `SubmoduleNode → Node`
  field mapping (including headShortOid → shortOid) so the build and reconcile
  paths can't drift.

```cpp
namespace {
// The 7-hex OID the delegate shows for a submodule: the CURRENT checkout when
// initialised, else the pinned OID (empty for uninitialised).
QString submoduleDisplayOid(const gittide::SubmoduleNode& s)
{
    return QString::fromStdString(s.headShortOid.empty() ? s.shortOid : s.headShortOid);
}
} // namespace
```

  Then in `appendSubmodules`, after the existing field assignments and before
  `appendSubmodules(*node, s.children)`, add:

```cpp
        node->shortOid    = submoduleDisplayOid(s);
        node->branch      = QString::fromStdString(s.branch);
        node->detached    = s.detached;
        node->dirtyCount  = s.dirtyCount;
        node->ahead       = s.ahead;
        node->behind      = s.behind;
```

  Remove the old `node->shortOid = QString::fromStdString(s.shortOid);` line (the
  new `submoduleDisplayOid` assignment replaces it).

- [ ] **Step 4: Compare the new fields in `submodulesEqual`** — so a change to any
  of them is not treated as "unchanged" and does trigger a refresh. Replace the
  per-child comparison in `submodulesEqual`:

```cpp
        const Node& c = *node.children[i];
        const auto& s = subs[i];
        if (!c.isSubmodule
            || c.path != QString::fromStdString(s.path.generic_string())
            || c.status != s.status
            || c.shortOid != submoduleDisplayOid(s)
            || c.branch != QString::fromStdString(s.branch)
            || c.detached != s.detached
            || c.dirtyCount != s.dirtyCount
            || c.ahead != s.ahead
            || c.behind != s.behind
            || !submodulesEqual(c, s.children))
            return false;
```

- [ ] **Step 5: Update the new fields in-place in `reconcileChildren`** — in the
  `sameShape` branch, extend the changed-field update and the emitted roles:

```cpp
            Node&       c       = *parent.children[i];
            const auto& s       = subs[i];
            const QString oid     = submoduleDisplayOid(s);
            const bool    missing = s.status == gittide::SubmoduleStatus::Uninitialized;
            const QString branch  = QString::fromStdString(s.branch);
            if (c.status != s.status || c.shortOid != oid || c.missing != missing
                || c.branch != branch || c.detached != s.detached
                || c.dirtyCount != s.dirtyCount || c.ahead != s.ahead || c.behind != s.behind)
            {
                c.status              = s.status;
                c.shortOid            = oid;
                c.missing             = missing;
                c.branch              = branch;
                c.detached            = s.detached;
                c.dirtyCount          = s.dirtyCount;
                c.ahead               = s.ahead;
                c.behind              = s.behind;
                const QModelIndex idx = index(static_cast<int>(i), 0, parentIdx);
                emit dataChanged(idx, idx, {StatusRole, ShortOidRole, MissingRole,
                                            BranchRole, DetachedRole, DirtyCountRole,
                                            AheadRole, BehindRole});
            }
            reconcileChildren(c, index(static_cast<int>(i), 0, parentIdx), s.children);
```

- [ ] **Step 6: Run the tests — verify pass**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: both new slots PASS; existing submodule identity/roundtrip slots still
  PASS.

- [ ] **Step 7: Commit**

```bash
git add ui/src/repolistmodel.cpp tests/ui/test_repo_list_model.cpp
git commit -m "feat(ui): submodule rows carry branch/dirty/ahead-behind; refresh in place"
```

---

## Task 3: QML — two-line submodule delegate + right-aligned sync

Make the delegate's line-2 (branch + sync) show for **initialized submodules**
too, right-align the ahead/behind indicators for both row kinds, and drop the old
standalone submodule OID + status dot (their info now lives on the two lines).

**Files:**
- Modify: `ui/qml/Sidebar.qml` (delegate `implicitHeight`; the line-1 submodule
  OID/dot block; the dirty-badge and line-2 visibility gates; the line-2 spacer)
- Test: `tests/ui/test_qml_sync.cpp`

- [ ] **Step 1: Write the failing test** — add a slot to `TestQmlSync` (declare in
  `private slots:`; define before `#include "test_qml_sync.moc"`). It seeds a repo
  with an initialized submodule carrying detail via `applySubmodules`, loads
  `Main.qml`, and asserts the load succeeds and the submodule row exposes the new
  roles.

```cpp
void TestQmlSync::sidebar_submodule_row_exposes_detail()
{
    using gittide::SubmoduleNode;
    using gittide::SubmoduleStatus;

    ThemeManager mgr;
    mgr.setMode(ThemeManager::Mode::Dark);
    QmlTheme theme(&mgr);
    RepoListModel repoModel;
    repoModel.setRepos({gittide::RepoRef{.path = "/tmp/gittide-qml-root", .alias = "root"}});

    SubmoduleNode sub;
    sub.name         = "libc";
    sub.path         = "/tmp/gittide-qml-root/libc";
    sub.status       = SubmoduleStatus::Dirty;
    sub.detached     = true;
    sub.headShortOid = "cur5678";
    sub.dirtyCount   = 2;
    sub.ahead        = 1;
    repoModel.applySubmodules(QStringLiteral("/tmp/gittide-qml-root"), {sub});

    gittide::ProjectStore store;
    auto& p = store.createProject("P");
    ProjectController controller(&store);
    controller.activate(QString::fromStdString(p.id));

    QQmlApplicationEngine engine;
    installQmlContext(engine.rootContext(), &theme, &repoModel, &controller, nullptr);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QVERIFY(!engine.rootObjects().isEmpty()); // loads with the extended delegate, no fatal QML error

    const QModelIndex top = repoModel.index(0, 0);
    const QModelIndex idx = repoModel.index(0, 0, top);
    QVERIFY(idx.isValid());
    QCOMPARE(repoModel.data(idx, RepoListModel::DirtyCountRole).toInt(), 2);
    QCOMPARE(repoModel.data(idx, RepoListModel::AheadRole).toInt(), 1);
}
```

  Declaration to add under `private slots:`:
  ```cpp
    void sidebar_submodule_row_exposes_detail();
  ```

- [ ] **Step 2: Run to verify it passes as a pre-change guard** — like the Plan 33
  QML slot, this asserts model data + load; it should PASS now (data comes from
  Task 2) and its role is to lock that `Main.qml` keeps loading after the delegate
  edit. Run:
  `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: PASS. Proceed to the delegate edit; re-run in Step 7.

- [ ] **Step 3: Make initialized submodules two-line** — in `ui/qml/Sidebar.qml`,
  change the delegate height so only *uninitialised* submodules stay single-line
  (replace line ~224):

```qml
                implicitHeight: row.uninit ? 30 : 46
```

- [ ] **Step 4: Remove the old submodule OID + status dot from line 1** — delete
  this block (currently ~lines 298–314), because the current OID moves to line 2
  and the clean/dirty dot is replaced by the dirty badge:

```qml
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
```

- [ ] **Step 5: Extend the dirty badge to initialized submodules** — change the
  dirty-badge `RowLayout` visibility (currently `visible: !row.isSub && !model.missing`)
  so it shows for repos and initialized submodules alike:

```qml
                            // Dirty/clean badge — repos and initialised submodules.
                            RowLayout {
                                visible: !model.missing && !row.uninit
                                spacing: 4
```

  (The three inner Labels/Rectangle — amber dot + count, or `✓` — are unchanged.)

- [ ] **Step 6: Rewrite line 2 for both row kinds + right-align sync** — replace
  the entire line-2 `RowLayout` (currently ~lines 397–445) with:

```qml
                        // LINE 2 — branch + ahead/behind. Repos and initialised
                        // submodules; sync indicators right-aligned into a column.
                        RowLayout {
                            visible: !model.missing && !row.uninit
                            spacing: 6

                            // Branch glyph (hidden when detached).
                            Label {
                                visible: !model.detached
                                text: "⎇"
                                color: theme.textSecondary
                                font.pixelSize: 11
                            }
                            Label {
                                text: model.detached ? "detached" : model.branch
                                color: theme.textSecondary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.maximumWidth: 180
                            }
                            // Detached: the current short commit id (repo's own, or
                            // the submodule's current checkout).
                            Label {
                                visible: model.detached
                                text: model.shortOid
                                color: theme.textMuted
                                font.family: "monospace"
                                font.pixelSize: 11
                            }

                            // Spacer → push the sync indicators to the right edge.
                            Item { Layout.fillWidth: true }

                            // Ahead / behind. Repos: only with an upstream and not
                            // detached. Submodules: vs the pinned commit, shown even
                            // when detached (the usual submodule state).
                            Label {
                                visible: model.ahead > 0
                                         && (row.isSub || (!model.detached && model.hasUpstream))
                                text: "↑" + model.ahead
                                color: theme.stateAdded
                                font.pixelSize: 11
                            }
                            Label {
                                visible: model.behind > 0
                                         && (row.isSub || (!model.detached && model.hasUpstream))
                                text: "↓" + model.behind
                                color: theme.stateIncoming
                                font.pixelSize: 11
                            }
                            // Repo with no upstream → dash (submodules never show this).
                            Label {
                                visible: !row.isSub && !model.detached && !model.hasUpstream
                                text: "—"
                                color: theme.textMuted
                                font.pixelSize: 11
                            }
                        }
```

- [ ] **Step 7: Run the test — verify pass (no QML load regression)**
  Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
  Expected: `sidebar_submodule_row_exposes_detail` PASS; existing `TestQmlSync`
  slots PASS; no QML warnings about unknown roles in the run output.

- [ ] **Step 8: Visual check in the running app** — the controller launches the app
  (isolated XDG config + a project containing a repo **with an initialized
  submodule**, ideally one checked out off its pin) and confirms against the spec:
  a submodule row is two-line with a dirty badge on line 1 and `⎇ <branch>` /
  `detached <oid>` + right-aligned `↑N/↓N` on line 2; repo rows show their
  ahead/behind right-aligned in the same column; uninitialised submodules stay
  single-line with the `Init` button.

- [ ] **Step 9: Commit**

```bash
git add ui/qml/Sidebar.qml tests/ui/test_qml_sync.cpp
git commit -m "feat(ui): two-line submodule rows + right-aligned ahead/behind"
```

---

## Task 4: Close-out — docs true

- [ ] **Step 1:** In `docs/spec/product/2026-07-20-submodule-detail-and-right-align-design.md`,
  set `**Status:** shipped` and add `**Shipped:** 2026-07-20 (Plan 34)`.
- [ ] **Step 2:** Set this plan's header `**Status**` to `done` and fill the
  Outcome section below.
- [ ] **Step 3:** Append the index row to `docs/plans/index.md`:
  ```
  | [Plan 34 — Submodule detail rows + right-aligned sync](2026-07-20-plan34-submodule-detail.md) | 2026-07-20 | done | product · core · ui |
  ```
- [ ] **Step 4:** Add decision **D43** to `docs/decisions.md`: submodule rows show
  branch + dirty + ahead/behind **vs the pinned commit** (computed on the
  submodule's own repo); sync indicators are right-aligned into a column; the
  detail is filled once in `submoduleTree()` so seed and poll share a path. Link
  the design doc.
- [ ] **Step 5: Commit**

```bash
git add docs/
git commit -m "docs: close out Plan 34 — submodule detail rows + right-aligned sync"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: <which `spec/` sections now describe this>.
> - Code: <the main files/types that resulted>.
