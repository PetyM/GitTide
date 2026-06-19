# Core Branch Enumeration (local + remote-tracking + worktree) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `core/` so `GitRepo::branches()` returns local branches, remote-tracking branches, and a worktree classification — the data the QML grouped branch dropdown (Local / Worktrees / Remote, design spec §7) needs.

**Architecture:** `BranchInfo` gains a `BranchKind` discriminant plus `upstream` and `worktreePath` fields. `GitRepo::branches()` enumerates `GIT_BRANCH_LOCAL` (as today) and `GIT_BRANCH_REMOTE`, fills each local branch's upstream short name, and sets `worktreePath` for any local branch checked out in a *linked* worktree. Pure facts only — the UI does the Local/Worktrees/Remote grouping from these fields. No Qt; libgit2 stays private to `core/`.

**Tech Stack:** C++23, libgit2 (`git2/branch.h`, `git2/worktree.h`, `git2/refs.h`), Catch2 tests with the `gittide::test::TempRepo` helper.

## Global Constraints

- No Qt in `core/`; libgit2 + nlohmann/json stay PRIVATE to `core/`.
- Errors are values: `Expected<T> = std::expected<T, GitError>`; no exceptions across layers.
- Paths to/from libgit2 via `generic_u8string()` / UTF-8 `std::string`, never `.string()`.
- Code style: `m_` members, Allman braces, lowercase file names; follow `docs/spec/engineering/code-style.md`.
- TDD: failing test first. New core tests go in the matching list in `tests/CMakeLists.txt`.
- Existing test `tests/test_git_repo_branches.cpp` "branches lists the default branch and marks HEAD" asserts `list->size() == 1` on a fresh repo with no remotes/worktrees — new enumeration MUST keep that true (a fresh repo has no remote-tracking refs and no linked worktrees).

---

### Task A1: Extend `BranchInfo` with kind / upstream / worktreePath

**Files:**
- Modify: `core/include/gittide/branchinfo.hpp`
- Test: `tests/test_git_repo_branches.cpp` (add one compile-time/defaults case)

**Interfaces:**
- Produces:
  ```cpp
  namespace gittide {
  enum class BranchKind { Local, RemoteTracking };
  struct BranchInfo {
      std::string name;               // short name: "main" or "origin/main"
      bool        isHead = false;     // current HEAD (local only)
      BranchKind  kind = BranchKind::Local;
      std::string upstream;           // local branch's upstream short name, else ""
      std::string worktreePath;       // path of the linked worktree holding this local branch, else ""
  };
  }
  ```

- [ ] **Step 1: Write the failing test**

Add to `tests/test_git_repo_branches.cpp`:
```cpp
TEST_CASE("BranchInfo defaults to a local branch with no upstream/worktree", "[branches]")
{
    gittide::BranchInfo b{};
    REQUIRE(b.kind == gittide::BranchKind::Local);
    REQUIRE(b.upstream.empty());
    REQUIRE(b.worktreePath.empty());
    REQUIRE_FALSE(b.isHead);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build -R branches --output-on-failure`
Expected: compile error — `BranchKind` / `kind` not declared.

- [ ] **Step 3: Add the enum + fields**

