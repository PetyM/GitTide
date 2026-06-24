# Plan 19 — Plain Rebase Driver (Tier 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

| | |
|--|--|
| **Date** | 2026-06-24 |
| **Status** | `draft` |
| **Spec** | [`docs/spec/product/rebase.md`](../spec/product/rebase.md) |
| **Depends on** | Plan 14 (merge engine + conflict UI), Plan 16 (context menus), Plan 15 (app menu) |

**Goal:** Replay the current branch onto a target branch, driven step-by-step with continue/skip/abort and a k/n in-progress banner.

**Architecture:** Pure additive, mirroring the shipped merge engine. `core/` gains a disk-derived `RebaseState` (D30) plus `Expected<T>` driver verbs over `git_rebase_*`; each verb re-opens the on-disk rebase (`git_rebase_open`) — no handle is held across async calls. `ui/` mirrors the merge surface: AsyncRepo wrappers, controller tasks with a `refreshAfterRebase` cascade and controller-side auto-stash (D31), VM properties/verbs, a `RebaseBanner`, two entry points (branch menu + app-menu dialog). Conflict resolution reuses the existing `acceptConflict` flow unchanged.

**Tech Stack:** C++23, libgit2 (`git2/rebase.h`), `std::expected`; Qt Quick/QML, QCoro, `Q_INVOKABLE`/`Q_PROPERTY`; Catch2 (core), QTest (ui).

## Global Constraints

- **No Qt in `core/`** — `rebase.hpp`/`gitrepo.*` stay pure `std`; Qt only at the ViewModel boundary.
- **libgit2 and nlohmann/json are PRIVATE to `core/`** — no public header includes libgit2; `rebase.hpp` uses only `std`.
- **Errors are values:** core returns `Expected<T>` = `std::expected<T, GitError>`; no exceptions across layers.
- **State is disk-truth (D30):** `RebaseState` is derived every call from the repository, never cached. Each driver verb re-opens the rebase from disk.
- **Auto-stash lives in the controller (D31):** core verbs assume a clean tree (like `mergeBranch`); the controller stashes/pops via `stashSave` + a deferred `m_pendingStashPop`.
- **Paths via `generic_u8string()`, never `.string()`.** Use libgit2 API; never build git command strings.
- **Colour comes from a theme token**, never a hex literal in a widget.
- **TDD:** write the failing test first.
- New `ui/` C++ sources → `ui/CMakeLists.txt`; new QML files → `ui/qml/qml.qrc` **and** `ui/CMakeLists.txt`; new core tests → `tests/CMakeLists.txt`; new ui tests → `tests/CMakeLists.txt` **and** `tests/ui/main.cpp` (`#include` + `RUN(...)` — both mandatory).
- Build: `cmake --build build --parallel`. Core tests: `ctest --test-dir build --output-on-failure -R gittide_core_tests`. UI tests: `... -R gittide_ui_tests`.
- Split a rename from content changes into two commits (n/a here — all new code).

---

## File Structure

**New `core/`:**
- `core/include/gittide/rebase.hpp` — `RebaseState`, `RebaseOutcome` structs (pure `std`).
- `core/src/gitrepo.cpp` / `core/include/gittide/gitrepo.hpp` — `startRebase`, `continueRebase`, `skipRebase`, `abortRebase`, `rebaseState`, private `driveRebase` helper.

**New `ui/`:**
- `ui/qml/RebaseBanner.qml` — in-progress banner (clone of `MergeBanner`).
- `ui/qml/RebaseTargetDialog.qml` — branch picker for the app-menu route.

**Modified `ui/`:** `asyncrepo.*`, `repocontroller.*`, `repoviewmodel.*`, `BranchContextMenu.qml`, `BranchDropdown.qml`, `WorkingPane.qml`, `TitleBar.qml`, `Main.qml`, `qml.qrc`, `ui/CMakeLists.txt`.

**New tests:** `tests/test_git_repo_rebase.cpp`; `tests/ui/test_async_rebase.cpp`, `test_repocontroller_rebase.cpp`, `test_repoviewmodel_rebase.cpp`, `test_qml_rebase_banner.cpp`, `test_qml_rebase_entrypoints.cpp`.

---

## Task 1: `RebaseState` / `RebaseOutcome` + `rebaseState()`

**Files:**
- Create: `core/include/gittide/rebase.hpp`
- Modify: `core/include/gittide/gitrepo.hpp` (declare `rebaseState`), `core/src/gitrepo.cpp` (include `git2/rebase.h`, implement `rebaseState`, private helper `rebaseOntoName`)
- Test: `tests/test_git_repo_rebase.cpp` (new), `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `gittide::RebaseState { bool inProgress; std::string ontoRef; int current; int total; std::string stepSummary; std::vector<std::filesystem::path> conflictedPaths; std::vector<std::filesystem::path> conflictedSubmodules; }`
- Produces: `gittide::RebaseOutcome { bool conflicted = false; }`
- Produces: `RebaseState GitRepo::rebaseState() const;`

- [ ] **Step 1: Create `core/include/gittide/rebase.hpp`**

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

/// Result of advancing a rebase (start/continue/skip).
struct RebaseOutcome
{
    bool conflicted = false; ///< true => paused on a conflicting step; false => finished
};

/// Rebase-in-progress state, ALWAYS derived from the repository (D30).
struct RebaseState
{
    bool        inProgress = false; ///< git_repository_state is REBASE / REBASE_MERGE
    std::string ontoRef;            ///< target branch shorthand (from rebase-merge/onto_name); may be empty
    int         current = 0;        ///< current step, 1-based (0 when none)
    int         total   = 0;        ///< total steps
    std::string stepSummary;        ///< summary of the commit being applied; may be empty
    std::vector<std::filesystem::path> conflictedPaths;       ///< all conflicted entries
    std::vector<std::filesystem::path> conflictedSubmodules;  ///< gitlink subset of the above
};

} // namespace gittide
```

- [ ] **Step 2: Declare `rebaseState` in `gitrepo.hpp`**

Add `#include "gittide/rebase.hpp"` near the other model includes, and in the public section (next to `mergeState` is declared on `asyncrepo`; here near `mergeBranch`/`abortMerge`):

```cpp
    /// Rebase-in-progress state, derived from disk every call (D30). Never errors:
    /// a not-rebasing repo returns a default (inProgress == false).
    RebaseState rebaseState() const;
```

Also declare the private helper near `commitTrees`:

```cpp
    // Best-effort read of rebase-merge/onto_name (the target's label). Empty if absent.
    std::string rebaseOntoName() const;
```

- [ ] **Step 3: Write the failing test**

Create `tests/test_git_repo_rebase.cpp`:

