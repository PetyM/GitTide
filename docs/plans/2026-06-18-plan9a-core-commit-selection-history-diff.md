# Plan 9a — Core: commit-from-selection primitive + history diff endpoints

> **For agentic workers:** implement this plan task-by-task, **test-first**. Each
> task's steps use checkbox (`- [ ]`) syntax; tick them as you go. REQUIRED
> SUB-SKILL: `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans`.

| | |
|--|--|
| **Date** | 2026-06-18 |
| **Status** | `planned` |
| **Spec** | [`spec/engineering` §Inline selection, commit, and the history diff](../spec/engineering/engineering.md#inline-selection-commit-and-the-history-diff) · [`spec/product` §Changes/History](../spec/product/product.md#changes-tab) · [D22](../decisions.md) · [D23](../decisions.md) |
| **Depends on** | Plan 3a (core git ops: status/diff/stage/commit), Plan 5a (graph + log) |

**Goal:** Add the three pure-git primitives the GitHub-Desktop UI refactor needs:
**reset the index to `HEAD`** (so the index can be rebuilt from a checked set at
commit time — D23), and two read-only **history-diff** endpoints (list a commit's
changed files, and diff one file in a commit) so History can share the working
diff view (D22).

**Architecture:** All three are pure libgit2 operations → new methods on
`core/GitRepo`, returning `Expected<T>` like the rest of core. `resetIndexToHead`
uses `git_reset` (MIXED) / `git_index_clear` for the unborn case. The history-diff
endpoints use `git_diff_tree_to_tree` (commit's tree vs its first parent; the root
commit diffs against an empty parent tree) and reuse `DiffEngine::parse`. The
commit-file list reuses `FileStatus` (mapping `git_delta_t` → `Index*` flags) so
the UI renders working changes and commit files through one widget.

**Tech stack:** C++23, libgit2, Catch2 (core). No Qt.

## Global constraints

- Invariants ([`engineering`](../spec/engineering/engineering.md#cross-cutting-invariants)):
  **no Qt in `core/`**; libgit2 stays PRIVATE to `core/`; core speaks `std` +
  `Expected<T>`, no exceptions across layers; one owner per `GitRepo`; paths via
  `toGitPath()` / `fromGitPath()` (`generic_u8string()`), never build git command
  strings.
- New `core/` tests → the `gittide_core_tests` list in `tests/CMakeLists.txt`.
- Keep green: all existing core tests. No new compiler warnings.
- Reuse the existing free helpers in `core/src/gitrepo.cpp`: `lastGitError(int)`,
  `toGitPath(path)`, `fromGitPath(const char*)`, `DiffEngine::parse(git_diff*)`.
- Commit style: `feat(core): …` / `test(core): …`, imperative subject; end with
  the Co-Authored-By trailer.

---

## Task 1: Core — `resetIndexToHead()`

Reset the git index to match `HEAD` without touching the working tree (`git reset
--mixed HEAD`). This is the first step of stage-on-commit (D23): clear any staged
state so the index can be rebuilt from exactly the checked set. Unborn-`HEAD` safe
(clears the index).

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `resetIndexToHead`)
- Modify: `core/src/gitrepo.cpp` (implement; add `#include <git2/reset.h>` if not
  already pulled in — `git_reset` lives there)
- Modify: `tests/CMakeLists.txt` (add `test_git_repo_reset.cpp`)
- Test: `tests/test_git_repo_reset.cpp`

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near commit())
// Reset the index to HEAD (git reset --mixed HEAD): unstage everything, leave the
// working tree untouched. On an unborn branch the index is cleared. Used to
// rebuild the index from a checked selection before committing.
Expected<void> resetIndexToHead();
```

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/test_git_repo_reset.cpp
#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
bool anyStaged(const std::vector<gittide::FileStatus>& v)
{
    return std::any_of(v.begin(), v.end(), [](const auto& f) {
        return hasFlag(f.flags, StatusFlag::IndexNew)
            || hasFlag(f.flags, StatusFlag::IndexModified)
            || hasFlag(f.flags, StatusFlag::IndexDeleted);
    });
}
}

TEST_CASE("resetIndexToHead unstages a staged file but keeps the worktree edit", "[reset]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("init");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "edited\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(anyStaged(*repo->status())); // precondition: a.txt is staged

    REQUIRE(repo->resetIndexToHead().has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    REQUIRE_FALSE(anyStaged(*st));                 // nothing staged now
    REQUIRE_FALSE(st->empty());                    // but the change still shows (unstaged)
    REQUIRE(hasFlag((*st)[0].flags, StatusFlag::WtModified));
}

TEST_CASE("resetIndexToHead clears the index on an unborn branch", "[reset]")
{
    gittide::test::TempRepo tmp;            // fresh repo, no commits
    tmp.writeFile("a.txt", "x\n");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    REQUIRE(repo->resetIndexToHead().has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    REQUIRE_FALSE(anyStaged(*st));          // a.txt is back to untracked
    REQUIRE(hasFlag((*st)[0].flags, StatusFlag::WtNew));
}
```

- [ ] **Step 2: Run it — expect FAIL** (`resetIndexToHead` undeclared).
  Run: `ctest --test-dir build -R 'reset' --output-on-failure`

- [ ] **Step 3: Declare `resetIndexToHead()` in `gitrepo.hpp`, implement in
  `gitrepo.cpp`:**

```cpp
Expected<void> GitRepo::resetIndexToHead()
{
    // Unborn HEAD: there is no commit to reset to, so clearing the index is the
    // equivalent "nothing staged" state.
    if (git_repository_head_unborn(m_repo) == 1)
    {
        git_index* index = nullptr;
        int rc           = git_repository_index(&index, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index, decltype(&git_index_free)> guard(index, git_index_free);
        git_index_clear(index);
        rc = git_index_write(index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        return {};
    }

    git_object* head = nullptr;
    int rc           = git_revparse_single(&head, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_object, decltype(&git_object_free)> head_guard(head, git_object_free);

    // MIXED resets the index to the target, leaving the working tree as-is.
    // Target is HEAD, so HEAD does not move — only the index is rewritten.
    rc = git_reset(m_repo, head, GIT_RESET_MIXED, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}
```

- [ ] **Step 4: Run — expect PASS** (both cases). Add `test_git_repo_reset.cpp`
  to `gittide_core_tests` in `tests/CMakeLists.txt`, reconfigure, build, run the
  full core suite green.
  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): reset the index to HEAD (stage-on-commit primitive)"`

