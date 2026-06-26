# Plan 25 — History redesign + full git-graph tab

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-06-26 |
| **Status** | `planned` |
| **Spec** | [spec/product/2026-06-26-history-graph-tab-design.md](../spec/product/2026-06-26-history-graph-tab-design.md); touches [spec/product/rebase-interactive.md](../spec/product/rebase-interactive.md) §3.2 |
| **Depends on** | Plan 22 (history-editing UX), Plan 23 (history drag/squash) |

**Goal:** Move the branch graph out of the History list into a new **Graph tab**
that renders a full git graph of all refs (local + remote branches + tags);
strip the in-list graph column and `⠿` drag grips from History; fix the broken
drag-to-reorder/squash.

**Architecture:** `core/` gains `GitRepo::logAllRefs` (walk every ref) and
`GitRepo::refTips` (oid → ref-name labels). `GraphBuilder` is untouched — it
already lays out multi-branch graphs. The Graph tab reuses `HistoryListModel`
(a second instance exposed as `RepoViewModel::graph`) plus a new `refLabels`
role for branch/tag chips. New `GraphPane.qml` is a trimmed `HistoryPane`
(graph + chips + reused `CommitDetail`/`CommitContextMenu`, no drag). The
History drag bug is fixed by replacing the delegate's `MouseArea` (which steals
the pointer grab) with cooperating `TapHandler`s.

**Tech stack:** C++23 + libgit2 (`git_revwalk_push_glob`, `git_reference_iterator`),
Qt 6 Quick/QML (`TapHandler`, `DragHandler`), QCoro async bridge, Catch2 +
QtTest headless runner.

## Global constraints

- **No Qt in `core/`** — `logAllRefs`/`refTips`/`RefTip` are `std`-only
  (`std::string`, `std::vector`). libgit2 stays PRIVATE to `core/`. See
  [engineering.md](../spec/engineering/engineering.md).
- **Errors are values:** new core methods return `Expected<T>`; no exceptions.
- **Paths/OIDs** via existing hex helpers; never shell out to `git`.
- **Colour from theme tokens** only (lane colours, chip colours).
- New `core/` sources → `core/CMakeLists.txt`; new `ui/` sources →
  `ui/CMakeLists.txt`; new tests → `tests/CMakeLists.txt`.
- **Must keep passing / stable object names:** `historyList`, `historyPane`,
  `historyTabBody`, `changesTabBody`, `reorderGrip` test (will be updated to
  assert absence), `dropZoneAt` band test, `Ctrl+1`/`Ctrl+2` wiring.
- **TDD:** failing test first for every task.

---

## Task 1: `GitRepo::logAllRefs` — walk every branch and tag

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare next to `log`)
- Modify: `core/src/gitrepo.cpp` (implement next to `log`, ~line 848)
- Modify: `tests/support/temprepo.hpp`, `tests/support/temprepo.cpp` (add `tagHead`)
- Test: `tests/core/test_gitrepo_logallrefs.cpp` (new) + register in `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `Expected<std::vector<CommitNode>> GitRepo::logAllRefs(unsigned limit = 0) const`
  — same `CommitNode` shape as `log`, but reachable set is every ref.
- Produces: `void TempRepo::tagHead(std::string_view name)` — lightweight tag at HEAD.

- [ ] **Step 1: Add the `TempRepo::tagHead` helper (test support).**

In `tests/support/temprepo.hpp`, declare under the existing helpers:

```cpp
// Create a lightweight tag named `name` pointing at current HEAD.
void tagHead(std::string_view name);
```

In `tests/support/temprepo.cpp` (include `<git2/tag.h>`, `<git2/refs.h>` as
needed), implement:

```cpp
void TempRepo::tagHead(std::string_view name)
{
    git_object* head = nullptr;
    if (git_revparse_single(&head, m_repo, "HEAD") != 0)
        return;
    git_oid out;
    git_tag_create_lightweight(&out, m_repo, std::string(name).c_str(), head, 0);
    git_object_free(head);
}
```

- [ ] **Step 2: Write the failing test.**

Create `tests/core/test_gitrepo_logallrefs.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

namespace {
bool hasSummary(const std::vector<CommitNode>& v, const std::string& s)
{
    return std::any_of(v.begin(), v.end(),
                       [&](const CommitNode& c) { return c.summary == s; });
}
} // namespace