```cpp
#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>

using gittide::GitRepo;

TEST_CASE("rebaseState reports not-in-progress for a clean repo", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->rebaseState();
    REQUIRE_FALSE(st.inProgress);
    REQUIRE(st.current == 0);
    REQUIRE(st.total == 0);
    REQUIRE(st.conflictedPaths.empty());
}
```

- [ ] **Step 4: Register the test in `tests/CMakeLists.txt`**

Add under the `gittide_core_tests` sources, next to `test_git_repo_merge.cpp`:

```cmake
  test_git_repo_rebase.cpp
```

- [ ] **Step 5: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: FAIL to link/compile — `GitRepo::rebaseState` is undefined.

- [ ] **Step 6: Implement `rebaseState` + `rebaseOntoName` in `gitrepo.cpp`**

Add `#include <git2/rebase.h>` near the other git2 includes (a `<git2/rebase.h>` include already exists for pull — keep one). Implement near `mergeState`:

```cpp
std::string GitRepo::rebaseOntoName() const
{
    const char* gp = git_repository_path(m_repo); // the .git dir
    if (!gp)
        return {};
    namespace fs = std::filesystem;
    for (const char* sub : {"rebase-merge", "rebase-apply"})
    {
        fs::path f = fs::path(fromGitPath(gp)) / sub / "onto_name";
        std::ifstream in(f);
        if (in)
        {
            std::string line;
            std::getline(in, line);
            return line; // may be a ref shorthand or an oid; best-effort label
        }
    }
    return {};
}

RebaseState GitRepo::rebaseState() const
{
    RebaseState st;
    const int state = git_repository_state(m_repo);
    st.inProgress = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                 || state == GIT_REPOSITORY_STATE_REBASE
                 || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!st.inProgress)
        return st;

    st.ontoRef = rebaseOntoName();

    // Re-open the on-disk rebase to read step counts and the current commit (D30).
    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    if (git_rebase_open(&rebase, m_repo, &opts) == 0)
    {
        std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);
        st.total          = static_cast<int>(git_rebase_operation_entrycount(rebase));
        const size_t curIx = git_rebase_operation_current(rebase);
        if (curIx != GIT_REBASE_NO_OPERATION && st.total > 0)
        {
            st.current = static_cast<int>(curIx) + 1;
            if (git_rebase_operation* op = git_rebase_operation_byindex(rebase, curIx))
            {
                git_commit* c = nullptr;
                if (git_commit_lookup(&c, m_repo, &op->id) == 0)
                {
                    std::unique_ptr<git_commit, decltype(&git_commit_free)> cg(c, git_commit_free);
                    if (const char* s = git_commit_summary(c))
                        st.stepSummary = s;
                }
            }
        }
    }

    // Conflicted entries — identical derivation to mergeState().
    git_index* index = nullptr;
    if (git_repository_index(&index, m_repo) == 0)
    {
        std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
        if (git_index_has_conflicts(index))
        {
            git_index_conflict_iterator* it = nullptr;
            if (git_index_conflict_iterator_new(&it, index) == 0)
            {
                std::unique_ptr<git_index_conflict_iterator,
                                decltype(&git_index_conflict_iterator_free)>
                    itg(it, git_index_conflict_iterator_free);
                const git_index_entry *anc = nullptr, *our = nullptr, *their = nullptr;
                while (git_index_conflict_next(&anc, &our, &their, it) == 0)
                {
                    const git_index_entry* e = our ? our : (their ? their : anc);
                    if (!e || !e->path)
                        continue;
                    std::filesystem::path p = fromGitPath(e->path);
                    st.conflictedPaths.push_back(p);
                    const bool gitlink =
                        (our && our->mode == GIT_FILEMODE_COMMIT) ||
                        (their && their->mode == GIT_FILEMODE_COMMIT) ||
                        (anc && anc->mode == GIT_FILEMODE_COMMIT);
                    if (gitlink)
                        st.conflictedSubmodules.push_back(p);
                }
            }
        }
    }
    return st;
}
```

Ensure `<fstream>` is included in `gitrepo.cpp` (it is used elsewhere; add if missing).

- [ ] **Step 7: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add core/include/gittide/rebase.hpp core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_rebase.cpp tests/CMakeLists.txt
git commit -m "feat(core): RebaseState + rebaseState() derived from disk (D30)"
```

---

## Task 2: `startRebase` — clean linear replay + guards

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `startRebase`, private `driveRebase`), `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_rebase.cpp`

**Interfaces:**
- Consumes: `RebaseState`, `RebaseOutcome` (Task 1).
- Produces: `Expected<RebaseOutcome> GitRepo::startRebase(std::string ontoRef);`
- Produces (private): `Expected<RebaseOutcome> GitRepo::driveRebase(git_rebase* rebase, git_signature* sig);` — advance next→commit until a step conflicts (return `conflicted=true`, leave on disk) or operations exhaust (`git_rebase_finish`, return `conflicted=false`). `GIT_EAPPLIED` steps auto-skip.

- [ ] **Step 1: Declare in `gitrepo.hpp`**

Public:

```cpp
    /// Rebase the current branch onto local branch `ontoRef`'s tip.
    /// Assumes a clean worktree (controller auto-stashes, D31). Drives every step;
    /// returns conflicted==true paused on the first conflicting step (state left on
    /// disk), else finishes. Errors: unborn/detached HEAD, ontoRef missing, a rebase
    /// or merge already in progress.
    Expected<RebaseOutcome> startRebase(std::string ontoRef);
```

Private:

```cpp
    // Advance an open rebase: next→(conflict? pause : commit) until GIT_ITEROVER
    // (then git_rebase_finish). GIT_EAPPLIED steps are skipped. Does not free rebase/sig.
    Expected<RebaseOutcome> driveRebase(git_rebase* rebase, git_signature* sig);
```

- [ ] **Step 2: Write the failing test**

Append to `tests/test_git_repo_rebase.cpp`:

```cpp
#include <git2.h> // for oid parent inspection in assertions