In `core/include/gittide/branchinfo.hpp`, replace the `BranchInfo` struct with the Produces block above. Keep Doxygen comments on each field. Leave `HeadState` untouched.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R branches --output-on-failure`
Expected: PASS (and the existing branches tests still pass — fields are additive with defaults).

- [ ] **Step 5: Commit**

```bash
git add core/include/gittide/branchinfo.hpp tests/test_git_repo_branches.cpp
git commit -m "feat(core): add BranchKind + upstream/worktreePath to BranchInfo"
```

---

### Task A2: `branches()` enumerates remote-tracking branches + fills local upstream

**Files:**
- Modify: `core/src/gitrepo.cpp` (`GitRepo::branches()` at ~497, and the includes block at top)
- Test: `tests/test_git_repo_branches.cpp`

**Interfaces:**
- Consumes: `BranchInfo` from Task A1.
- Produces: `GitRepo::branches()` returns, in one vector: every local branch (`kind == Local`, `isHead` set, `upstream` filled when configured) followed by every remote-tracking branch (`kind == RemoteTracking`, `name` is the short form e.g. `"origin/main"`, `isHead == false`).

- [ ] **Step 1: Write the failing test**

Add to `tests/test_git_repo_branches.cpp` (this test reopens the repo with raw libgit2 to fabricate a remote-tracking ref + upstream — include `<git2.h>` at the top of the test file if not already present):
```cpp
TEST_CASE("branches lists remote-tracking refs and local upstream", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");

    // Fabricate refs/remotes/origin/main pointing at HEAD, and set the local
    // branch's upstream to it, using raw libgit2 on the same repo.
    git_repository* raw = nullptr;
    REQUIRE(git_repository_open(&raw, tmp.path().generic_string().c_str()) == 0);
    git_oid head_oid;
    REQUIRE(git_reference_name_to_id(&head_oid, raw, "HEAD") == 0);
    git_reference* remote_ref = nullptr;
    REQUIRE(git_reference_create(&remote_ref, raw, "refs/remotes/origin/main",
                                 &head_oid, 0, "test") == 0);
    git_reference_free(remote_ref);

    // Determine the local branch's name, then set its upstream.
    git_reference* head_ref = nullptr;
    REQUIRE(git_repository_head(&head_ref, raw) == 0);
    const std::string local = git_reference_shorthand(head_ref);
    git_reference* local_ref = nullptr;
    REQUIRE(git_branch_lookup(&local_ref, raw, local.c_str(), GIT_BRANCH_LOCAL) == 0);
    git_branch_set_upstream(local_ref, "origin/main"); // best-effort
    git_reference_free(local_ref);
    git_reference_free(head_ref);
    git_repository_free(raw);

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto list = repo->branches();
    REQUIRE(list.has_value());

    const auto local_it = std::find_if(list->begin(), list->end(), [&](const auto& b) {
        return b.kind == gittide::BranchKind::Local && b.name == local;
    });
    REQUIRE(local_it != list->end());
    REQUIRE(local_it->upstream == "origin/main");

    const auto remote_it = std::find_if(list->begin(), list->end(), [](const auto& b) {
        return b.kind == gittide::BranchKind::RemoteTracking;
    });
    REQUIRE(remote_it != list->end());
    REQUIRE(remote_it->name == "origin/main");
    REQUIRE_FALSE(remote_it->isHead);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build -R branches --output-on-failure`
Expected: FAIL — no remote-tracking entry returned; `upstream` empty.

- [ ] **Step 3: Implement**

In `core/src/gitrepo.cpp`, rewrite `GitRepo::branches()` so it iterates local then remote. For each local branch, fill `upstream` via `git_branch_upstream()` (returns `GIT_ENOTFOUND` when none — treat as "no upstream", not an error) and read its short name with `git_branch_name()`. Set `kind = BranchKind::Local`. For the remote pass, open a second iterator with `GIT_BRANCH_REMOTE`, set `kind = BranchKind::RemoteTracking`, `isHead = false`. Reuse the existing RAII pattern (`std::unique_ptr` with `git_..._free`). Sketch (adapt to the existing style in the file):
```cpp
Expected<std::vector<BranchInfo>> GitRepo::branches() const
{
    std::vector<BranchInfo> result;

    auto collect = [&](git_branch_t scope, BranchKind kind) -> Expected<void> {
        git_branch_iterator* it = nullptr;
        int rc = git_branch_iterator_new(&it, m_repo, scope);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_branch_iterator, decltype(&git_branch_iterator_free)> guard(it, git_branch_iterator_free);

        git_reference* ref = nullptr;
        git_branch_t    br_type;
        while ((rc = git_branch_next(&ref, &br_type, it)) == 0)
        {
            std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);
            const char* name = nullptr;
            if (git_branch_name(&name, ref) != 0 || !name)
                continue;
            BranchInfo info;
            info.name   = name;
            info.kind   = kind;
            info.isHead = (kind == BranchKind::Local) && git_branch_is_head(ref) == 1;
            if (kind == BranchKind::Local)
            {
                git_reference* up = nullptr;
                if (git_branch_upstream(&up, ref) == 0 && up)
                {
                    const char* up_name = nullptr;
                    if (git_branch_name(&up_name, up) == 0 && up_name)
                        info.upstream = up_name;
                    git_reference_free(up);
                }
            }
            result.push_back(std::move(info));
        }
        if (rc != GIT_ITEROVER)
            return std::unexpected(lastGitError(rc));
        return {};
    };

    if (auto r = collect(GIT_BRANCH_LOCAL, BranchKind::Local); !r)
        return std::unexpected(r.error());
    if (auto r = collect(GIT_BRANCH_REMOTE, BranchKind::RemoteTracking); !r)
        return std::unexpected(r.error());
    return result;
}
```
Add `#include <git2/refs.h>` to the includes block if `git_branch_upstream`/`git_reference_*` are not already transitively available (they are declared in `git2/branch.h` and `git2/refs.h`; verify against the installed headers).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R branches --output-on-failure`
Expected: PASS, including the pre-existing branches tests (fresh repos still report `size()==1`).

- [ ] **Step 5: Commit**

```bash
git add core/src/gitrepo.cpp tests/test_git_repo_branches.cpp
git commit -m "feat(core): branches() lists remote-tracking refs + local upstream"
```

---

### Task A3: Classify local branches checked out in linked worktrees

**Files:**
- Modify: `core/src/gitrepo.cpp` (`GitRepo::branches()` + a file-local helper; includes)
- Test: `tests/test_git_repo_branches.cpp`

