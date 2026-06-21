# Plan 8 — Branch management

> **For agentic workers:** implement this plan task-by-task, **test-first**. Each
> task's steps use checkbox (`- [x]`) syntax; tick them as you go. REQUIRED
> SUB-SKILL: `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans`.

| | |
|--|--|
| **Date** | 2026-06-18 |
| **Status** | `done` |
| **Spec** | [`spec/product` §Branches](../spec/product/product.md#branches) · [`spec/engineering` §Branch operations](../spec/engineering/engineering.md#branch-operations--the-refresh-cascade) · [`spec/design` §Components](../spec/design/design.md#components) · [D21](../decisions.md) |
| **Depends on** | Plan 3a (core git ops), Plan 3b (AsyncRepo/RepoController), Plan 5b (HistoryView) |

**Goal:** Let the user list / create / switch / delete / rename a repo's local
branches (plus detached-commit checkout) from inside GitTide, with safe-by-default
checkout (stash + re-apply) that never clobbers uncommitted work.

**Architecture:** Branch ops are pure git → new methods on `core/GitRepo` over
libgit2 `git_branch_*` / `git_checkout_tree` / `git_repository_set_head[_detached]`
/ `git_stash_*`, returning `Expected<T>`. `AsyncRepo` wraps each as a `QCoro::Task`;
`RepoController` exposes slots + signals and drives the refresh cascade. UI adds a
`BranchBar` above the tabs, three small dialogs, and a History-graph context menu.

**Tech stack:** C++23, libgit2, Qt 6 Widgets, QtConcurrent + QCoro, Catch2 (core),
Qt Test (ui).

## Global constraints

- Invariants ([`engineering`](../spec/engineering/engineering.md#cross-cutting-invariants)):
  **no Qt in `core/`**; libgit2 stays PRIVATE to `core/`; core speaks `std` +
  `Expected<T>`, no exceptions across layers; one owner per `GitRepo`; paths via
  `toGitPath()` (`generic_u8string()`), never build git command strings.
- **Safe-switch invariant (D21):** a checkout that would overwrite a dirty tree
  must stash → checkout → re-apply; a pop conflict stops, keeps the stash, returns
  an error. Use `GIT_CHECKOUT_SAFE`, never `FORCE`.
- **Colour from tokens only** (D18); state never colour-alone (D19).
- New `core/` sources → `core/CMakeLists.txt`; new `ui/` sources →
  `ui/CMakeLists.txt`; new tests → the matching list in `tests/CMakeLists.txt`
  (core in `gittide_core_tests`, ui in `gittide_ui_test_sources`).
- Keep green: all existing core + ui tests. No new compiler/Qt warnings.
- Commit style: `feat(core|ui): …` / `test(...)`, imperative subject; end with the
  Co-Authored-By trailer.

---

## Task 1: Core — branch info types + `branches()` + `head()`

**Files:**
- Create: `core/include/gittide/branchinfo.hpp`
- Modify: `core/include/gittide/gitrepo.hpp` (declare `branches()`, `head()`)
- Modify: `core/src/gitrepo.cpp` (implement; add `#include <git2/branch.h>`)
- Modify: `tests/CMakeLists.txt` (add `test_git_repo_branches.cpp`)
- Test: `tests/test_git_repo_branches.cpp`

**Interfaces — Produces:**
```cpp
// branchinfo.hpp
namespace gittide {
struct BranchInfo { std::string name; bool isHead = false; };
struct HeadState  { std::string branch; std::string oid; bool detached = false; bool unborn = false; };
}
// gitrepo.hpp (public)
Expected<std::vector<BranchInfo>> branches() const;
Expected<HeadState>               head() const;
```

- [x] **Step 1: Write the failing test**

```cpp
// tests/test_git_repo_branches.cpp
#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;

namespace {
bool has(const std::vector<gittide::BranchInfo>& v, const std::string& n)
{
    return std::any_of(v.begin(), v.end(), [&](const auto& b) { return b.name == n; });
}
}

TEST_CASE("branches lists the default branch and marks HEAD", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto list = repo->branches();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 1);
    REQUIRE((*list)[0].isHead);

    auto h = repo->head();
    REQUIRE(h.has_value());
    REQUIRE_FALSE(h->detached);
    REQUIRE(h->branch == (*list)[0].name);
    REQUIRE(h->oid.size() == 40);
}
```

- [x] **Step 2: Run it — expect FAIL** (`branches`/`head` undeclared).
  Run: `ctest --test-dir build -R 'branches' --output-on-failure`

- [x] **Step 3: Add the types + declarations.** Create `branchinfo.hpp` with the
  structs above; `#include "gittide/branchinfo.hpp"` in `gitrepo.hpp` and declare
  the two methods near `log()`.

- [x] **Step 4: Implement in `gitrepo.cpp`** (add `#include <git2/branch.h>`):

```cpp
Expected<std::vector<BranchInfo>> GitRepo::branches() const
{
    git_branch_iterator* it = nullptr;
    int rc = git_branch_iterator_new(&it, m_repo, GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_branch_iterator, decltype(&git_branch_iterator_free)> guard(it, git_branch_iterator_free);

    std::vector<BranchInfo> result;
    git_reference* ref    = nullptr;
    git_branch_t br_type;
    while ((rc = git_branch_next(&ref, &br_type, it)) == 0)
    {
        std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);
        const char* name = nullptr;
        if (git_branch_name(&name, ref) == 0 && name)
            result.push_back(BranchInfo{name, git_branch_is_head(ref) == 1});
    }
    if (rc != GIT_ITEROVER)
        return std::unexpected(lastGitError(rc));
    return result;
}

Expected<HeadState> GitRepo::head() const
{
    HeadState st;
    if (git_repository_head_unborn(m_repo) == 1)
    {
        st.unborn = true;
        return st;
    }
    git_reference* ref = nullptr;
    int rc             = git_repository_head(&ref, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> guard(ref, git_reference_free);

    git_oid oid;
    if (git_reference_name_to_id(&oid, m_repo, "HEAD") == 0)
    {
        char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
        git_oid_tostr(hex, sizeof(hex), &oid);
        st.oid = hex;
    }
    st.detached = git_repository_head_detached(m_repo) == 1;
    if (!st.detached)
    {
        const char* sh = git_reference_shorthand(ref);
        st.branch      = sh ? sh : "";
    }
    return st;
}
```

- [x] **Step 5: Run — expect PASS.** Then add `test_git_repo_branches.cpp` to
  `gittide_core_tests` in `tests/CMakeLists.txt`, reconfigure, run the full core
  suite green.
  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`

- [x] **Step 6: Commit.**
  `git commit -am "feat(core): list local branches and resolve HEAD state"`

---

## Task 2: Core — `createBranch()` with name validation

**Files:** Modify `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`;
Test `tests/test_git_repo_branches.cpp` (append cases).

**Interfaces — Produces:**
```cpp
// empty fromOid = from current HEAD. Creation only — does NOT switch; the
// create-then-switch orchestration lives in RepoController (Task 7), which keeps
// this method free of the checkout forward-dependency.
Expected<void> createBranch(std::string name, std::string fromOid);
```
**Consumes:** `branches()`, `head()` (Task 1).

- [x] **Step 1: Write the failing tests.**

```cpp
TEST_CASE("createBranch from HEAD makes a listable branch", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    REQUIRE(repo->createBranch("feature", "").has_value());
    auto list = repo->branches();
    REQUIRE(list.has_value());
    REQUIRE(has(*list, "feature"));
    // not switched: HEAD unchanged
    REQUIRE_FALSE(std::any_of(list->begin(), list->end(),
                              [](const auto& b) { return b.isHead && b.name == "feature"; }));
}

TEST_CASE("createBranch rejects an invalid name", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto r = repo->createBranch("bad name~^:", "");
    REQUIRE_FALSE(r.has_value());
}
```

- [x] **Step 2: Run — expect FAIL** (undeclared).

- [x] **Step 3: Declare + implement.** Add a private helper to resolve a target
  commit, then:

```cpp
Expected<void> GitRepo::createBranch(std::string name, std::string fromOid)
{
    int valid = 0;
    if (git_branch_name_is_valid(&valid, name.c_str()) < 0 || valid == 0)
        return std::unexpected(GitError{-1, "invalid branch name"});

    git_commit* target = nullptr;
    int rc;
    if (fromOid.empty())
    {
        git_oid head_oid;
        rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        rc = git_commit_lookup(&target, m_repo, &head_oid);
    }
    else
    {
        git_oid oid;
        rc = git_oid_fromstr(&oid, fromOid.c_str());
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        rc = git_commit_lookup(&target, m_repo, &oid);
    }
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> target_guard(target, git_commit_free);

    git_reference* new_ref = nullptr;
    rc = git_branch_create(&new_ref, m_repo, name.c_str(), target, /*force=*/0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_reference_free(new_ref);
    return {};
}
```

- [x] **Step 4: Run — expect PASS** (both new cases).

- [x] **Step 5: Commit.**
  `git commit -am "feat(core): create a local branch from HEAD or a commit"`

---

## Task 3: Core — safe-switch helper + `checkoutBranch()`

**Files:** Modify `core/include/gittide/gitrepo.hpp` (declare `checkoutBranch`,
private `safeSwitch`), `core/src/gitrepo.cpp` (add `#include <git2/stash.h>`,
`#include <git2/checkout.h>`); Test `tests/test_git_repo_checkout.cpp` (new file,
add to CMake).

**Interfaces — Produces:**
```cpp
Expected<void> checkoutBranch(std::string name);
// private:
Expected<void> safeSwitch(const git_oid& targetCommit, const std::string& refToSet /*"" => detached*/);
```
**Consumes:** `status()` (existing), `head()` (Task 1).

- [x] **Step 1: Write the failing tests** (clean switch; dirty-follow; pop-conflict):

```cpp
// tests/test_git_repo_checkout.cpp
#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using gittide::GitRepo;
namespace {
std::string read_file(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}

TEST_CASE("checkoutBranch switches a clean tree", "[checkout]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());

    REQUIRE(repo->checkoutBranch("feature").has_value());
    auto h = repo->head();
    REQUIRE(h.has_value());
    REQUIRE(h->branch == "feature");
}

TEST_CASE("checkoutBranch carries uncommitted changes to the target", "[checkout]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());

    tmp.writeFile("a.txt", "dirty\n"); // uncommitted edit
    REQUIRE(repo->checkoutBranch("feature").has_value());

    auto h = repo->head();
    REQUIRE(h->branch == "feature");
    REQUIRE(read_file(tmp.path() / "a.txt") == "dirty\n"); // change followed
}
```

- [x] **Step 2: Run — expect FAIL** (undeclared / link error).

- [x] **Step 3: Implement `safeSwitch` + `checkoutBranch`:**

```cpp
Expected<void> GitRepo::safeSwitch(const git_oid& targetCommit, const std::string& refToSet)
{
    auto st = status();
    if (!st)
        return std::unexpected(st.error());
    const bool dirty = !st->empty();

    git_oid stash_oid;
    bool stashed = false;
    if (dirty)
    {
        git_signature* sig = nullptr;
        if (git_signature_default(&sig, m_repo) < 0)
            git_signature_now(&sig, "GitTide", "gittide@localhost");
        int rc = git_stash_save(&stash_oid, m_repo, sig, "gittide: auto-stash on switch",
                                GIT_STASH_INCLUDE_UNTRACKED);
        git_signature_free(sig);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        stashed = true;
    }

    git_commit* commit = nullptr;
    int rc             = git_commit_lookup(&commit, m_repo, &targetCommit);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy    = GIT_CHECKOUT_SAFE;
    rc = git_checkout_tree(m_repo, reinterpret_cast<const git_object*>(commit), &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    if (refToSet.empty())
        rc = git_repository_set_head_detached(m_repo, &targetCommit);
    else
        rc = git_repository_set_head(m_repo, refToSet.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    if (stashed)
    {
        git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
        rc = git_stash_pop(m_repo, 0, &aopts);
        if (rc == GIT_EMERGECONFLICT || rc < 0)
            return std::unexpected(GitError{rc,
                "Switched branch, but your changes conflict and are kept in the stash"});
    }
    return {};
}

Expected<void> GitRepo::checkoutBranch(std::string name)
{
    git_reference* ref = nullptr;
    int rc             = git_branch_lookup(&ref, m_repo, name.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);

    git_oid oid;
    rc = git_reference_name_to_id(&oid, m_repo, git_reference_name(ref));
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    return safeSwitch(oid, std::string("refs/heads/") + name);
}
```

- [x] **Step 4: Run — expect PASS** (clean + dirty-follow). Add the pop-conflict
  case if you can construct competing edits on `a.txt` in both branches; assert the
  call returns an error and `git stash list` still has an entry (open a second
  `status()` to confirm HEAD moved). If reliably constructing a pop conflict in a
  unit test proves flaky, cover it at the safeSwitch level with a targeted fixture
  and **log the limitation in the test file** rather than silently dropping it.

- [x] **Step 5:** Add `test_git_repo_checkout.cpp` to `gittide_core_tests`; build;
  run full core suite green.

- [x] **Step 6: Commit.**
  `git commit -am "feat(core): safe branch checkout with stash-and-reapply (D21)"`

---

## Task 4: Core — `checkoutCommit()` (detached HEAD)

**Files:** Modify `gitrepo.hpp`, `gitrepo.cpp`; Test append to
`tests/test_git_repo_checkout.cpp`.

**Interfaces — Produces:** `Expected<void> checkoutCommit(std::string oid);`
**Consumes:** `safeSwitch` (Task 3), `head()` (Task 1).

- [x] **Step 1: Write the failing test.**

```cpp
TEST_CASE("checkoutCommit yields a detached HEAD and reattaches", "[checkout]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    const std::string first = repo->head()->oid;

    tmp.writeFile("a.txt", "two\n");
    tmp.commitAll("c2");
    REQUIRE(repo->checkoutCommit(first).has_value());

    auto h = repo->head();
    REQUIRE(h.has_value());
    REQUIRE(h->detached);
    REQUIRE(h->oid == first);

    auto branch = repo->branches();
    REQUIRE(branch->size() == 1);
    REQUIRE(repo->checkoutBranch((*branch)[0].name).has_value());
    REQUIRE_FALSE(repo->head()->detached); // reattached
}
```

- [x] **Step 2: Run — expect FAIL.**

- [x] **Step 3: Implement.**

```cpp
Expected<void> GitRepo::checkoutCommit(std::string oid)
{
    git_oid target;
    int rc = git_oid_fromstr(&target, oid.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return safeSwitch(target, /*refToSet=*/"");
}
```

- [x] **Step 4: Run — expect PASS.**

- [x] **Step 5: Commit.**
  `git commit -am "feat(core): checkout a bare commit (detached HEAD)"`

---

## Task 5: Core — `deleteBranch()` (merge guard) + `renameBranch()`

**Files:** Modify `gitrepo.hpp`, `gitrepo.cpp`; Test append to
`tests/test_git_repo_branches.cpp`.

**Interfaces — Produces:**
```cpp
Expected<void> deleteBranch(std::string name, bool force);   // force = allow unmerged
Expected<void> renameBranch(std::string oldName, std::string newName, bool force);
```

- [x] **Step 1: Write the failing tests** (delete merged ok; delete current
  blocked; delete unmerged needs force; rename; rename validates name):

```cpp
TEST_CASE("deleteBranch removes a merged branch but blocks the current one", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    const std::string cur = repo->head()->branch;

    REQUIRE(repo->createBranch("merged", "").has_value()); // same tip => merged
    REQUIRE(repo->deleteBranch("merged", /*force=*/false).has_value());
    REQUIRE_FALSE(has(*repo->branches(), "merged"));

    REQUIRE_FALSE(repo->deleteBranch(cur, false).has_value()); // current is blocked
}

TEST_CASE("renameBranch renames and rejects invalid names", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("old", "").has_value());

    REQUIRE(repo->renameBranch("old", "new", false).has_value());
    REQUIRE(has(*repo->branches(), "new"));
    REQUIRE_FALSE(has(*repo->branches(), "old"));

    REQUIRE_FALSE(repo->renameBranch("new", "bad~name", false).has_value());
}
```

- [x] **Step 2: Run — expect FAIL.**

- [x] **Step 3: Implement** (add `#include <git2/graph.h>`):

```cpp
Expected<void> GitRepo::deleteBranch(std::string name, bool force)
{
    git_reference* ref = nullptr;
    int rc             = git_branch_lookup(&ref, m_repo, name.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> guard(ref, git_reference_free);

    if (git_branch_is_head(ref) == 1)
        return std::unexpected(GitError{-1, "cannot delete the current branch"});

    if (!force)
    {
        git_oid branch_oid, head_oid;
        if (git_reference_name_to_id(&branch_oid, m_repo, git_reference_name(ref)) == 0
            && git_reference_name_to_id(&head_oid, m_repo, "HEAD") == 0)
        {
            // merged == branch tip is an ancestor of (or equal to) HEAD.
            int merged = git_oid_equal(&branch_oid, &head_oid)
                         || git_graph_descendant_of(m_repo, &head_oid, &branch_oid) == 1;
            if (!merged)
                return std::unexpected(GitError{-1, "branch is not fully merged"});
        }
    }

    rc = git_branch_delete(ref);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}

Expected<void> GitRepo::renameBranch(std::string oldName, std::string newName, bool force)
{
    int valid = 0;
    if (git_branch_name_is_valid(&valid, newName.c_str()) < 0 || valid == 0)
        return std::unexpected(GitError{-1, "invalid branch name"});

    git_reference* ref = nullptr;
    int rc             = git_branch_lookup(&ref, m_repo, oldName.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> guard(ref, git_reference_free);

    git_reference* moved = nullptr;
    rc = git_branch_move(&moved, ref, newName.c_str(), force ? 1 : 0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_reference_free(moved);
    return {};
}
```

- [x] **Step 4: Run — expect PASS.**

- [x] **Step 5: Commit.**
  `git commit -am "feat(core): delete (merge-guarded) and rename local branches"`

---

## Task 6: Async — wrap the seven branch ops in `AsyncRepo`

**Files:** Modify `ui/include/gittide/ui/asyncrepo.hpp`, `ui/src/asyncrepo.cpp`;
Test `tests/ui/test_async_repo.cpp` (append), register in `gittide_ui_test_sources`
if not already listed (it is).

**Interfaces — Produces:**
```cpp
QCoro::Task<gittide::Expected<std::vector<gittide::BranchInfo>>> branches();
QCoro::Task<gittide::Expected<gittide::HeadState>>               head();
QCoro::Task<gittide::Expected<void>> createBranch(QString name, QString fromOid);
QCoro::Task<gittide::Expected<void>> checkoutBranch(QString name);
QCoro::Task<gittide::Expected<void>> checkoutCommit(QString oid);
QCoro::Task<gittide::Expected<void>> deleteBranch(QString name, bool force);
QCoro::Task<gittide::Expected<void>> renameBranch(QString oldName, QString newName, bool force);
```

- [x] **Step 1: Write the failing test.**

```cpp
// in tests/ui/test_async_repo.cpp — add a slot
void branches_lists_and_creates()
{
    const auto dir = make_repo_with_commit(); // existing helper in this file
    auto repo      = gittide::ui::AsyncRepo::open(dir);
    QVERIFY(repo.has_value());

    auto created = QCoro::waitFor(repo->createBranch(QStringLiteral("feature"), QString()));
    QVERIFY(created.has_value());
    auto list = QCoro::waitFor(repo->branches());
    QVERIFY(list.has_value());
    QVERIFY(list->size() == 2);
    std::filesystem::remove_all(dir);
}
```
> Use whatever repo-with-commit helper `test_async_repo.cpp` already defines; if
> none, build one inline with `gittide::test::TempRepo`-style libgit2 calls as the
> other UI tests do.

- [x] **Step 2: Run — expect FAIL** (undeclared).

- [x] **Step 3: Add `#include "gittide/branchinfo.hpp"` to `asyncrepo.hpp` and
  declare the methods. Implement each in `asyncrepo.cpp`** following the exact
  existing pattern, e.g.:

```cpp
QCoro::Task<gittide::Expected<std::vector<gittide::BranchInfo>>> AsyncRepo::branches()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.branches();
        });
}

QcoroTaskVoid AsyncRepo::checkoutBranch(QString name) // pattern; real return type spelled in full
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, n = name.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.checkoutBranch(n);
        });
}
```
> Spell each return type out in full as the header declares (no alias). Convert
> every `QString` arg with `.toStdString()` inside the captured lambda, exactly as
> shown — never capture the `QString` by reference across the `co_await`.

- [x] **Step 4: Run — expect PASS.** Run the full UI suite green.
  Run: `ctest --test-dir build -R gittide_ui_tests --output-on-failure`

- [x] **Step 5: Commit.**
  `git commit -am "feat(ui): async wrappers for branch operations"`

---

## Task 7: ViewModel — `RepoController` branch slots, signals, cascade

**Files:** Modify `ui/include/gittide/ui/repocontroller.hpp`,
`ui/src/repocontroller.cpp`, `ui/include/gittide/ui/metatypes.hpp`; Test
`tests/ui/test_repo_controller.cpp` (append).

**Interfaces — Produces:**
```cpp
public slots:
  QCoro::Task<void> refreshBranches();                 // emits branchesChanged + headChanged
  QCoro::Task<void> createBranch(QString name, QString fromOid, bool checkout);
  QCoro::Task<void> switchBranch(QString name);
  QCoro::Task<void> checkoutCommit(QString oid);
  QCoro::Task<void> deleteBranch(QString name, bool force);
  QCoro::Task<void> renameBranch(QString oldName, QString newName);
signals:
  void branchesChanged(std::vector<gittide::BranchInfo>);
  void headChanged(gittide::HeadState);
```

- [x] **Step 1: Write the failing test.**

```cpp
void refresh_branches_emits_branches_and_head()
{
    const auto dir = make_repo_with_commit(); // existing helper in this file
    RepoController controller;
    qRegisterMetaType<std::vector<gittide::BranchInfo>>();
    qRegisterMetaType<gittide::HeadState>();
    QSignalSpy branches(&controller, &RepoController::branchesChanged);
    QSignalSpy head(&controller, &RepoController::headChanged);

    controller.open(QString::fromStdString(dir.generic_string()));
    QCoro::waitFor(controller.refreshBranches());

    QCOMPARE(branches.count(), 1);
    QCOMPARE(head.count(), 1);
    std::filesystem::remove_all(dir);
}

void switch_branch_runs_the_refresh_cascade()
{
    const auto dir = make_repo_with_commit();
    RepoController controller;
    controller.open(QString::fromStdString(dir.generic_string()));
    QCoro::waitFor(controller.createBranch(QStringLiteral("feature"), QString(), false));

    QSignalSpy status(&controller, &RepoController::statusChanged);
    QSignalSpy history(&controller, &RepoController::historyReady);
    QSignalSpy branches(&controller, &RepoController::branchesChanged);
    QCoro::waitFor(controller.switchBranch(QStringLiteral("feature")));

    QVERIFY(status.count() >= 1);
    QVERIFY(history.count() >= 1);
    QVERIFY(branches.count() >= 1);
    std::filesystem::remove_all(dir);
}
```

- [x] **Step 2: Run — expect FAIL.**

- [x] **Step 3:** Add `Q_DECLARE_METATYPE(gittide::BranchInfo)`,
  `Q_DECLARE_METATYPE(std::vector<gittide::BranchInfo>)`,
  `Q_DECLARE_METATYPE(gittide::HeadState)` to `metatypes.hpp` (and
  `#include "gittide/branchinfo.hpp"`). Register them in the `RepoController`
  constructor. Declare slots/signals. Implement:

```cpp
QCoro::Task<void> RepoController::refreshBranches()
{
    if (!m_repo)
        co_return;
    auto list = co_await m_repo->branches();
    if (!list)
    {
        emit operationFailed(QString::fromStdString(list.error().message));
        co_return;
    }
    emit branchesChanged(*list);
    auto h = co_await m_repo->head();
    if (h)
        emit headChanged(*h);
}

QCoro::Task<void> RepoController::switchBranch(QString name)
{
    if (!m_repo)
        co_return;
    auto r = co_await m_repo->checkoutBranch(name);
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::createBranch(QString name, QString fromOid, bool checkout)
{
    if (!m_repo)
        co_return;
    auto r = co_await m_repo->createBranch(name, fromOid);
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    if (checkout)
    {
        auto sw = co_await m_repo->checkoutBranch(name);
        if (!sw)
        {
            emit operationFailed(QString::fromStdString(sw.error().message));
            co_await refreshBranches(); // branch exists even if the switch failed
            co_return;
        }
        co_await refreshStatus();
        co_await refreshHistory();
    }
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::checkoutCommit(QString oid)
{
    if (!m_repo)
        co_return;
    auto r = co_await m_repo->checkoutCommit(oid);
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::deleteBranch(QString name, bool force)
{
    if (!m_repo)
        co_return;
    auto r = co_await m_repo->deleteBranch(name, force);
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::renameBranch(QString oldName, QString newName)
{
    if (!m_repo)
        co_return;
    auto r = co_await m_repo->renameBranch(oldName, newName, /*force=*/false);
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
}
```

- [x] **Step 4: Run — expect PASS.**

- [x] **Step 5: Commit.**
  `git commit -am "feat(ui): RepoController branch slots with refresh cascade"`

---

## Task 8: UI — `BranchBar` widget

**Files:** Create `ui/include/gittide/ui/branchbar.hpp`, `ui/src/branchbar.cpp`;
add both to `ui/CMakeLists.txt`; Test `tests/ui/test_branch_bar.cpp` (add to
`gittide_ui_test_sources`).

**Interfaces — Produces:**
```cpp
class BranchBar : public QWidget {
    Q_OBJECT
public:
    explicit BranchBar(QWidget* parent = nullptr);
    void setBranches(const std::vector<gittide::BranchInfo>& branches);
    void setHead(const gittide::HeadState& head);
signals:
    void switchRequested(const QString& name);
    void createRequested();       // "New branch…" chosen (dialog handled by MainWindow)
    void renameRequested();       // "Rename current…"
    void deleteRequested();       // "Delete…"
};
```
Object names: widget `branchBar`; the button `currentBranchButton`.

- [x] **Step 1: Write the failing test.**

```cpp
// tests/ui/test_branch_bar.cpp
#include "gittide/ui/branchbar.hpp"
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

using gittide::ui::BranchBar;

class TestBranchBar : public QObject
{
    Q_OBJECT
private slots:
    void shows_branch_name()
    {
        BranchBar bar;
        bar.setHead(gittide::HeadState{"main", "abc", false, false});
        auto* btn = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn);
        QVERIFY(btn->text().contains(QStringLiteral("main")));
    }

    void shows_detached_state()
    {
        BranchBar bar;
        bar.setHead(gittide::HeadState{"", "abc1234deadbeef", true, false});
        auto* btn = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn->text().contains(QStringLiteral("detached")));
        QVERIFY(btn->text().contains(QStringLiteral("abc1234"))); // short oid
    }
};
#include "test_branch_bar.moc"
```

- [x] **Step 2: Run — expect FAIL** (no such widget).

- [x] **Step 3: Implement** `BranchBar`: a `QToolButton` (objectName
  `currentBranchButton`) with a `QMenu` popup. `setHead` sets the label — branch
  name, or `"detached @ " + oid.left(7)` when `head.detached`, or `"(no commits)"`
  when `head.unborn`. `setBranches` rebuilds the menu: one action per branch
  (checkable, the `isHead` one checked) wired to `switchRequested(name)`; a
  separator; then `"New branch…"`, `"Rename current…"`, `"Delete…"` wired to the
  three signals. Set `setObjectName("branchBar")`. Colours come from QSS via object
  name — do **not** hard-code hex.

- [x] **Step 4: Run — expect PASS.**

- [x] **Step 5:** Add `branchbar.*` to `gittide_ui` sources and the test to
  `gittide_ui_test_sources`; build; full UI suite green.

- [x] **Step 6: Commit.**
  `git commit -am "feat(ui): BranchBar widget (current branch + actions menu)"`

---

## Task 9: UI — branch dialogs (new / rename / delete)

**Files:** Create `ui/include/gittide/ui/branchdialogs.hpp`,
`ui/src/branchdialogs.cpp`; add to `ui/CMakeLists.txt`; Test
`tests/ui/test_branch_dialogs.cpp`.

**Interfaces — Produces** (free functions returning a chosen value, mirroring
`addrepodialogs` style):
```cpp
struct NewBranchChoice { QString name; bool checkout = true; bool accepted = false; };
NewBranchChoice askNewBranch(QWidget* parent, const QString& fromLabel);
struct RenameChoice { QString name; bool accepted = false; };
RenameChoice askRenameBranch(QWidget* parent, const QString& current);
// delete returns: accepted + force (force set only after the "delete anyway" step)
struct DeleteChoice { bool accepted = false; bool force = false; };
DeleteChoice askDeleteBranch(QWidget* parent, const QString& name, bool unmerged);
```

- [x] **Step 1: Write a focused test** for name validation feedback in
  `askNewBranch` — drive it headlessly by constructing the dialog object the
  function builds (factor the dialog into a small `QDialog` subclass the test can
  instantiate without `exec()`), assert the OK button disables on an empty/invalid
  name. (Follow the existing `test_*_dialog`/`addrepodialogs` test pattern if one
  exists; otherwise test the validation predicate as a free function and keep the
  `exec()` wrapper thin and untested.)

- [x] **Step 2: Run — expect FAIL.**

- [x] **Step 3: Implement** the dialogs with token-styled object names
  (`surface.raised` card via QSS), a `QLineEdit` + live validation (disable primary
  on invalid), and for delete the two-step "delete anyway (not fully merged)"
  secondary action that sets `force = true`.

- [x] **Step 4: Run — expect PASS.**

- [x] **Step 5:** Wire into CMake; build; suite green.

- [x] **Step 6: Commit.**
  `git commit -am "feat(ui): new/rename/delete branch dialogs"`

---

## Task 10: UI — History graph context menu

**Files:** Modify `ui/include/gittide/ui/historyview.hpp`,
`ui/src/historyview.cpp`; Test `tests/ui/test_history_view.cpp` (create or append).

**Interfaces — Produces (HistoryView signals):**
```cpp
void newBranchFromCommitRequested(const QString& oid);
void checkoutCommitRequested(const QString& oid);
```

- [x] **Step 1: Write the failing test** — set a known history on the view, set
  the table's current index to row 0, invoke the context-menu handler
  programmatically (factor it into a `void showContextMenuFor(const QModelIndex&)`
  helper that builds the menu and, in the test, trigger the "Checkout this commit"
  action), assert `checkoutCommitRequested` fires with the row's OID (read via the
  model's `GraphRowRole`).

- [x] **Step 2: Run — expect FAIL.**

- [x] **Step 3: Implement** — set
  `table->setContextMenuPolicy(Qt::CustomContextMenu)`, connect
  `customContextMenuRequested` to a slot that resolves the row's OID from
  `HistoryModel` (via `GraphRowRole`) and shows a `QMenu` with "New branch from
  here…" → `newBranchFromCommitRequested(oid)` and "Checkout this commit" →
  `checkoutCommitRequested(oid)`.

- [x] **Step 4: Run — expect PASS.**

- [x] **Step 5: Commit.**
  `git commit -am "feat(ui): History graph context menu for branch/checkout"`

---

## Task 11: Integration — wire `BranchBar` + menus into `MainWindow`

**Files:** Modify `ui/src/mainwindow.cpp`, `ui/include/gittide/ui/mainwindow.hpp`;
Test `tests/ui/test_main_window.cpp` (append).

**Interfaces — Consumes:** Tasks 7–10.

- [x] **Step 1: Write the failing test** — open a repo with two branches in a
  `MainWindow`, assert a `branchBar` child exists and, after the repo-open cascade,
  its `currentBranchButton` text contains the current branch name.

- [x] **Step 2: Run — expect FAIL.**

- [x] **Step 3: Implement wiring:**
  - Insert a `BranchBar` above the tab widget at central-stack index 2.
  - On `repoOpened`, also `refreshBranches()` (extend the existing cascade in
    `mainwindow.cpp`).
  - `RepoController::branchesChanged`/`headChanged` → `BranchBar::setBranches`/
    `setHead`.
  - `BranchBar::switchRequested` → `controller->switchBranch`.
  - `BranchBar::createRequested` → `askNewBranch(...)` →
    `controller->createBranch(name, "", checkout)`.
  - `BranchBar::renameRequested` → `askRenameBranch(current)` →
    `controller->renameBranch(current, newName)`.
  - `BranchBar::deleteRequested` → `askDeleteBranch(name, unmerged?)` →
    `controller->deleteBranch(name, force)` (retry with `force` after the "not
    fully merged" error if the user confirms — surface the first error via the
    existing `operationFailed` → status bar path).
  - `HistoryView::newBranchFromCommitRequested(oid)` → `askNewBranch(...)` →
    `controller->createBranch(name, oid, checkout)`.
  - `HistoryView::checkoutCommitRequested(oid)` → `controller->checkoutCommit(oid)`.

- [x] **Step 4: Run — expect PASS.** Run the **entire** suite (core + ui) green.
  Run: `ctest --test-dir build --output-on-failure`

- [x] **Step 5: Commit.**
  `git commit -am "feat(ui): wire branch bar and graph menu into the window"`

---

## Task 12: Theming + close-out

**Files:** Modify `ui/src/themestyle.cpp` (QSS for `#branchBar`,
`#currentBranchButton`, and the branch dialogs' object names); verify
`docs/spec/*` already matches; flip statuses.

- [x] **Step 1:** Add QSS rules keyed by the new object names using existing
  tokens (`surface.raised`, `border`, `accent`, `text.primary`) — confirm no hex
  literal lands in any widget `.cpp` (grep). Add/adjust a `test_theme_style.cpp`
  assertion if that suite checks specific selectors; otherwise visual-only.
  Run: `grep -rnE '#[0-9a-fA-F]{6}' ui/src/branchbar.cpp ui/src/branchdialogs.cpp` → expect no matches.
- [x] **Step 2:** Build + full suite green; no new warnings.
- [x] **Step 3:** Confirm the spec sections (product §Branches, engineering
  §Branch operations, design §Components, D21) describe what shipped; fix any
  drift (code is ground truth).
- [x] **Step 4:** Tick this plan's boxes, fill **Outcome** below, set this plan's
  `Status` to `done` here and in [`plans/index.md`](index.md); set the
  [wish](../wishlist/shipped/branch-management.md) `Status` to `done`.
- [x] **Step 5: Commit.**
  `git commit -am "docs: close Plan 8 — branch management shipped"`

---

## Outcome

- Shipped: Full local-branch management — list, create, safe-checkout (stash +
  re-apply), delete (merge-guard + unmerged force-confirm), rename, detached-commit
  checkout from the History graph — wired end-to-end from `GitRepo` through
  `AsyncRepo` / `RepoController` to the `BranchBar` widget and three branch dialogs
  (new / rename / delete) in `MainWindow`. All operations trigger the appropriate
  refresh cascade (status + history + branches on switch/checkout; branches-only on
  delete/rename). Theme tokens applied via QSS for all new object names.
- Spec updated: product §Branches (already current), engineering §Branch
  operations & the refresh cascade (already current), design §Components branch bar
  + branch dialogs (already current), decisions D21 (already current). No drift
  found — code matches spec.
- Code: `core/{branchinfo.hpp,gitrepo.cpp}`, `ui/{asyncrepo,repocontroller,
  branchbar,branchdialogs,historyview,mainwindow}.*`, `ui/src/themestyle.cpp`.