TEST_CASE("startRebase replays a linear branch onto the target", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");                       // master @ c0

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("f.txt", "feature\n");
    tmp.commitAll("c1 on feature");            // feature @ c1

    // master advances on a different file (no conflict).
    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("m.txt", "main\n");
    tmp.commitAll("c2 on master");             // master @ c2
    REQUIRE(repo->checkoutBranch("feature").has_value());

    auto out = repo->startRebase("master");
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    // feature's tip now sits on top of master: m.txt and f.txt both present.
    auto st = repo->status();
    REQUIRE(st.has_value());
    auto hist = repo->log(10);
    REQUIRE(hist.has_value());
    // c1's content replayed: f.txt exists in the working tree.
    REQUIRE(std::filesystem::exists(tmp.path() / "f.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "m.txt"));
}

TEST_CASE("startRebase errors when the onto branch is missing", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto out = repo->startRebase("does-not-exist");
    REQUIRE_FALSE(out.has_value());
}

TEST_CASE("startRebase errors mid-merge", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("base");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "feature\n");
    tmp.commitAll("feature edit");
    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("main edit");
    auto m = repo->mergeBranch("feature");     // conflicts → mid-merge
    REQUIRE(m.has_value());

    auto out = repo->startRebase("feature");
    REQUIRE_FALSE(out.has_value());            // refuse to rebase mid-merge
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: FAIL — `startRebase` undefined.

- [ ] **Step 4: Implement `driveRebase` + `startRebase` in `gitrepo.cpp`**

```cpp
Expected<RebaseOutcome> GitRepo::driveRebase(git_rebase* rebase, git_signature* sig)
{
    RebaseOutcome out;
    while (true)
    {
        git_rebase_operation* op = nullptr;
        int rc = git_rebase_next(&op, rebase);
        if (rc == GIT_ITEROVER)
        {
            rc = git_rebase_finish(rebase, sig);
            if (rc < 0)
                return std::unexpected(lastGitError(rc));
            out.conflicted = false;
            return out;
        }
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        // Did applying this operation leave conflicts? Pause if so (state on disk).
        git_index* index = nullptr;
        rc = git_repository_index(&index, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
        if (git_index_has_conflicts(index))
        {
            out.conflicted = true;
            return out;
        }

        // Commit the applied step, reusing its original author/message.
        git_oid id;
        rc = git_rebase_commit(&id, rebase, nullptr, sig, nullptr, nullptr);
        if (rc == GIT_EAPPLIED)
            continue; // change already upstream → implicit skip
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
}

Expected<RebaseOutcome> GitRepo::startRebase(std::string ontoRef)
{
    // Guard: never start over a merge or an existing rebase.
    const int state = git_repository_state(m_repo);
    if (state != GIT_REPOSITORY_STATE_NONE)
        return std::unexpected(GitError{-1, "cannot rebase: another operation is in progress"});

    // Guard: need a born, attached HEAD (a branch to move).
    if (git_repository_head_unborn(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is unborn"});
    if (git_repository_head_detached(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is detached"});

    // Resolve the target branch to an annotated commit (the upstream).
    git_reference* ref = nullptr;
    int rc = git_branch_lookup(&ref, m_repo, ontoRef.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);

    git_annotated_commit* upstream = nullptr;
    rc = git_annotated_commit_from_ref(&upstream, m_repo, ref);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_annotated_commit, decltype(&git_annotated_commit_free)>
        up_guard(upstream, git_annotated_commit_free);

    // branch == NULL → use HEAD; onto == NULL → defaults to upstream. This is the
    // exact "git rebase <upstream>" semantics: replay upstream..HEAD onto upstream.
    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    rc = git_rebase_init(&rebase, m_repo, nullptr, upstream, nullptr, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    return driveRebase(rebase, sig);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS (all three new cases + Task 1's case).

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_rebase.cpp
git commit -m "feat(core): startRebase clean replay + guards (git_rebase_init/next/commit/finish)"
```

---

## Task 3: `continueRebase` — conflict pause then resume

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_rebase.cpp`

**Interfaces:**
- Consumes: `driveRebase` (Task 2).
- Produces: `Expected<RebaseOutcome> GitRepo::continueRebase();` — commit the resolved current step (`git_rebase_commit`; `GIT_EAPPLIED` tolerated), then `driveRebase` to the next pause or finish. Errors if no rebase is in progress or unresolved conflicts remain.

- [ ] **Step 1: Declare in `gitrepo.hpp`**

```cpp
    /// Continue an in-progress rebase after the current step's conflicts are
    /// resolved (the resolved files must be staged in the index). Commits the step,
    /// then advances. Errors if not rebasing or conflicts remain.
    Expected<RebaseOutcome> continueRebase();
```

- [ ] **Step 2: Write the failing test**

Append to `tests/test_git_repo_rebase.cpp`:

```cpp
#include <fstream>

TEST_CASE("startRebase pauses on conflict, continueRebase finishes after resolve", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "feature\n");       // same line → will conflict
    tmp.commitAll("c1 on feature");

    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("c2 on master");
    REQUIRE(repo->checkoutBranch("feature").has_value());

    auto out = repo->startRebase("master");
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);
    auto st = repo->rebaseState();
    REQUIRE(st.inProgress);
    REQUIRE(st.conflictedPaths.size() == 1);

    // Resolve: write a merged file and stage it.
    tmp.writeFile("a.txt", "resolved\n");
    REQUIRE(repo->stagePath("a.txt").has_value());

    auto cont = repo->continueRebase();
    REQUIRE(cont.has_value());
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);
}