TEST_CASE("logAllRefs reaches commits on every branch, log only HEAD")
{
    test::TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "1");
    repo.commitAll("base commit");

    auto git = GitRepo::open(repo.path());
    REQUIRE(git.has_value());

    // Branch off base, switch to it, commit there.
    auto head = git->log(1);
    REQUIRE(head.has_value());
    REQUIRE_FALSE(head->empty());
    REQUIRE(git->createBranch("feature", head->front().oid).has_value());
    REQUIRE(git->checkoutBranch("feature").has_value());
    repo.writeFile("b.txt", "2");
    repo.commitAll("feature commit");

    // Back on feature: log() (HEAD) sees base + feature, not master-only extras.
    // Make master diverge so the two sets differ.
    REQUIRE(git->checkoutBranch("master").has_value());
    repo.writeFile("c.txt", "3");
    repo.commitAll("master commit");

    auto reopened = GitRepo::open(repo.path());
    REQUIRE(reopened.has_value());

    auto head_only = reopened->log(0);
    auto all       = reopened->logAllRefs(0);
    REQUIRE(head_only.has_value());
    REQUIRE(all.has_value());

    // HEAD is master: sees master + base, NOT feature.
    REQUIRE(hasSummary(*head_only, "master commit"));
    REQUIRE_FALSE(hasSummary(*head_only, "feature commit"));

    // logAllRefs sees every branch tip.
    REQUIRE(hasSummary(*all, "master commit"));
    REQUIRE(hasSummary(*all, "feature commit"));
    REQUIRE(hasSummary(*all, "base commit"));
}

TEST_CASE("logAllRefs includes tagged commits")
{
    test::TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "1");
    repo.commitAll("tagged base");
    repo.tagHead("v1.0");

    auto git = GitRepo::open(repo.path());
    REQUIRE(git.has_value());
    auto all = git->logAllRefs(0);
    REQUIRE(all.has_value());
    REQUIRE(hasSummary(*all, "tagged base"));
}
```

Register in `tests/CMakeLists.txt` next to `test_graph_builder.cpp`:

```cmake
  test_gitrepo_logallrefs.cpp
```

- [ ] **Step 3: Run the test, verify it fails** (no `logAllRefs`):

`cmake --build build --parallel && ctest --test-dir build -R logallrefs --output-on-failure`
Expected: compile error / FAIL — `logAllRefs` undeclared.

- [ ] **Step 4: Declare in `core/include/gittide/gitrepo.hpp`** (right after `log`):

```cpp
    // Walk every ref (refs/heads/*, refs/remotes/*, refs/tags/*) topologically
    // and by time, newest first. For the full-graph view. limit 0 = unbounded.
    // Same CommitNode shape as log(), so GraphBuilder::build() consumes it.
    Expected<std::vector<CommitNode>> logAllRefs(unsigned limit = 0) const;