**Interfaces:**
- Consumes: `branches()` from Task A2.
- Produces: a local `BranchInfo` whose branch is checked out in a *linked* worktree (not the main working tree) has `worktreePath` set to that worktree's absolute path; all others have `worktreePath == ""`.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_git_repo_branches.cpp`:
```cpp
TEST_CASE("branches marks a branch checked out in a linked worktree", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");

    git_repository* raw = nullptr;
    REQUIRE(git_repository_open(&raw, tmp.path().generic_string().c_str()) == 0);

    // A worktree needs its own branch ref. Create "wt" at HEAD, then add a
    // linked worktree checked out to it.
    git_oid head_oid;
    REQUIRE(git_reference_name_to_id(&head_oid, raw, "HEAD") == 0);
    git_commit* head_commit = nullptr;
    REQUIRE(git_commit_lookup(&head_commit, raw, &head_oid) == 0);
    git_reference* wt_branch = nullptr;
    REQUIRE(git_branch_create(&wt_branch, raw, "wt", head_commit, 0) == 0);

    const auto wt_path = tmp.path().parent_path() / (tmp.path().filename().string() + "-wt");
    git_worktree* wt = nullptr;
    git_worktree_add_options opts = GIT_WORKTREE_ADD_OPTIONS_INIT;
    opts.ref = wt_branch;
    REQUIRE(git_worktree_add(&wt, raw, "wt", wt_path.generic_string().c_str(), &opts) == 0);
    git_worktree_free(wt);
    git_reference_free(wt_branch);
    git_commit_free(head_commit);
    git_repository_free(raw);

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto list = repo->branches();
    REQUIRE(list.has_value());

    const auto it = std::find_if(list->begin(), list->end(),
                                 [](const auto& b) { return b.name == "wt"; });
    REQUIRE(it != list->end());
    REQUIRE_FALSE(it->worktreePath.empty());
    REQUIRE_FALSE(it->isHead); // the main repo's HEAD is the default branch, not "wt"

    std::filesystem::remove_all(wt_path);
}
```
Add `#include <filesystem>` and `#include <git2/worktree.h>` to the test file if missing.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build -R branches --output-on-failure`
Expected: FAIL — `worktreePath` is empty for "wt".

- [ ] **Step 3: Implement**

In `core/src/gitrepo.cpp` add `#include <git2/worktree.h>` and a file-local helper that maps a local branch short name → worktree path, then consult it inside the local-branch loop. Verify the exact `git_worktree_*` signatures against the installed headers (`/home/michal/Qt`… not relevant; libgit2 headers under the build's include path) — in particular `git_worktree_path()` returns `const char*` and `git_repository_open_from_worktree(git_repository**, git_worktree*)` opens the worktree's repo. Sketch:
```cpp
namespace {
std::map<std::string, std::string> worktreeBranchPaths(git_repository* repo)
{
    std::map<std::string, std::string> out;
    git_strarray names = {};
    if (git_worktree_list(&names, repo) != 0)
        return out;
    for (std::size_t i = 0; i < names.count; ++i)
    {
        git_worktree* wt = nullptr;
        if (git_worktree_lookup(&wt, repo, names.strings[i]) != 0)
            continue;
        std::unique_ptr<git_worktree, decltype(&git_worktree_free)> wt_guard(wt, git_worktree_free);
        git_repository* wt_repo = nullptr;
        if (git_repository_open_from_worktree(&wt_repo, wt) != 0)
            continue;
        std::unique_ptr<git_repository, decltype(&git_repository_free)> repo_guard(wt_repo, git_repository_free);
        git_reference* head = nullptr;
        if (git_repository_head(&head, wt_repo) == 0 && head)
        {
            std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);
            if (git_reference_is_branch(head))
            {
                const char* sh = git_reference_shorthand(head);
                const char* p  = git_worktree_path(wt);
                if (sh && p)
                    out[sh] = p;
            }
        }
    }
    git_strarray_dispose(&names);
    return out;
}
}
```
Build the map once at the top of `branches()` and, inside the local-branch loop, set `info.worktreePath = map[name]` when present. Add `#include <map>` and `#include <string>` to the source includes if not already present.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R branches --output-on-failure`
Expected: PASS (all branches tests green).

- [ ] **Step 5: Run the full suite + commit**

Run: `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
Expected: all tests pass.
```bash
git add core/src/gitrepo.cpp tests/test_git_repo_branches.cpp
git commit -m "feat(core): classify branches checked out in linked worktrees"
```

---

## Outcome

Realises part of the QML migration design spec §7 (branch dropdown grouped Local / Worktrees / Remote) — the **core** data layer only. The UI grouping/filtering and dialogs are Plan B (`qml-plan3b-ui-branch-actions`). `RepoController::branchesChanged` already carries `std::vector<BranchInfo>` and `qRegisterMetaType` is in place, so the new fields flow to the ViewModel with no controller change.

## Self-review notes

- Spec coverage: provides `kind`, `upstream`, `worktreePath` — everything the grouped/dimmed/path-annotated dropdown needs. Local/Worktrees/Remote bucketing is a UI concern (Plan B).
- Existing `size()==1` test preserved (fresh repo: no remotes, no worktrees).
- Type consistency: `BranchKind` / field names identical across A1→A3.