TEST_CASE("continueRebase errors when no rebase is in progress", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto cont = repo->continueRebase();
    REQUIRE_FALSE(cont.has_value());
}
```

> **Note:** if the core API for staging a single path is named differently than `stagePath`, use the existing single-path stage method (check `gitrepo.hpp`; the merge tests resolve via `acceptConflict` at the UI layer, but core tests stage directly). If none exists, stage via `git_index_add_bypath` is wrapped by the existing `stage`/`stageSelection` method — use that with a whole-file selection.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: FAIL — `continueRebase` undefined.

- [ ] **Step 4: Implement `continueRebase` in `gitrepo.cpp`**

```cpp
Expected<RebaseOutcome> GitRepo::continueRebase()
{
    const int state = git_repository_state(m_repo);
    const bool rebasing = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                       || state == GIT_REPOSITORY_STATE_REBASE
                       || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!rebasing)
        return std::unexpected(GitError{-1, "no rebase in progress"});

    git_index* index = nullptr;
    int rc = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    {
        std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
        if (git_index_has_conflicts(index))
            return std::unexpected(GitError{-1, "cannot continue: unresolved conflicts remain"});
    }

    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    rc = git_rebase_open(&rebase, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    // Commit the just-resolved current operation, then advance.
    git_oid id;
    rc = git_rebase_commit(&id, rebase, nullptr, sig, nullptr, nullptr);
    if (rc < 0 && rc != GIT_EAPPLIED)
        return std::unexpected(lastGitError(rc));

    return driveRebase(rebase, sig);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_rebase.cpp
git commit -m "feat(core): continueRebase — commit resolved step then resume"
```

---

## Task 4: `skipRebase` + `abortRebase`

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_rebase.cpp`

**Interfaces:**
- Consumes: `driveRebase` (Task 2).
- Produces: `Expected<RebaseOutcome> GitRepo::skipRebase();` — discard the current step (reset the conflicted worktree to the rebase HEAD), advance. Errors if not rebasing.
- Produces: `Expected<void> GitRepo::abortRebase();` — `git_rebase_abort`, restoring the exact pre-rebase HEAD. Errors if not rebasing.

- [ ] **Step 1: Declare in `gitrepo.hpp`**

```cpp
    /// Skip the current rebase step without committing it, then advance.
    Expected<RebaseOutcome> skipRebase();
    /// Abort an in-progress rebase: restore the exact pre-rebase HEAD and worktree.
    Expected<void> abortRebase();
```

- [ ] **Step 2: Write the failing test**

Append to `tests/test_git_repo_rebase.cpp`:

```cpp
TEST_CASE("abortRebase restores the exact pre-rebase HEAD", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "feature\n");
    tmp.commitAll("c1 on feature");
    auto before = repo->head();                // pre-rebase feature tip
    REQUIRE(before.has_value());

    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("c2 on master");
    REQUIRE(repo->checkoutBranch("feature").has_value());

    auto out = repo->startRebase("master");
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);

    REQUIRE(repo->abortRebase().has_value());
    REQUIRE_FALSE(repo->rebaseState().inProgress);
    auto after = repo->head();
    REQUIRE(after.has_value());
    REQUIRE(after->oid == before->oid);        // tip identical to pre-rebase
}

TEST_CASE("skipRebase drops the conflicting commit", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "feature\n");       // the only feature commit; conflicts
    tmp.commitAll("c1 on feature");

    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("c2 on master");
    REQUIRE(repo->checkoutBranch("feature").has_value());

    auto out = repo->startRebase("master");
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);

    auto sk = repo->skipRebase();              // drop c1 → nothing left → finish
    REQUIRE(sk.has_value());
    REQUIRE_FALSE(sk->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);
}
```

> Adjust `before->oid`/`head()` field access to match the existing `HeadState` shape (see `gitrepo.hpp`; the field may be `oid`). If `head()` returns a struct with a different member, use it.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: FAIL — `skipRebase`/`abortRebase` undefined.

- [ ] **Step 4: Implement in `gitrepo.cpp`**

```cpp
Expected<RebaseOutcome> GitRepo::skipRebase()
{
    const int state = git_repository_state(m_repo);
    const bool rebasing = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                       || state == GIT_REPOSITORY_STATE_REBASE
                       || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!rebasing)
        return std::unexpected(GitError{-1, "no rebase in progress"});

    // Discard the conflicted/half-applied worktree so the next patch applies cleanly.
    git_oid head_oid;
    int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* head = nullptr;
    rc = git_commit_lookup(&head, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    {
        std::unique_ptr<git_commit, decltype(&git_commit_free)> hg(head, git_commit_free);
        git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
        copts.checkout_strategy    = GIT_CHECKOUT_FORCE;
        rc = git_reset(m_repo, reinterpret_cast<const git_object*>(head), GIT_RESET_HARD, &copts);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }

    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    rc = git_rebase_open(&rebase, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    // driveRebase starts with git_rebase_next, abandoning the current op's commit.
    return driveRebase(rebase, sig);
}

Expected<void> GitRepo::abortRebase()
{
    const int state = git_repository_state(m_repo);
    const bool rebasing = state == GIT_REPOSITORY_STATE_REBASE_MERGE
                       || state == GIT_REPOSITORY_STATE_REBASE
                       || state == GIT_REPOSITORY_STATE_REBASE_INTERACTIVE;
    if (!rebasing)
        return std::unexpected(GitError{-1, "no rebase in progress"});

    git_rebase*        rebase = nullptr;
    git_rebase_options opts   = GIT_REBASE_OPTIONS_INIT;
    int rc = git_rebase_open(&rebase, m_repo, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rb_guard(rebase, git_rebase_free);

    rc = git_rebase_abort(rebase);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS. Run the whole `[rebase]` tag too: `ctest --test-dir build --output-on-failure -R gittide_core_tests`.

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_rebase.cpp
git commit -m "feat(core): skipRebase + abortRebase"
```

---

## Task 5: AsyncRepo wrappers

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp`, `ui/src/asyncrepo.cpp`
- Test: `tests/ui/test_async_rebase.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: core verbs (Tasks 1–4).
- Produces on `AsyncRepo`:
  - `QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> startRebase(QString ontoRef);`
  - `QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> continueRebase();`
  - `QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> skipRebase();`
  - `QCoro::Task<gittide::Expected<void>> abortRebase();`
  - `QCoro::Task<gittide::Expected<gittide::RebaseState>> rebaseState();`

- [ ] **Step 1: Declare in `asyncrepo.hpp`**

Add `#include "gittide/rebase.hpp"` near `#include "gittide/merge.hpp"`, then near the merge wrappers:

```cpp
    /// Rebase the current branch onto ontoRef's tip (clean tree assumed; controller stashes).
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> startRebase(QString ontoRef);
    /// Continue an in-progress rebase after the current step's conflicts are resolved.
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> continueRebase();
    /// Skip the current rebase step.
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> skipRebase();
    /// Abort an in-progress rebase, restoring the pre-rebase state.
    QCoro::Task<gittide::Expected<void>> abortRebase();
    /// Rebase-in-progress state, derived from disk (D30).
    QCoro::Task<gittide::Expected<gittide::RebaseState>> rebaseState();
```

> `rebaseState()` core returns a plain `RebaseState` (not `Expected`). Wrap it as `Expected` for a uniform AsyncRepo surface (mirrors how `mergeState` is `Expected`): the lambda returns `gittide::Expected<gittide::RebaseState>{ impl->repo.rebaseState() }`.

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_async_rebase.cpp` (mirror `test_async_merge.cpp`):

```cpp
#pragma once
#include "gittide/ui/asyncrepo.hpp"
#include "support/temprepo.hpp"
#include <QObject>
#include <QtTest>
#include <QCoreApplication>

class TestAsyncRebase : public QObject
{
    Q_OBJECT
private slots:
    void start_clean_then_state_idle()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("base.txt", "base\n");
        tmp.commitAll("c0");
        // feature diverges on a different file (no conflict).
        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r.has_value());
            QVERIFY(r->createBranch("feature", "").has_value());
            QVERIFY(r->checkoutBranch("feature").has_value());
        }
        tmp.writeFile("f.txt", "feature\n");
        tmp.commitAll("c1");
        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r->checkoutBranch("master").has_value());
        }
        tmp.writeFile("m.txt", "main\n");
        tmp.commitAll("c2");
        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r->checkoutBranch("feature").has_value());
        }

        gittide::ui::AsyncRepo repo;
        QVERIFY(repo.open(tmp.path()));

        bool done = false;
        [&]() -> QCoro::Task<void> {
            auto out = co_await repo.startRebase("master");
            QVERIFY(out.has_value());
            QVERIFY(!out->conflicted);
            auto st = co_await repo.rebaseState();
            QVERIFY(st.has_value());
            QVERIFY(!st->inProgress);
            done = true;
        }();
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    }
};
```

> Match the exact `AsyncRepo::open` signature and namespace used in `test_async_merge.cpp` (it may be `gittide::ui::AsyncRepo` with `open(std::filesystem::path)` returning bool, or a factory). Copy that file's setup boilerplate verbatim and adapt the body.

- [ ] **Step 3: Register the ui test**

In `tests/CMakeLists.txt`, add next to `test_async_merge.cpp`:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_async_rebase.cpp
```