```

- [ ] **Step 5: Implement in `core/src/gitrepo.cpp`** (after `log`). Factor the
  per-commit fill so we don't duplicate `log`'s body — extract a static helper,
  then have both push differently:

```cpp
namespace {
// Fill a CommitNode from a looked-up commit object (shared by log/logAllRefs).
CommitNode nodeFromCommit(git_repository* repo, const git_oid& oid)
{
    CommitNode node;
    char hex[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(hex, sizeof(hex), &oid);
    node.oid = hex;

    git_commit* c = nullptr;
    if (git_commit_lookup(&c, repo, &oid) < 0)
        return node;

    const char* msg = git_commit_summary(c);
    node.summary    = msg ? msg : "";
    const git_signature* author = git_commit_author(c);
    node.author = author ? author->name : "";
    node.time   = author ? author->when.time : 0;

    unsigned nparents = git_commit_parentcount(c);
    node.parents.reserve(nparents);
    for (unsigned i = 0; i < nparents; ++i)
    {
        const git_oid* pid = git_commit_parent_id(c, i);
        char phex[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(phex, sizeof(phex), pid);
        node.parents.push_back(phex);
    }
    git_commit_free(c);
    return node;
}
} // namespace

Expected<std::vector<CommitNode>> GitRepo::logAllRefs(unsigned limit) const
{
    git_revwalk* walk = nullptr;
    int rc            = git_revwalk_new(&walk, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

    // Push every ref. Missing globs (e.g. no remotes) are not fatal — a repo
    // with no matching refs simply contributes nothing.
    git_revwalk_push_glob(walk, "refs/heads/*");
    git_revwalk_push_glob(walk, "refs/remotes/*");
    git_revwalk_push_glob(walk, "refs/tags/*");

    std::vector<CommitNode> result;
    git_oid oid;
    unsigned count = 0;
    while ((limit == 0 || count < limit) && git_revwalk_next(&oid, walk) == 0)
    {
        result.push_back(nodeFromCommit(m_repo, oid));
        ++count;
    }
    git_revwalk_free(walk);
    return result;
}
```

> Optional cleanup (same commit): rewrite `log`'s inner loop to call
> `nodeFromCommit` too, removing the duplicated fill block. Keep `log`'s
> `push_head` + unborn-branch handling unchanged.

- [ ] **Step 6: Run the test, verify it passes.**

`ctest --test-dir build -R logallrefs --output-on-failure`
Expected: PASS (both cases).

- [ ] **Step 7: Commit.**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp \
        tests/support/temprepo.hpp tests/support/temprepo.cpp \
        tests/core/test_gitrepo_logallrefs.cpp tests/CMakeLists.txt
git commit -m "feat(core): GitRepo::logAllRefs walks all refs for the graph view"
```

---

## Task 2: `GitRepo::refTips` — oid → ref-name labels

**Files:**
- Modify: `core/include/gittide/graph.hpp` (add `RefTip` struct)
- Modify: `core/include/gittide/gitrepo.hpp` (declare `refTips`)
- Modify: `core/src/gitrepo.cpp` (implement)
- Test: `tests/core/test_gitrepo_logallrefs.cpp` (extend — same suite)

**Interfaces:**
- Produces: `struct gittide::RefTip { std::string oid; std::string name; RefTipKind kind; }`
  with `enum class RefTipKind { Branch, Remote, Tag }`.
- Produces: `Expected<std::vector<RefTip>> GitRepo::refTips() const` — one entry
  per local branch / remote-tracking branch / tag, `name` already shortened
  (`main`, `origin/main`, `v1.0`), `oid` = the commit it resolves to.

- [ ] **Step 1: Add the struct to `core/include/gittide/graph.hpp`** (after `CommitNode`):

```cpp
// A ref tip for the graph's branch/tag chips. name is the short form
// (main, origin/main, v1.0); oid is the commit it peels to.
enum class RefTipKind { Branch, Remote, Tag };
struct RefTip
{
    std::string oid;
    std::string name;
    RefTipKind  kind = RefTipKind::Branch;
};
```

- [ ] **Step 2: Write the failing test** (append to `test_gitrepo_logallrefs.cpp`):

```cpp
TEST_CASE("refTips reports branch and tag labels with short names")
{
    test::TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "1");
    repo.commitAll("base");
    repo.tagHead("v1.0");

    auto git = GitRepo::open(repo.path());
    REQUIRE(git.has_value());
    auto base = git->log(1);
    REQUIRE(base.has_value());
    REQUIRE(git->createBranch("feature", base->front().oid).has_value());

    auto tips = git->refTips();
    REQUIRE(tips.has_value());

    auto find = [&](const std::string& n) -> const RefTip* {
        for (const auto& t : *tips) if (t.name == n) return &t;
        return nullptr;
    };
    const RefTip* master  = find("master");
    const RefTip* feature = find("feature");
    const RefTip* tag     = find("v1.0");
    REQUIRE(master  != nullptr);
    REQUIRE(feature != nullptr);
    REQUIRE(tag     != nullptr);
    REQUIRE(master->kind  == RefTipKind::Branch);
    REQUIRE(tag->kind     == RefTipKind::Tag);
    REQUIRE(feature->oid  == base->front().oid); // feature points at base
}
```

- [ ] **Step 3: Run, verify it fails** (`refTips` undeclared):

`cmake --build build --parallel && ctest --test-dir build -R logallrefs --output-on-failure`
Expected: compile error / FAIL.

- [ ] **Step 4: Declare in `gitrepo.hpp`** (near `branches`):

```cpp
    // Enumerate ref tips (local + remote-tracking branches + tags) with short
    // names, each resolved to the commit oid it points at. For graph chips.
    Expected<std::vector<RefTip>> refTips() const;
```

- [ ] **Step 5: Implement in `gitrepo.cpp`** (include `<git2/refs.h>`):

```cpp
Expected<std::vector<RefTip>> GitRepo::refTips() const
{
    git_reference_iterator* it = nullptr;
    int rc = git_reference_iterator_new(&it, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    std::vector<RefTip> tips;
    git_reference* ref = nullptr;
    while (git_reference_next(&ref, it) == 0)
    {
        const char* name = git_reference_name(ref); // full, e.g. refs/heads/main
        std::string full = name ? name : "";

        // Peel to a commit; skip refs that don't resolve to one (e.g. annotated
        // tag of a tree, symbolic HEAD).
        git_object* obj = nullptr;
        if (git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT) == 0)
        {
            git_oid out;
            git_oid_cpy(&out, git_object_id(obj));
            char hex[GIT_OID_SHA1_HEXSIZE + 1];
            git_oid_tostr(hex, sizeof(hex), &out);
            git_object_free(obj);

            RefTip tip;
            tip.oid = hex;
            if (full.rfind("refs/heads/", 0) == 0)
            {
                tip.kind = RefTipKind::Branch;
                tip.name = full.substr(std::string("refs/heads/").size());
            }
            else if (full.rfind("refs/remotes/", 0) == 0)
            {
                tip.kind = RefTipKind::Remote;
                tip.name = full.substr(std::string("refs/remotes/").size());
                // Skip the synthetic origin/HEAD pointer.
                if (tip.name.size() >= 5 &&
                    tip.name.compare(tip.name.size() - 5, 5, "/HEAD") == 0)
                {
                    git_reference_free(ref);
                    ref = nullptr;
                    continue;
                }
            }
            else if (full.rfind("refs/tags/", 0) == 0)
            {
                tip.kind = RefTipKind::Tag;
                tip.name = full.substr(std::string("refs/tags/").size());
            }
            else
            {
                git_reference_free(ref);
                ref = nullptr;
                continue; // ignore refs/stash, notes, etc.
            }
            tips.push_back(std::move(tip));
        }
        git_reference_free(ref);
        ref = nullptr;
    }
    git_reference_iterator_free(it);
    return tips;
}
```

- [ ] **Step 6: Run, verify it passes.**

`ctest --test-dir build -R logallrefs --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit.**

```bash
git add core/include/gittide/graph.hpp core/include/gittide/gitrepo.hpp \
        core/src/gitrepo.cpp tests/core/test_gitrepo_logallrefs.cpp
git commit -m "feat(core): GitRepo::refTips enumerates branch/tag labels for graph chips"
```

---

## Task 3: `HistoryListModel` gains a `refLabels` role

**Files:**
- Modify: `ui/include/gittide/ui/historylistmodel.hpp`
- Modify: `ui/src/historylistmodel.cpp`
- Test: `tests/ui/test_qml_graph.cpp` (new — created here, grows in Task 5) OR
  fold the assertion into Task 5. To keep this task independently testable, add a
  tiny model unit check in a new `tests/ui/test_history_model_reflabels.cpp`.

**Interfaces:**
- Produces: role `RefLabelsRole` (QML name `refLabels`) → `QStringList` of short
  ref names whose tip is this commit; empty for non-tips and whenever
  `setRefTips` was never called (History model leaves it empty).
- Produces: `void HistoryListModel::setRefTips(const QHash<QString, QStringList>& oidToLabels)`.

- [ ] **Step 1: Write the failing test** `tests/ui/test_history_model_reflabels.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "gittide/graph.hpp"
#include "gittide/ui/historylistmodel.hpp"

using namespace gittide;
using namespace gittide::ui;

TEST_CASE("HistoryListModel exposes refLabels per tip oid")
{
    GraphLayout layout;
    CommitNode n; n.oid = "abc123"; n.summary = "tip";
    layout.rows.push_back(GraphRow{n, false, {}, {}});
    layout.laneCount = 1;

    HistoryListModel m;
    m.setLayout(layout, "");
    QHash<QString, QStringList> tips;
    tips.insert("abc123", QStringList{"main", "v1.0"});
    m.setRefTips(tips);

    const QModelIndex idx = m.index(0, 0);
    const QStringList labels =
        m.data(idx, HistoryListModel::RefLabelsRole).toStringList();
    REQUIRE(labels.contains("main"));
    REQUIRE(labels.contains("v1.0"));
}
```

Register in `tests/CMakeLists.txt` (UI test list).

- [ ] **Step 2: Run, verify it fails** (`RefLabelsRole`/`setRefTips` missing):

`cmake --build build --parallel && ctest --test-dir build -R reflabels --output-on-failure`
Expected: compile error / FAIL.

- [ ] **Step 3: Extend `historylistmodel.hpp`** — add the enum value after
  `LocalBranchNameRole`, declare the setter and member:

```cpp
        LocalBranchNameRole, // short name of a local branch whose tip is this commit; empty otherwise
        RefLabelsRole,       // QStringList of ref names (branch/tag) tipped here; graph chips
    };
```
```cpp
    /// Update the oid → ref-label-list map used by RefLabelsRole (graph chips).
    void setRefTips(const QHash<QString, QStringList>& oidToLabels);
```
```cpp
    QHash<QString, QStringList> m_oidToRefLabels; // tip oid → [branch/tag names]
```

- [ ] **Step 4: Implement in `historylistmodel.cpp`** — setter (mirrors
  `setLocalBranchTips`), `data` case, and `roleNames` entry:

```cpp
void HistoryListModel::setRefTips(const QHash<QString, QStringList>& oidToLabels)
{
    m_oidToRefLabels = oidToLabels;
    if (!m_layout.rows.empty())
        emit dataChanged(index(0, 0),
                         index(static_cast<int>(m_layout.rows.size()) - 1, 0),
                         {RefLabelsRole});
}
```
Add to `data`'s switch:
```cpp
    case RefLabelsRole:
        return m_oidToRefLabels.value(oid);
```
Add to `roleNames`:
```cpp
        {RefLabelsRole, QByteArrayLiteral("refLabels")},
```

- [ ] **Step 5: Run, verify it passes.**

`ctest --test-dir build -R reflabels --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit.**

```bash
git add ui/include/gittide/ui/historylistmodel.hpp ui/src/historylistmodel.cpp \
        tests/ui/test_history_model_reflabels.cpp tests/CMakeLists.txt
git commit -m "feat(ui): HistoryListModel refLabels role for graph branch/tag chips"
```

---

## Task 4: ViewModel `graph` model + controller `refreshGraph`/`refTips` wiring

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp` (declare task + signals)
- Modify: `ui/src/repocontroller.cpp` (implement `refreshGraph`)
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` (graph getter/property/invokable/slots/members)
- Modify: `ui/src/repoviewmodel.cpp` (construct, connect, slots)
- Test: `tests/ui/test_qml_graph.cpp` (new — asserts `repoVm.graph` exists & populates)

**Interfaces:**
- Produces (controller): `QCoro::Task<void> RepoController::refreshGraph(unsigned limit = 1000)`;
  signals `void graphReady(gittide::GraphLayout layout)` and
  `void refTipsReady(QHash<QString, QStringList> oidToLabels)`.
- Produces (viewmodel): `HistoryListModel* RepoViewModel::graph() const`,
  `Q_PROPERTY(... graph READ graph CONSTANT)`, `Q_INVOKABLE void refreshGraph()`.

- [ ] **Step 1: Write the failing test** `tests/ui/test_qml_graph.cpp`. Use the
  existing headless QML harness pattern (copy the include/setup block from
  `tests/ui/test_qml_history.cpp`). Minimal first assertion — the property exists
  and is a model:

```cpp
// Loads a repo with two branches, calls refreshGraph, asserts repoVm.graph
// surfaces commits from both branches (rowCount covers all-refs, not just HEAD).
TEST_CASE("repoVm.graph populates from all refs")
{
    // ... harness boilerplate mirroring test_qml_history.cpp: build a TempRepo
    //     with master + feature commits, construct RepoController+RepoViewModel,
    //     open the repo, co_await refreshGraph (spin the event loop). ...
    REQUIRE(repoVm.graph() != nullptr);
    REQUIRE(repoVm.graph()->rowCount() >= 3); // base + feature + master
}
```

> If the existing UI harness drives via QML only, assert through QML instead:
> load a tiny inline component reading `repoVm.graph.rowCount` after
> `repoVm.refreshGraph()`. Follow whatever `test_qml_history.cpp` already does.

Register `test_qml_graph.cpp` in `tests/CMakeLists.txt` (UI list).

- [ ] **Step 2: Run, verify it fails** (`graph()` missing):

`cmake --build build --parallel && ctest --test-dir build -R qml_graph --output-on-failure`
Expected: compile error / FAIL.

- [ ] **Step 3: Controller — declare** in `repocontroller.hpp` (near `refreshHistory`
  and the `historyReady` signal):

```cpp
    QCoro::Task<void> refreshGraph(unsigned limit = 1000);
```
```cpp
    void graphReady(gittide::GraphLayout layout);
    void refTipsReady(QHash<QString, QStringList> oidToLabels);
```

- [ ] **Step 4: Controller — implement** in `repocontroller.cpp` (mirror
  `refreshHistory`, ~line 213):

```cpp
QCoro::Task<void> RepoController::refreshGraph(unsigned limit)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    auto result = co_await m_repo->logAllRefs(limit);
    if (!self)
        co_return;
    if (!result)
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        co_return;
    }
    emit graphReady(gittide::GraphBuilder::build(std::move(*result)));

    auto tips = co_await m_repo->refTips();
    if (!self || !tips)
        co_return;
    QHash<QString, QStringList> map;
    for (const auto& t : *tips)
        map[QString::fromStdString(t.oid)] << QString::fromStdString(t.name);
    emit refTipsReady(std::move(map));
}
```

> `co_await m_repo->logAllRefs(...)` requires the async-repo bridge to expose
> `logAllRefs`/`refTips`. If `m_repo` is an `AsyncRepo` wrapper rather than a raw
> `GitRepo`, add thin forwarding methods there mirroring the existing `log`
> forwarder (same file as the other `m_repo->...` calls). Check how `log` is
> bridged and copy that exact shape.

- [ ] **Step 5: ViewModel — declare** in `repoviewmodel.hpp`:

Property (next to the `history` one, line 44):
```cpp
    Q_PROPERTY(gittide::ui::HistoryListModel* graph READ graph CONSTANT)
```
Getter (next to `history()`):
```cpp
    HistoryListModel* graph() const;
```
Invokable (next to `refreshHistory`):
```cpp
    Q_INVOKABLE void refreshGraph();
```
Slots (private, next to `onHistory`):
```cpp
    void onGraph(const gittide::GraphLayout& layout);
    void onRefTips(const QHash<QString, QStringList>& oidToLabels);
```
Member (next to `m_history`):
```cpp
    HistoryListModel* m_graph = nullptr;
```

- [ ] **Step 6: ViewModel — implement** in `repoviewmodel.cpp`:

Construct (next to `, m_history(new HistoryListModel(this))`, line 27):
```cpp
    , m_graph(new HistoryListModel(this))
```
Connect in the constructor body (near the other `connect(m_controller, ...)`):
```cpp
    connect(m_controller, &RepoController::graphReady,   this, &RepoViewModel::onGraph);
    connect(m_controller, &RepoController::refTipsReady, this, &RepoViewModel::onRefTips);
```
Getter + invokable + slots:
```cpp
HistoryListModel* RepoViewModel::graph() const { return m_graph; }

void RepoViewModel::refreshGraph()
{
    QCoro::connect(m_controller->refreshGraph(), this, [] {});
}

void RepoViewModel::onGraph(const gittide::GraphLayout& layout)
{
    // Graph applies immediately (no head/history reconciliation): the HEAD marker
    // reuses the last-known head oid; chips arrive separately via onRefTips.
    m_graph->setLayout(layout, m_headOid);
}

void RepoViewModel::onRefTips(const QHash<QString, QStringList>& oidToLabels)
{
    m_graph->setRefTips(oidToLabels);
}
```
In `close()`/reset (where `m_history->setLayout({}, {})` is called, ~line 154),
also clear the graph:
```cpp
    m_graph->setLayout({}, {});
```

- [ ] **Step 7: Run, verify it passes.**

`cmake --build build --parallel && ctest --test-dir build -R qml_graph --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit.**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp \
        ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp \
        tests/ui/test_qml_graph.cpp tests/CMakeLists.txt
git commit -m "feat(ui): RepoViewModel.graph model + refreshGraph/refTips wiring"
```

---

## Task 5: `GraphPane.qml` + third "Graph" tab

**Files:**
- Create: `ui/qml/GraphPane.qml`
- Modify: `ui/qml/WorkingPane.qml` (3rd tab, 3rd stack child, Ctrl+3, focus chain)
- Modify: `ui/CMakeLists.txt` if QML files are listed there (check; many Qt6 QML
  setups auto-glob — only add if `HistoryPane.qml` is explicitly listed)
- Test: `tests/ui/test_qml_graph.cpp` (extend — assert tab + selection→detail)

**Interfaces:**
- Consumes: `repoVm.graph` (Task 4), `RefLabelsRole` chips (Task 3),
  `GraphColumn`, `CommitDetail`, `CommitContextMenu` (existing).
- Produces: `GraphPane` with `objectName: "graphTabBody"`, `takeFocus()`,
  `takeFocusLast()`, signals `tabNext()`/`tabPrev()` (mirrors `HistoryPane`).

- [ ] **Step 1: Write the failing test** (extend `test_qml_graph.cpp`): assert the
  Graph tab body exists by object name and that selecting row 0 populates the
  commit detail. Mirror the selection assertions in `test_qml_history.cpp`:

```cpp
TEST_CASE("Graph tab exists and selection drives commit detail")
{
    // ... load Main/WorkingPane via the QML harness, open a repo ...
    QObject* graphBody = root->findChild<QObject*>("graphTabBody");
    REQUIRE(graphBody != nullptr);
    // switch to Graph tab (Ctrl+3 or set tabs.currentIndex = 2), click row 0,
    // assert repoVm.selectedCommit becomes non-empty.
}
```

- [ ] **Step 2: Run, verify it fails** (`graphTabBody` not found):

`ctest --test-dir build -R qml_graph --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Create `ui/qml/GraphPane.qml`** — a trimmed HistoryPane: graph +
  chips + detail, selection via `TapHandler`, no drag, no grips:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GitTide 1.0

RowLayout {
    id: graphPane
    spacing: 0

    function takeFocus() { graphList.forceActiveFocus() }
    function takeFocusLast() { graphDetail.takeFocus() }
    signal tabNext()
    signal tabPrev()

    CommitContextMenu {
        id: graphMenu
        onCopySha:           if (repoVm) repoVm.copyToClipboard(graphMenu.oid)
        onNewBranchFromHere: graphNewBranchDialog.openFromCommit(graphMenu.oid)
        onCheckoutCommit:    if (repoVm) repoVm.checkoutCommit(graphMenu.oid)
        onMerge:             if (repoVm) repoVm.startMerge(graphMenu.localBranchName)
        // reword / squash / edit-history / undo intentionally NOT wired here —
        // the graph is cross-branch and read-only w.r.t. history editing.
    }
    NewBranchDialog { id: graphNewBranchDialog }

    Item {
        Layout.preferredWidth: 460
        Layout.fillHeight: true

        ListView {
            id: graphList
            objectName: "graphList"
            anchors.fill: parent
            clip: true
            model: repoVm ? repoVm.graph : null
            ScrollBar.vertical: AppScrollBar {}
            WheelScroller {}
            activeFocusOnTab: true

            property int selectedRow: -1
            Keys.onUpPressed:   if (currentIndex > 0)        { currentIndex--; selectRow(currentIndex) }
            Keys.onDownPressed: if (currentIndex < count - 1) { currentIndex++; selectRow(currentIndex) }
            function selectRow(i) { selectedRow = i; if (repoVm) repoVm.selectCommitAtRow(i) }

            delegate: Rectangle {
                width: ListView.view.width
                height: 48
                color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        graphList.forceActiveFocus()
                        graphList.currentIndex = index
                        graphList.selectRow(index)
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        graphList.currentIndex = index
                        graphList.selectRow(index)
                        graphMenu.oid             = model.oid
                        graphMenu.shortOid        = model.shortOid
                        graphMenu.localBranchName = model.localBranchName ?? ""
                        graphMenu.isHead          = model.isHead
                        graphMenu.selectionCount  = 1
                        graphMenu.popup()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 12
                    spacing: 8

                    GraphColumn {
                        Layout.fillHeight: true
                        Layout.preferredWidth: implicitWidth
                        graphRow: model.graphRow
                        laneColors: theme.laneColors
                        headColor: theme.head
                        laneCount: repoVm && repoVm.graph ? repoVm.graph.laneCount : 1
                        head: model.isHead
                    }

                    // Branch/tag chips for ref tips.
                    Repeater {
                        model: (typeof refLabels !== "undefined" && refLabels) ? refLabels : []
                        delegate: Rectangle {
                            radius: 3
                            color: theme.surfaceRaised
                            border.width: 1
                            border.color: theme.border
                            implicitHeight: 16
                            implicitWidth: chipLabel.implicitWidth + 10
                            Layout.alignment: Qt.AlignVCenter
                            Label {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: theme.textSecondary
                                font.pixelSize: 10
                            }
                        }
                    }

                    Avatar { name: model.author; Layout.alignment: Qt.AlignVCenter }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: model.summary
                            color: theme.textPrimary
                            font.pixelSize: 13
                        }
                        RowLayout {
                            spacing: 8
                            Label { text: model.author;   color: theme.textMuted; font.pixelSize: 11 }
                            Label { text: model.shortOid;  color: theme.textMuted; font.family: "monospace"; font.pixelSize: 11 }
                            Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignRight
                                text: model.date; color: theme.textMuted; font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: theme.border }

    CommitDetail {
        id: graphDetail
        objectName: "graphCommitDetail"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    Connections {
        target: graphDetail
        function onTabBackward() { graphList.forceActiveFocus() }
        function onTabForward() { graphPane.tabNext() }
    }
}
```

> Verify `CommitContextMenu`'s signal/property names against
> `ui/qml/CommitContextMenu.qml` and adjust the wiring if any differ. `Avatar`,
> `AppScrollBar`, `WheelScroller`, `NewBranchDialog`, `CommitDetail` are existing
> components used by `HistoryPane`.

- [ ] **Step 4: Wire the third tab in `WorkingPane.qml`.**

Add after the `MainTab { text: "History" }` line (104):
```qml
                MainTab { text: "Graph" }
```
Add a third `StackLayout` child after the `HistoryPane { ... }` block (166),
and refresh the graph lazily on first activation:
```qml
            // Index 2: Graph — full all-refs git graph + read-only commit detail.
            GraphPane {
                id: graphTabBody
                objectName: "graphTabBody"
                onTabNext: workingPane.focusSidebar()
                onTabPrev: workingPane.focusSidebar()
            }
```
Update `takeFocus`/`takeFocusLast` for index 2:
```qml
    function takeFocus() {
        if (tabs.currentIndex === 0) changesTabBody.takeFocus()
        else if (tabs.currentIndex === 1) historyTabBody.takeFocus()
        else graphTabBody.takeFocus()
    }
    function takeFocusLast() {
        if (tabs.currentIndex === 0) changesTabBody.takeFocusLast()
        else if (tabs.currentIndex === 1) historyTabBody.takeFocusLast()
        else graphTabBody.takeFocusLast()
    }
```
Refresh the graph when the Graph tab becomes current (add a handler on `tabs`):
```qml
                onCurrentIndexChanged: {
                    if (currentIndex === 2 && repoVm) repoVm.refreshGraph()
                }
```
Add the `Ctrl+3` shortcut after the `Ctrl+2` block (191):
```qml
    Shortcut {
        sequence: "Ctrl+3"
        enabled: repoVm !== null && repoVm.repoOpen
        onActivated: {
            tabs.currentIndex = 2
            graphTabBody.takeFocus()
        }
    }
```

- [ ] **Step 5: Run, verify it passes.**

`cmake --build build --parallel && ctest --test-dir build -R qml_graph --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit.**

```bash
git add ui/qml/GraphPane.qml ui/qml/WorkingPane.qml ui/CMakeLists.txt \
        tests/ui/test_qml_graph.cpp
git commit -m "feat(ui): Graph tab with full all-refs git graph + branch/tag chips"
```

---

## Task 6: History — strip graph column + grips, fix the drag

**Files:**
- Modify: `ui/qml/HistoryPane.qml`
- Test: `tests/ui/test_qml_history.cpp` (update: assert no `GraphColumn`/`reorderGrip`;
  assert drag arms + drops)

**Interfaces:**
- Consumes: existing `dropLogic` (`dropZoneAt`/`updateDropTarget`/`performDrop`),
  `rowDrag` `DragHandler`, `holdTimer`. All kept.
- Produces: a History delegate whose left-click selection is driven by
  `TapHandler` (cooperates with `DragHandler`), with no graph column and no grip.

- [ ] **Step 1: Update the test** in `test_qml_history.cpp`. Two changes:
  (a) the existing assertion that finds `reorderGrip` becomes an assertion that it
  is **absent**; (b) add a drag test that arms and drops. Mirror how the file
  currently drives the delegate (it already references `dropZoneAt`):

```cpp
// Grip removed: no child named "reorderGrip" remains in a history row.
REQUIRE(historyRow->findChild<QObject*>("reorderGrip") == nullptr);
```
```cpp
// Drag now arms: simulate a press-hold (fire holdTimer) + centroid move into a
// target row + release, and assert performDrop routed (e.g. reorderConfirm opened
// or squashCommitInto invoked). Use the same QML-driving approach already used
// for dropZoneAt in this file (invoke the JS helpers / set dragArmed and call
// the handler paths), since true pointer synthesis is awkward headless.
```

> If headless pointer synthesis isn't feasible in this harness, assert the
> structural fix instead: the delegate root has **no** `MouseArea` child and
> **has** a `TapHandler`, plus a direct call into `dropLogic.performDrop(...)`
> still routes correctly. The key regression guard is "the grab-stealing
> `MouseArea` is gone."

- [ ] **Step 2: Run, verify it fails** (grip still present / MouseArea still present):

`cmake --build build --parallel && ctest --test-dir build -R qml_history --output-on-failure`
Expected: FAIL.

- [ ] **Step 3: Remove the `GraphColumn` block** (current lines 223–231) from the
  delegate's `RowLayout`. The first child becomes `Avatar`.

- [ ] **Step 4: Remove the `reorderGrip` `Label` block** (current lines 274–287).

- [ ] **Step 5: Replace the `MouseArea` (lines 170–215) with two `TapHandler`s**
  that preserve the exact selection semantics and cooperate with `rowDrag`:

```qml
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        historyList.forceActiveFocus()
                        const mods = point.modifiers
                        if (mods & Qt.ShiftModifier) {
                            var anchor = historyList.currentIndex
                            var lo = Math.max(0, Math.min(anchor, index))
                            var hi = Math.max(anchor, index)
                            var range = []
                            for (var r = lo; r <= hi; ++r) range.push(r)
                            historyList.selectedRows = range
                            historyList.currentIndex = index
                        } else if (mods & Qt.ControlModifier) {
                            var set = historyList.selectedRows.slice()
                            var at = set.indexOf(index)
                            if (at >= 0) set.splice(at, 1); else set.push(index)
                            if (set.indexOf(historyList.currentIndex) < 0)
                                set.push(historyList.currentIndex)
                            historyList.selectedRows = set
                            historyList.currentIndex = index
                        } else {
                            historyList.selectedRows = [index]
                            historyList.currentIndex = index
                        }
                        if (repoVm) historyList.applySelection()
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        historyList.forceActiveFocus()
                        var inMulti = historyList.selectedRows.length >= 2
                                      && historyList.selectedRows.indexOf(index) >= 0
                        if (!inMulti) {
                            historyList.currentIndex = index
                            historyList.selectedRows = [index]
                        }
                        commitMenu.oid             = model.oid
                        commitMenu.shortOid        = model.shortOid
                        commitMenu.localBranchName = model.localBranchName ?? ""
                        commitMenu.isHead          = model.isHead
                        commitMenu.selectionCount  = historyList.selectedRows.length
                        commitMenu.popup()
                    }
                }