---

## Task 2: Core — `commitFiles()` (a commit's changed files)

List the files a commit changed relative to its first parent, as `FileStatus` with
`Index*` flags (so the UI's changed-files widget renders them with the same A/M/D
cue as working changes). The root commit (no parent) diffs against an empty tree —
every file reads as added.

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `commitFiles`)
- Modify: `core/src/gitrepo.cpp` (implement; needs `git_commit_tree`,
  `git_commit_parent`, `git_diff_tree_to_tree`, `git_diff_get_delta` — all in
  `<git2/commit.h>`/`<git2/diff.h>`, already used by this TU)
- Modify: `tests/CMakeLists.txt` (add `test_git_repo_commit_diff.cpp`)
- Test: `tests/test_git_repo_commit_diff.cpp`

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near log())
// Files changed by the commit identified by the 40-char hex oid, relative to its
// first parent (root commit: relative to an empty tree). Flags use Index* to mean
// added / modified / deleted, matching the working-changes display model.
Expected<std::vector<FileStatus>> commitFiles(std::string oid) const;
```
**Consumes:** `log()` (Plan 5a) for obtaining OIDs in tests.

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/test_git_repo_commit_diff.cpp
#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
bool has(const std::vector<gittide::FileStatus>& v, const std::string& p)
{
    return std::any_of(v.begin(), v.end(),
                       [&](const auto& f) { return f.path.generic_string() == p; });
}
}

TEST_CASE("commitFiles lists files added in a commit vs its parent", "[commitfiles]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("c1");
    tmp.writeFile("b.txt", "two\n");          // add a new file in c2
    tmp.commitAll("c2");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto history = repo->log();
    REQUIRE(history.has_value());
    REQUIRE(history->size() == 2);
    const std::string c2 = history->front().oid; // newest first

    auto files = repo->commitFiles(c2);
    REQUIRE(files.has_value());
    REQUIRE(has(*files, "b.txt"));
    REQUIRE_FALSE(has(*files, "a.txt"));      // a.txt unchanged in c2
    auto it = std::find_if(files->begin(), files->end(),
                           [](const auto& f) { return f.path.generic_string() == "b.txt"; });
    REQUIRE(hasFlag(it->flags, StatusFlag::IndexNew));
}

TEST_CASE("commitFiles treats the root commit's files as added", "[commitfiles]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("root");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    const std::string root = repo->log()->front().oid;

    auto files = repo->commitFiles(root);
    REQUIRE(files.has_value());
    REQUIRE(has(*files, "a.txt"));
    REQUIRE(hasFlag((*files)[0].flags, StatusFlag::IndexNew));
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Declare + implement.** Factor the "commit tree + first-parent
  tree" resolution into a small private helper so Task 3 reuses it:

```cpp
// gitrepo.hpp (private)
// Resolve a commit's tree and its first-parent tree (parentTree == nullptr for a
// root commit). Both out-trees are owned by the caller (git_tree_free).
Expected<void> commitTrees(const std::string& oid, git_tree** outTree, git_tree** outParentTree) const;
```

```cpp
// gitrepo.cpp
Expected<void> GitRepo::commitTrees(const std::string& oidHex, git_tree** outTree, git_tree** outParentTree) const
{
    *outTree       = nullptr;
    *outParentTree = nullptr;

    git_oid oid;
    int rc = git_oid_fromstr(&oid, oidHex.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_commit* commit = nullptr;
    rc                 = git_commit_lookup(&commit, m_repo, &oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    rc = git_commit_tree(outTree, commit);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    if (git_commit_parentcount(commit) > 0)
    {
        git_commit* parent = nullptr;
        if (git_commit_parent(&parent, commit, 0) == 0)
        {
            std::unique_ptr<git_commit, decltype(&git_commit_free)> parent_guard(parent, git_commit_free);
            rc = git_commit_tree(outParentTree, parent);
            if (rc < 0)
            {
                git_tree_free(*outTree);
                *outTree = nullptr;
                return std::unexpected(lastGitError(rc));
            }
        }
    }
    return {};
}

Expected<std::vector<FileStatus>> GitRepo::commitFiles(std::string oid) const
{
    git_tree* tree       = nullptr;
    git_tree* parentTree = nullptr;
    if (auto r = commitTrees(oid, &tree, &parentTree); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> parent_guard(parentTree, git_tree_free);

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, parentTree, tree, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    std::vector<FileStatus> result;
    size_t n = git_diff_num_deltas(raw);
    result.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        const git_diff_delta* d = git_diff_get_delta(raw, i);
        StatusFlag flag         = StatusFlag::IndexModified;
        const char* path        = d->new_file.path;
        switch (d->status)
        {
            case GIT_DELTA_ADDED:
                flag = StatusFlag::IndexNew;
                break;
            case GIT_DELTA_DELETED:
                flag = StatusFlag::IndexDeleted;
                path = d->old_file.path;
                break;
            default: // MODIFIED, RENAMED, COPIED, TYPECHANGE → show as modified
                flag = StatusFlag::IndexModified;
                break;
        }
        if (path)
            result.push_back(FileStatus{fromGitPath(path), flag});
    }
    return result;
}
```

- [ ] **Step 4: Run — expect PASS** (both cases).

- [ ] **Step 5:** Add `test_git_repo_commit_diff.cpp` to `gittide_core_tests`;
  build; run full core suite green.

- [ ] **Step 6: Commit.**
  `git commit -am "feat(core): list a commit's changed files vs its parent"`

---

## Task 3: Core — `commitDiff()` (one file in a commit)

Diff a single file inside a commit against its first parent, returning the same
`DiffResult` the working diff returns — so the History view feeds the shared diff
widget read-only.

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `commitDiff`)
- Modify: `core/src/gitrepo.cpp` (implement; reuses `commitTrees` from Task 2 and
  `DiffEngine::parse`)
- Test: `tests/test_git_repo_commit_diff.cpp` (append)

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near diff())
// Diff one file inside the commit identified by the 40-char hex oid against its
// first parent (root commit: against an empty tree). Mirrors diff()'s DiffResult.
Expected<DiffResult> commitDiff(std::string oid, const std::filesystem::path& file) const;
```
**Consumes:** `commitTrees` (Task 2), `commitFiles` (Task 2) for picking a path in
tests.

- [ ] **Step 1: Write the failing test (append).**

```cpp
TEST_CASE("commitDiff returns the added lines of a file in a commit", "[commitfiles]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("c1");
    tmp.writeFile("a.txt", "one\ntwo\n");      // modify a.txt in c2
    tmp.commitAll("c2");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    const std::string c2 = repo->log()->front().oid;

    auto d = repo->commitDiff(c2, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE_FALSE(d->hunks.empty());

    bool sawAddedTwo = false;
    for (const auto& h : d->hunks)
        for (const auto& ln : h.lines)
            if (ln.origin == gittide::DiffLineOrigin::Added && ln.text == "two")
                sawAddedTwo = true;
    REQUIRE(sawAddedTwo);
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Implement.**

```cpp
Expected<DiffResult> GitRepo::commitDiff(std::string oid, const std::filesystem::path& file) const
{
    git_tree* tree       = nullptr;
    git_tree* parentTree = nullptr;
    if (auto r = commitTrees(oid, &tree, &parentTree); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> parent_guard(parentTree, git_tree_free);

    std::string git_file = toGitPath(file);
    char* paths[]        = {git_file.data()};
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count   = 1;

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, parentTree, tree, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);
    return DiffEngine::parse(diff_guard.get());
}
```

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): diff a single file inside a commit"`

---

## Task 4: Core — `WorktreeVsHead` diff target

The reshaped Changes view shows **all** of a file's working changes against `HEAD`
(GitHub-Desktop model), independent of what is in the index. Today `diff()` offers
only `WorktreeVsIndex` and `IndexVsHead`; neither is worktree-vs-`HEAD` when the
index differs from `HEAD`. Add a third target.

> **Why this stays consistent with partial staging:** `commitSelection` (Plan 9b)
> resets the index to `HEAD` *before* staging the checked lines, and `stage()`'s
> partial path computes its patch from `WorktreeVsIndex` at that moment. With
> `index == HEAD`, `WorktreeVsIndex == WorktreeVsHead`, so the line indices the
> user picked from the displayed `WorktreeVsHead` diff line up with what
> `applyPartial` stages.

**Files:**
- Modify: `core/include/gittide/diff.hpp` (add the enum value)
- Modify: `core/src/gitrepo.cpp` (handle it in `diff()`)
- Test: `tests/test_git_repo_diff.cpp` (append)

**Interfaces — Produces:**
```cpp
// diff.hpp
enum class DiffTarget
{
    WorktreeVsIndex, // unstaged changes
    IndexVsHead,     // staged changes
    WorktreeVsHead,  // all working changes vs HEAD (index-independent)
};
```

- [ ] **Step 1: Write the failing test.**

```cpp
TEST_CASE("WorktreeVsHead shows changes even when the index is staged", "[diff]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "one\ntwo\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    // index now matches the worktree, so WorktreeVsIndex is empty...
    auto wi = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(wi.has_value());
    REQUIRE(wi->hunks.empty());

    // ...but WorktreeVsHead still reports the added line.
    auto wh = repo->diff(gittide::DiffTarget::WorktreeVsHead, "a.txt");
    REQUIRE(wh.has_value());
    REQUIRE_FALSE(wh->hunks.empty());
}
```

- [ ] **Step 2: Run — expect FAIL** (enum value missing).

- [ ] **Step 3: Implement.** Add the enum value, then in `GitRepo::diff()` handle
  it by diffing `HEAD`'s tree to the workdir (reuse the existing `HEAD^{tree}`
  resolution; unborn `HEAD` → null tree):

```cpp
// inside GitRepo::diff(), replacing the if/else on target:
if (target == DiffTarget::WorktreeVsIndex)
{
    rc = git_diff_index_to_workdir(&raw, m_repo, nullptr, &opts);
}
else if (target == DiffTarget::WorktreeVsHead)
{
    git_object* head_obj = nullptr;
    git_tree* head_tree  = nullptr;
    if (git_revparse_single(&head_obj, m_repo, "HEAD^{tree}") == 0)
        head_tree = reinterpret_cast<git_tree*>(head_obj);
    rc = git_diff_tree_to_workdir(&raw, m_repo, head_tree, &opts);
    if (head_tree)
        git_tree_free(head_tree);
}
else // IndexVsHead (unchanged)
{
    git_object* head_obj = nullptr;
    git_tree* head_tree  = nullptr;
    if (git_revparse_single(&head_obj, m_repo, "HEAD^{tree}") == 0)
        head_tree = reinterpret_cast<git_tree*>(head_obj);
    rc = git_diff_tree_to_index(&raw, m_repo, head_tree, nullptr, &opts);
    if (head_tree)
        git_tree_free(head_tree);
}
```

- [ ] **Step 4: Run — expect PASS.** Full core suite green.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): add WorktreeVsHead diff target (index-independent)"`

---

## Task 5: Close-out

- [ ] **Step 1:** Build + full core suite green; no new warnings.
  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
- [ ] **Step 2:** Confirm the spec section (engineering §Inline selection, commit,
  and the history diff) describes these three endpoints; fix any drift (code is
  ground truth). Per-symbol Doxygen lives on the new methods in `gitrepo.hpp`.
- [ ] **Step 3:** Tick this plan's boxes, fill **Outcome**, set `Status` to `done`
  here and in [`plans/index.md`](index.md).
- [ ] **Step 4: Commit.**
  `git commit -am "docs: close Plan 9a — core commit-selection + history diff"`

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: `GitRepo::resetIndexToHead`, `GitRepo::commitFiles`,
>   `GitRepo::commitDiff` (+ private `commitTrees` helper), `DiffTarget::WorktreeVsHead`.
> - Spec: engineering §Inline selection, commit, and the history diff.
> - Code: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`; tests
>   `tests/test_git_repo_reset.cpp`, `tests/test_git_repo_commit_diff.cpp`.
</content>
</invoke>