In `tests/ui/main.cpp`: add `#include "test_async_rebase.cpp"` near the other includes and `RUN(TestAsyncRebase);` near `RUN(TestAsyncMerge);`.

- [ ] **Step 4: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL — wrappers undefined.

- [ ] **Step 5: Implement the wrappers in `asyncrepo.cpp`**

```cpp
QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> AsyncRepo::startRebase(QString ontoRef)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, n = ontoRef.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.startRebase(n);
        });
}

QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> AsyncRepo::continueRebase()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]() { std::scoped_lock lock(impl->mutex); return impl->repo.continueRebase(); });
}

QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> AsyncRepo::skipRebase()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]() { std::scoped_lock lock(impl->mutex); return impl->repo.skipRebase(); });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::abortRebase()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]() { std::scoped_lock lock(impl->mutex); return impl->repo.abortRebase(); });
}

QCoro::Task<gittide::Expected<gittide::RebaseState>> AsyncRepo::rebaseState()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]() -> gittide::Expected<gittide::RebaseState>
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.rebaseState();
        });
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/asyncrepo.hpp ui/src/asyncrepo.cpp tests/ui/test_async_rebase.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): AsyncRepo wrappers for the rebase driver"
```

---

## Task 6: RepoController — driver tasks, auto-stash, refresh cascade

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp`, `ui/src/repocontroller.cpp`
- Test: `tests/ui/test_repocontroller_rebase.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: AsyncRepo rebase wrappers (Task 5); existing `stashSave`, `m_pendingStashPop`, `popPendingStash()`, `currentBranchName()`.
- Produces on `RepoController`:
  - `QCoro::Task<void> startRebase(QString ontoRef);`
  - `QCoro::Task<void> continueRebase();`
  - `QCoro::Task<void> skipRebase();`
  - `QCoro::Task<void> abortRebase();`
  - `QCoro::Task<void> refreshAfterRebase();`
  - signals `void rebaseStateChanged(gittide::RebaseState state);` and `void rebaseFinished(QString headOid);`
- Modifies: `refreshStatus()` also fetches `rebaseState()` and emits `rebaseStateChanged` (mirrors the `mergeState` emit already there).

- [ ] **Step 1: Declare in `repocontroller.hpp`**

Add `#include "gittide/rebase.hpp"`, then:

```cpp
    /// Rebase the current branch onto ontoRef. Auto-stashes a dirty tree (D31),
    /// drives the first run; on a clean finish emits rebaseFinished + pops the stash,
    /// on conflict leaves the repo mid-rebase (pop deferred to continue/abort).
    QCoro::Task<void> startRebase(QString ontoRef);
    /// Continue after resolving the current step's conflicts.
    QCoro::Task<void> continueRebase();
    /// Skip the current step.
    QCoro::Task<void> skipRebase();
    /// Abort the rebase, restoring the pre-rebase state, and pop the auto-stash.
    QCoro::Task<void> abortRebase();
```

In signals:

```cpp
    /// Emitted whenever rebase-in-progress state is refreshed (D30).
    void rebaseStateChanged(gittide::RebaseState state);
    /// Emitted when a rebase finishes cleanly. headOid is the new HEAD commit OID.
    void rebaseFinished(QString headOid);
```

Private:

```cpp
    // Status(+rebaseState) + history + branches + sync. Tail of every rebase op.
    QCoro::Task<void> refreshAfterRebase();
```

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_repocontroller_rebase.cpp` (mirror `test_repocontroller_merge.cpp` setup; copy its harness verbatim). Core body:

```cpp
    void clean_rebase_emits_finished_and_idle_state()
    {
        // Build feature diverged from master on a non-conflicting file.
        // (Reuse the helper style from test_repocontroller_merge.cpp.)
        // ... set up TempRepo + AsyncRepo + RepoController `ctrl` ...

        QSignalSpy finished(&ctrl, &RepoController::rebaseFinished);
        QSignalSpy stateSpy(&ctrl, &RepoController::rebaseStateChanged);

        bool done = false;
        [&]() -> QCoro::Task<void> {
            co_await ctrl.startRebase("master");
            done = true;
        }();
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

        QCOMPARE(finished.count(), 1);
        // Last rebaseStateChanged reports not-in-progress.
        QVERIFY(stateSpy.count() >= 1);
        auto last = qvariant_cast<gittide::RebaseState>(stateSpy.last().at(0));
        QVERIFY(!last.inProgress);
    }
```

> Register `gittide::RebaseState` with the Qt metatype system if `rebaseStateChanged` is used across queued connections / `QSignalSpy`. Add `qRegisterMetaType<gittide::RebaseState>("gittide::RebaseState");` in `RepoController`'s constructor (next to where `MergeState` is registered — grep `qRegisterMetaType` in `repocontroller.cpp`; if `MergeState` is registered there, register `RebaseState` the same way) and add `Q_DECLARE_METATYPE(gittide::RebaseState)` after the struct in `rebase.hpp`'s ui consumers — but **do not** put `Q_DECLARE_METATYPE` in the core header (keeps core Qt-free). Place `Q_DECLARE_METATYPE(gittide::RebaseState)` at the top of `repocontroller.hpp` (after includes), mirroring how `MergeState` is declared for the metatype system.

- [ ] **Step 3: Register the ui test**

`tests/CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_rebase.cpp`.
`tests/ui/main.cpp`: `#include "test_repocontroller_rebase.cpp"` + `RUN(TestRepoControllerRebase);`.

- [ ] **Step 4: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL — controller methods/signals undefined.

- [ ] **Step 5: Implement in `repocontroller.cpp`**

Register the metatype in the constructor (next to the `MergeState` registration):

```cpp
    qRegisterMetaType<gittide::RebaseState>("gittide::RebaseState");
```

Extend `refreshStatus()` — after the existing `mergeState` block, add:

```cpp
    // D30: rebase state is derived from disk on every status refresh, never cached.
    auto rs = co_await m_repo->rebaseState();
    if (!self)
        co_return;
    if (rs)
        emit rebaseStateChanged(*rs);
```

The driver tasks:

```cpp
QCoro::Task<void> RepoController::startRebase(QString ontoRef)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    // Auto-stash a dirty tree (D31); remember the deferred pop.
    auto saved = co_await m_repo->stashSave("gittide: auto-stash before rebase");
    if (!self)
        co_return;
    if (!saved)
    {
        emit operationFailed(QString::fromStdString(saved.error().message));
        co_return;
    }
    m_pendingStashPop = *saved;

    auto out = co_await m_repo->startRebase(ontoRef);
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await popPendingStash(); // start never began → restore the user's work
        if (!self)
            co_return;
        co_await refreshAfterRebase();
        co_return;
    }

    if (!out->conflicted) // finished in one run
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    // else: conflicted → leave mid-rebase; the deferred pop waits for finish/abort.

    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::continueRebase()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto out = co_await m_repo->continueRebase();
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await refreshAfterRebase();
        co_return;
    }
    if (!out->conflicted)
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::skipRebase()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto out = co_await m_repo->skipRebase();
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await refreshAfterRebase();
        co_return;
    }
    if (!out->conflicted)
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::abortRebase()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->abortRebase();
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_await refreshAfterRebase();
        co_return;
    }
    co_await popPendingStash(); // restore the user's pre-rebase work
    if (!self)
        co_return;
    co_await refreshAfterRebase();
}

QCoro::Task<void> RepoController::refreshAfterRebase()
{
    QPointer<RepoController> self = this;
    co_await refreshStatus(); // also fetches rebaseState → rebaseStateChanged
    if (!self)
        co_return;
    co_await refreshHistory();
    if (!self)
        co_return;
    co_await refreshBranches();
    if (!self)
        co_return;
    co_await refreshSyncStatus();
}
```

> Verify `m_repo->head()` returns `Expected<HeadState>` with an `oid` member (it is used elsewhere in the controller — grep `->head()`); adjust the member access if the field differs.

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp tests/ui/test_repocontroller_rebase.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): RepoController rebase tasks + auto-stash + refresh cascade"
```

---

## Task 7: RepoViewModel — properties + verbs

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`, `ui/src/repoviewmodel.cpp`
- Test: `tests/ui/test_repoviewmodel_rebase.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: controller signals/tasks (Task 6).
- Produces on `RepoViewModel`:
  - Properties (NOTIFY `rebaseStateChanged`): `bool rebaseInProgress`, `QString rebaseOnto`, `int rebaseStep`, `int rebaseTotal`, `QString rebaseStepSummary`, `int rebaseConflictedCount`, `bool rebaseHasSubmoduleConflicts`.
  - `Q_INVOKABLE void startRebase(const QString& ref);`, `continueRebase()`, `skipRebase()`, `abortRebase()`.

- [ ] **Step 1: Declare in `repoviewmodel.hpp`**

Add `#include "gittide/rebase.hpp"`. Near the merge properties:

```cpp
    Q_PROPERTY(bool    rebaseInProgress  READ rebaseInProgress  NOTIFY rebaseStateChanged)
    Q_PROPERTY(QString rebaseOnto        READ rebaseOnto        NOTIFY rebaseStateChanged)
    Q_PROPERTY(int     rebaseStep        READ rebaseStep        NOTIFY rebaseStateChanged)
    Q_PROPERTY(int     rebaseTotal       READ rebaseTotal       NOTIFY rebaseStateChanged)
    Q_PROPERTY(QString rebaseStepSummary READ rebaseStepSummary NOTIFY rebaseStateChanged)
    Q_PROPERTY(int     rebaseConflictedCount       READ rebaseConflictedCount       NOTIFY rebaseStateChanged)
    Q_PROPERTY(bool    rebaseHasSubmoduleConflicts READ rebaseHasSubmoduleConflicts NOTIFY rebaseStateChanged)
```

Getters (inline, mirroring the merge getters):

```cpp
    bool    rebaseInProgress() const { return m_rebase.inProgress; }
    QString rebaseOnto() const { return QString::fromStdString(m_rebase.ontoRef); }
    int     rebaseStep() const { return m_rebase.current; }
    int     rebaseTotal() const { return m_rebase.total; }
    QString rebaseStepSummary() const { return QString::fromStdString(m_rebase.stepSummary); }
    int     rebaseConflictedCount() const { return int(m_rebase.conflictedPaths.size()); }
    bool    rebaseHasSubmoduleConflicts() const { return !m_rebase.conflictedSubmodules.empty(); }
```

Invokables + signal:

```cpp
    Q_INVOKABLE void startRebase(const QString& ref);
    Q_INVOKABLE void continueRebase();
    Q_INVOKABLE void skipRebase();
    Q_INVOKABLE void abortRebase();
signals:
    void rebaseStateChanged();
```

Member:

```cpp
    gittide::RebaseState m_rebase;
```

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_repoviewmodel_rebase.cpp` (mirror `test_repoviewmodel_merge.cpp`):

```cpp
    void properties_reflect_controller_state()
    {
        // ... construct VM wired to a controller over a TempRepo with a divergent
        // feature (copy the merge VM test harness) ...

        QSignalSpy spy(&vm, &RepoViewModel::rebaseStateChanged);
        bool done = false;
        [&]() -> QCoro::Task<void> {
            vm.startRebase("master");           // non-conflicting → finishes
            co_return;
        }();
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
        QVERIFY(!vm.rebaseInProgress());
    }
```

- [ ] **Step 3: Register the ui test**

`tests/CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repoviewmodel_rebase.cpp`.
`tests/ui/main.cpp`: `#include "test_repoviewmodel_rebase.cpp"` + `RUN(TestRepoViewModelRebase);`.

- [ ] **Step 4: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL.

- [ ] **Step 5: Implement in `repoviewmodel.cpp`**

In the constructor, next to the `mergeStateChanged` connect:

```cpp
    connect(m_controller, &RepoController::rebaseStateChanged, this,
            [this](const gittide::RebaseState& s) { m_rebase = s; emit rebaseStateChanged(); });
    connect(m_controller, &RepoController::rebaseFinished, this,
            [this](const QString&) { /* refresh driven by the controller cascade */ });
```

Verbs:

```cpp
void RepoViewModel::startRebase(const QString& ref)
{
    QCoro::connect(m_controller->startRebase(ref), this, [] {});
}
void RepoViewModel::continueRebase()
{
    QCoro::connect(m_controller->continueRebase(), this, [] {});
}
void RepoViewModel::skipRebase()
{
    QCoro::connect(m_controller->skipRebase(), this, [] {});
}
void RepoViewModel::abortRebase()
{
    QCoro::connect(m_controller->abortRebase(), this, [] {});
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp tests/ui/test_repoviewmodel_rebase.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): RepoViewModel rebase state properties + verbs"
```

---

## Task 8: RebaseBanner.qml

**Files:**
- Create: `ui/qml/RebaseBanner.qml`
- Modify: `ui/qml/WorkingPane.qml` (host it next to `MergeBanner`), `ui/qml/qml.qrc`, `ui/CMakeLists.txt`
- Test: `tests/ui/test_qml_rebase_banner.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: VM rebase properties + verbs (Task 7).
- Produces: `RebaseBanner { property var repo }` — visible iff `repo.rebaseInProgress`; objectName `"rebaseBanner"`; buttons objectNames `"rebaseContinueButton"`, `"rebaseSkipButton"`, `"rebaseAbortButton"`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_qml_rebase_banner.cpp` (mirror `test_qml_merge_banner.cpp`): load a QML fragment hosting `RebaseBanner` with a stub `repo` object exposing `rebaseInProgress=true`, `rebaseStep=1`, `rebaseTotal=3`, `rebaseConflictedCount=1`; assert the banner is visible, the label contains `"1/3"`, and **Continue** is disabled while `rebaseConflictedCount > 0`. Copy the stub/loader scaffolding from `test_qml_merge_banner.cpp` verbatim and adapt property names.

- [ ] **Step 2: Register the ui test**

`tests/CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_rebase_banner.cpp`.
`tests/ui/main.cpp`: `#include "test_qml_rebase_banner.cpp"` + `RUN(TestQmlRebaseBanner);`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL — `RebaseBanner` type not found.

- [ ] **Step 4: Create `ui/qml/RebaseBanner.qml`**

```qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Rebase-in-progress banner. Collapses to height 0 when not rebasing. Reads only
// the VM's RebaseState properties (D30 — disk truth only). Mutually exclusive with
// MergeBanner: rebase and merge are never both in progress.
Rectangle {
    id: root
    objectName: "rebaseBanner"

    property var repo

    visible: repo && repo.rebaseInProgress
    height: visible ? 44 : 0
    color: theme.surfaceRaised
    clip: true

    Rectangle
    {
        width: 3
        height: parent.height
        color: theme.stateConflict
    }

    RowLayout
    {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12

        Label
        {
            text: "⤴"
            color: theme.stateConflict
            font.pixelSize: 16
        }

        Label
        {
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: theme.textPrimary
            font.pixelSize: 13
            text: repo
                  ? ("Rebasing onto " + (repo.rebaseOnto.length ? repo.rebaseOnto : "target")
                     + " — step " + repo.rebaseStep + "/" + repo.rebaseTotal
                     + (repo.rebaseStepSummary.length ? (" — " + repo.rebaseStepSummary) : "")
                     + (repo.rebaseConflictedCount > 0
                        ? (" — " + repo.rebaseConflictedCount + " conflicted file"
                           + (repo.rebaseConflictedCount === 1 ? "" : "s"))
                        : ""))
                  : ""
        }

        Button
        {
            objectName: "rebaseContinueButton"
            text: "Continue"
            enabled: repo && repo.rebaseConflictedCount === 0
            onClicked: repo.continueRebase()
        }

        Button
        {
            objectName: "rebaseSkipButton"
            text: "Skip"
            onClicked: repo.skipRebase()
        }

        Button
        {
            objectName: "rebaseAbortButton"
            text: "Abort rebase"
            onClicked: repo.abortRebase()
        }
    }
}
```

- [ ] **Step 5: Register the QML file**

In `ui/qml/qml.qrc`, add `<file>RebaseBanner.qml</file>` next to `MergeBanner.qml`.
In `ui/CMakeLists.txt`, add `RebaseBanner.qml` wherever `MergeBanner.qml` is listed (grep for it).

- [ ] **Step 6: Host it in `WorkingPane.qml`**

Right after the existing `MergeBanner { ... }` block:

```qml
        // Rebase-in-progress banner — collapses to height 0 when not rebasing.
        RebaseBanner {
            Layout.fillWidth: true
            repo: repoVm
        }
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add ui/qml/RebaseBanner.qml ui/qml/WorkingPane.qml ui/qml/qml.qrc ui/CMakeLists.txt tests/ui/test_qml_rebase_banner.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): RebaseBanner with continue/skip/abort"
```

---

## Task 9: Entry points — branch menu + app-menu dialog

**Files:**
- Create: `ui/qml/RebaseTargetDialog.qml`
- Modify: `ui/qml/BranchContextMenu.qml`, `ui/qml/BranchDropdown.qml`, `ui/qml/TitleBar.qml`, `ui/qml/Main.qml`, `ui/qml/qml.qrc`, `ui/CMakeLists.txt`
- Modify spec: `docs/spec/product/context-menus.md`, `docs/spec/product/app-menu.md`
- Test: `tests/ui/test_qml_rebase_entrypoints.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `repoVm.startRebase(ref)` (Task 7); `BranchContextMenu` existing `isHead` / `branchName`.
- Produces: `BranchContextMenu` new `signal rebase()` + item (hidden when `isHead`); `RebaseTargetDialog { signal accepted(string ref) }`; app-menu item `"Rebase current branch…"` → `titleBar.rebaseRequested()` → Main opens the dialog.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_qml_rebase_entrypoints.cpp` (mirror `test_qml_merge_entrypoints.cpp`): load `BranchContextMenu` with `isHead=false`, find the `"Rebase current onto …"` `AppMenuItem` by text/objectName, trigger it, assert the `rebase()` signal fires; load with `isHead=true`, assert the item is not visible. Also assert `appMenuPopup` contains an item with objectName `"rebaseMenuItem"`.

- [ ] **Step 2: Register the ui test**

`tests/CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_rebase_entrypoints.cpp`.
`tests/ui/main.cpp`: `#include "test_qml_rebase_entrypoints.cpp"` + `RUN(TestQmlRebaseEntrypoints);`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL.

- [ ] **Step 4: Add the item to `BranchContextMenu.qml`**

Add a signal `signal rebase()` next to `signal merge()`, then below the Merge item:

```qml
    AppMenuItem {
        objectName: "rebaseBranchItem"
        text: repoVm ? ("Rebase " + repoVm.currentBranch + " onto " + menu.branchName)
                     : "Rebase onto branch"
        visible: !menu.isHead
        onTriggered: menu.rebase()
    }
```