```

Keep `rowDrag` (`DragHandler`), `holdTimer`, the drop indicators, and
`dropLogic` exactly as they are. With the `MouseArea` gone, `rowDrag` wins the
grab after the 250 ms hold and the drag arms.

- [ ] **Step 6: Run, verify it passes.**

`cmake --build build --parallel && ctest --test-dir build -R qml_history --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Manual smoke (per [run] skill or `cmake --build build --target run`).**
  Open a repo, History tab: confirm a click selects, Shift/Ctrl multi-select work,
  right-click opens the menu, and **press-hold-drag a row onto another row**
  reorders/squashes (the bug is gone). Graph tab: confirm the full graph renders
  with branch/tag chips and selection shows the commit detail.

- [ ] **Step 8: Commit.**

```bash
git add ui/qml/HistoryPane.qml tests/ui/test_qml_history.cpp
git commit -m "fix(ui): history drag works (TapHandler over MouseArea); drop graph col + grips"
```

---

## Task 7: Docs — spec truth + plan outcome

**Files:**
- Modify: `docs/spec/product/` history/graph sections (and the design doc status)
- Modify: `docs/plans/index.md` (add the Plan 25 row)
- Modify: this plan's **Status** → `done` and fill **Outcome**

- [ ] **Step 1: Update the living spec.** In the product spec describing the
  History view, record: the graph now lives in its own **Graph tab** (all refs:
  heads/remotes/tags, with branch/tag chips, read-only select+detail+menu); the
  History list no longer renders a graph column or drag grips; History drag uses
  `TapHandler` + `DragHandler`. Flip the design doc's **Status** to `shipped`.

- [ ] **Step 2: Add the Plan 25 row** to `docs/plans/index.md` (mirror the Plan 24
  row format, link this file).

- [ ] **Step 3: Run the full suite** to confirm nothing regressed:

`cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: all PASS.

- [ ] **Step 4: Fill this plan's Outcome, set Status `done`, commit.**

```bash
git add docs/
git commit -m "docs: close out Plan 25 (history graph tab) — spec + plan outcome"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: <which `spec/` sections now describe this>.
> - Code: <main files/types — `logAllRefs`, `refTips`, `RepoViewModel::graph`,
>   `GraphPane.qml`, History `TapHandler` fix>.