Update the header comment's disabled-vs-hidden list to add: `Rebase: hidden when isHead (can't rebase a branch onto itself)`.

- [ ] **Step 5: Wire it in `BranchDropdown.qml`**

Next to `onMerge:`:

```qml
        onRebase:            { if (repoVm) repoVm.startRebase(branchMenu.branchName); dropdown.close() }
```

- [ ] **Step 6: Create `ui/qml/RebaseTargetDialog.qml`**

```qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Branch picker for the app-menu "Rebase current branch…" route. Lists local
// branches except the current one; emits accepted(ref) on Rebase.
OverlayCard {
    id: dialog
    objectName: "rebaseTargetDialog"

    /// The RepoViewModel — used for the branch list and the current branch name.
    property var repo
    signal accepted(string ref)

    property string selectedRef: ""

    ColumnLayout
    {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label
        {
            text: repo ? ("Rebase " + repo.currentBranch + " onto:") : "Rebase onto:"
            color: theme.textPrimary
            font.pixelSize: 14
        }

        ListView
        {
            id: list
            objectName: "rebaseTargetList"
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true
            model: repo ? repo.localBranchNames : []   // see note below
            delegate: ItemDelegate
            {
                width: list.width
                text: modelData
                highlighted: dialog.selectedRef === modelData
                onClicked: dialog.selectedRef = modelData
            }
        }

        RowLayout
        {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button { text: "Cancel"; onClicked: dialog.close() }
            Button
            {
                objectName: "rebaseTargetConfirm"
                text: "Rebase"
                enabled: dialog.selectedRef.length > 0
                onClicked: { dialog.accepted(dialog.selectedRef); dialog.close() }
            }
        }
    }
}
```

> **Branch list source:** use whatever the VM already exposes for the branch dropdown's local branches (grep `repoviewmodel.hpp` for the branch list model/property — e.g. `branchModel` / `localBranchNames`). If only a `QAbstractListModel` exists, set `model:` to it and read the name role in the delegate (`model.name` / a role property) instead of `modelData`, and filter out `repo.currentBranch` in the delegate's `visible`. Do not add a new VM property if one already serves the dropdown.

- [ ] **Step 7: Add the app-menu item in `TitleBar.qml`**

Add a signal to `TitleBar` (next to `optionsRequested`/`aboutRequested`): `signal rebaseRequested()`. In `appMenuPopup`, before the separator/Quit:

```qml
                AppMenuItem {
                    objectName: "rebaseMenuItem"
                    text: "Rebase current branch…"
                    onTriggered: titleBar.rebaseRequested()
                }
```

- [ ] **Step 8: Open the dialog from `Main.qml`**

Instantiate `RebaseTargetDialog { id: rebaseTargetDialog; repo: repoVm; onAccepted: repoVm.startRebase(ref) }` next to the other dialogs, and connect the title bar: in the `TitleBar { ... }` usage add `onRebaseRequested: rebaseTargetDialog.open()` (mirror how `onOptionsRequested` opens the options dialog).

- [ ] **Step 9: Register the QML file**

`ui/qml/qml.qrc`: add `<file>RebaseTargetDialog.qml</file>`.
`ui/CMakeLists.txt`: add `RebaseTargetDialog.qml` next to the other dialog QML entries.

- [ ] **Step 10: Update the spec tables**

In `docs/spec/product/context-menus.md` `BranchContextMenu` table (§4.2), add a row:

```
| **Rebase `<current>` onto `<name>`** | **Hidden** when `isHead` |
```

and add to the wiring line: `onRebase → repoVm.startRebase(name)`.

In `docs/spec/product/app-menu.md` §3 popup layout, add `Rebase current branch…` to the item list and note it opens `RebaseTargetDialog`.

- [ ] **Step 11: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 12: Commit**

```bash
git add ui/qml/RebaseTargetDialog.qml ui/qml/BranchContextMenu.qml ui/qml/BranchDropdown.qml ui/qml/TitleBar.qml ui/qml/Main.qml ui/qml/qml.qrc ui/CMakeLists.txt docs/spec/product/context-menus.md docs/spec/product/app-menu.md tests/ui/test_qml_rebase_entrypoints.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): rebase entry points — branch menu item + app-menu target dialog"
```

---

## Task 10: Docs — decisions, wishlist graduation, plan index

**Files:**
- Modify: `docs/decisions.md`, `docs/wishlist/rebase.md`, `docs/plans/index.md`, this plan's `Status`.

- [ ] **Step 1: Add a decision to `docs/decisions.md`**

Append a new D-entry (use the next free number) recording: rebase and merge are **mutually exclusive** (each verb guards on `git_repository_state`); the auto-stash lives in the controller (D31 extended to rebase); and the **driver-first** scope (no interactive todo-editor in this cut). Keep the format of the existing D30/D31 entries.

- [ ] **Step 2: Graduate the wish in `docs/wishlist/rebase.md`**

Change the driver part from `idea` → designed/shipped: update the status note at the top to point at `spec/product/rebase.md` and `plans/2026-06-24-plan19-rebase-driver.md`, and mark the **interactive editor** as the remaining `idea`.

- [ ] **Step 3: Add this plan to `docs/plans/index.md`**

Add a row for Plan 19 next to Plan 18, linking `2026-06-24-plan19-rebase-driver.md`.

- [ ] **Step 4: Flip this plan's Status**

Change the header `Status` from `draft` to `shipped (2026-06-24)`.

- [ ] **Step 5: Full test sweep**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: PASS — all core + ui tests.

- [ ] **Step 6: Commit**

```bash
git add docs/decisions.md docs/wishlist/rebase.md docs/plans/index.md docs/plans/2026-06-24-plan19-rebase-driver.md
git commit -m "docs: graduate rebase driver — decisions, wishlist, plan index"
```

---

## Self-Review notes (for the implementer)

- **`head()` shape:** Tasks 4/6 read `head()->oid`. Confirm `HeadState`'s member name in `gitrepo.hpp` and adjust.
- **Single-path stage in core test (Task 3):** use the existing core stage API; the name `stagePath` is a placeholder for whatever single-file stage method exists.
- **Branch-list source (Task 9):** reuse the dropdown's existing branch model/property; don't invent a new one.
- **Metatype (Task 6):** `Q_DECLARE_METATYPE`/`qRegisterMetaType` for `gittide::RebaseState` live in the **ui** layer only — never in the core header.
- **libgit2 rebase across calls:** every verb re-opens via `git_rebase_open` and frees the handle; no `git_rebase*` is held across async boundaries (D30).
- **Skip reset:** `skipRebase` hard-resets to HEAD before advancing so the abandoned step's conflict markers don't block the next patch. Watch the conflicting-only-commit case (the skip test) — after skip there may be nothing left, so `driveRebase` finishes immediately.
