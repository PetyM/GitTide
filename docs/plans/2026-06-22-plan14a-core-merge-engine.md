# Plan 14a — Core: merge engine + conflict & merge-in-progress model

> **For agentic workers:** implement this plan task-by-task, **test-first**. Each
> task's steps use checkbox (`- [ ]`) syntax; tick them as you go. REQUIRED
> SUB-SKILL: `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans`.

| | |
|--|--|
| **Date** | 2026-06-22 |
| **Status** | `planned` |
| **Spec** | [`spec/engineering` §Merge & conflict resolution](../spec/engineering/engineering.md#merge--conflict-resolution) · [`spec/product` §Merge](../spec/product/product.md#merge) · [D29](../decisions.md) · [D30](../decisions.md) · [D31](../decisions.md) |
| **Depends on** | Plan 3a (status/diff/stage/commit), Plan 5a (log), Plan 8 (branches: `safeSwitch`, stash internals) |

**Goal:** Add the pure-git primitives a merge + inline conflict-resolution UI
needs: analyse and perform a local-branch merge (fast-forward / clean merge
commit / conflicting merge that writes markers into the worktree), read
merge-in-progress state **from the repository** (the no-limbo invariant, D30),
commit or abort the merge, plus the supporting primitives the controller
orchestrates — `stashSave`/`stashPop` (deferred-pop merge auto-stash, D31) and
submodule `deinit`/`reinit` (reactive submodule-conflict retry, D31).

**Architecture:** All new operations are pure libgit2 → methods on `core/GitRepo`
returning `Expected<T>`. A new header `core/include/gittide/merge.hpp` holds the
plain-`std` model types (`MergeAnalysis`, `MergeOutcome`, `MergeState`). A new
`StatusFlag::Conflicted` bit lets `status()` mark conflicted files so the UI
badges them with the existing A/M/D/U/**C** machinery. Merge state is **always
re-derived from disk** (`git_repository_state`, `MERGE_HEAD`, the index conflict
iterator) — never cached — so a merge from the CLI or surviving a restart is
reported correctly and abort is always reachable (D30). Auto-stash is **not**
folded into `mergeBranch`: because the stash pop must be deferred past a
conflicted merge until `commitMerge` (D31), the stash lifecycle spans multiple
controller calls, so core exposes `stashSave`/`stashPop` and the controller (Plan
14b) owns the sequence.

**Tech stack:** C++23, libgit2 (`<git2/merge.h>`, `<git2/index.h>`,
`<git2/submodule.h>`, `<git2/reset.h>` — all already included by `gitrepo.cpp`),
Catch2 (core). No Qt.

## Global constraints

- Invariants ([`engineering`](../spec/engineering/engineering.md#cross-cutting-invariants)):
  **no Qt in `core/`**; libgit2 + nlohmann/json stay PRIVATE to `core/`; core
  speaks `std` + `Expected<T>`, no exceptions across layers; one owner per
  `GitRepo`; paths via `toGitPath()` / `fromGitPath()` (`generic_u8string()`),
  never build git command strings.
- **D30 — merge state is derived from disk, never cached.** No method may store
  an "are we merging?" flag; `mergeState()` re-reads the repository every call.
- Reuse the existing free helpers in `core/src/gitrepo.cpp`: `lastGitError(int)`,
  `toGitPath(path)`, `fromGitPath(const char*)`, `workdir()`, and the
  signature-building pattern from `safeSwitch` (`git_signature_default` →
  fallback `git_signature_now`).
- New `core/` tests → the `gittide_core_tests` list in `tests/CMakeLists.txt`.
  Tests that touch submodules clone from a local `file://` temp repo (no network).
- Keep green: all existing core tests; no new compiler warnings.
- Commit style: `feat(core): …` / `test(core): …`, imperative subject; end with
  the `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` trailer.

---

## Task 1: `StatusFlag::Conflicted` reported by `status()`

A conflicted file (an index entry with conflict stages) must surface in the
normal changed-files list with a distinct bit so the UI can render the **C**
letter and sort it on top. Add the flag and report it from `status()`.

**Files:**
- Modify: `core/include/gittide/filestatus.hpp` (add `Conflicted` bit)
- Modify: `core/src/gitrepo.cpp` (`status()`: map libgit2 `GIT_STATUS_CONFLICTED`)
- Modify: `tests/CMakeLists.txt` (add `test_git_repo_merge.cpp`)
- Test: `tests/test_git_repo_merge.cpp` (new file — Tasks 1–5 append to it)

**Interfaces — Produces:**
```cpp
// filestatus.hpp — add to enum class StatusFlag
Conflicted = 1 << 6, // index has conflict stages (mid-merge)
```

- [ ] **Step 1: Write the failing test.** A merge that conflicts leaves a
  conflicted index entry; `status()` must flag it. (Uses `mergeBranch` from Task 3
  — write the test now; it will fail to compile until Task 3, so for THIS task
  assert the simpler property that the flag exists and `hasFlag` composes.)

```cpp
// tests/test_git_repo_merge.cpp
#include "gittide/gitrepo.hpp"
#include "gittide/merge.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
// Build two branches that touch the same line so a merge must conflict.
// Returns an opened repo positioned on "main" with "feature" diverged.
gittide::Expected<GitRepo> conflictingRepo(gittide::test::TempRepo& tmp)
{
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("base");
    auto repo = GitRepo::open(tmp.path());
    if (!repo) return std::unexpected(repo.error());
    // feature: change the line one way
    (void)repo->createBranch("feature", "");
    (void)repo->checkoutBranch("feature");
    tmp.writeFile("a.txt", "feature\n");
    tmp.commitAll("feature edit");
    // back to main: change the same line another way
    (void)repo->checkoutBranch("main");      // TempRepo's default branch
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("main edit");
    return repo;
}
}

TEST_CASE("Conflicted flag composes and is distinct", "[merge]")
{
    StatusFlag f = StatusFlag::WtModified | StatusFlag::Conflicted;
    REQUIRE(hasFlag(f, StatusFlag::Conflicted));
    REQUIRE(hasFlag(f, StatusFlag::WtModified));
    REQUIRE_FALSE(hasFlag(StatusFlag::WtModified, StatusFlag::Conflicted));
}
```

> Note: `TempRepo`'s default branch may be `master` or `main` depending on the
> libgit2/git default. Confirm by reading `tests/support/temprepo.hpp`; use the
> actual name in `checkoutBranch(...)`. If it is `master`, substitute throughout.

- [ ] **Step 2: Run — expect FAIL** (`StatusFlag::Conflicted` undeclared,
  `merge.hpp` missing — create `merge.hpp` as a stub in Task 2; for now add only
  the flag so this case compiles). Run: `ctest --test-dir build -R 'merge' --output-on-failure`

- [ ] **Step 3: Add the flag** to `filestatus.hpp`, and map it in `status()`.
  In `GitRepo::status()`, where each entry's `git_status_t` is translated, add:

```cpp
if (s & GIT_STATUS_CONFLICTED)
    flags |= StatusFlag::Conflicted;
```

  (Place it alongside the existing `GIT_STATUS_WT_*` / `GIT_STATUS_INDEX_*`
  mappings. `GIT_STATUS_CONFLICTED` is in `<git2/status.h>`, already pulled in.)

- [ ] **Step 4: Run — expect PASS** (the compose test). Build the full core suite
  green. Add `test_git_repo_merge.cpp` to `gittide_core_tests`, reconfigure.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): add StatusFlag::Conflicted, report it from status()"`

---

## Task 2: `merge.hpp` model + `mergeState()` derived from disk

Add the plain-`std` merge model and a method that reports merge-in-progress state
read **fresh from the repository** every call (D30): is a merge in progress, the
ref being merged (parsed from `MERGE_MSG`), the conflicted paths, and the subset
of those that are submodule (gitlink) conflicts.

**Files:**
- Create: `core/include/gittide/merge.hpp`
- Modify: `core/include/gittide/gitrepo.hpp` (declare `mergeState`, include `merge.hpp`)
- Modify: `core/src/gitrepo.cpp` (implement `mergeState`)
- Test: `tests/test_git_repo_merge.cpp` (append)

**Interfaces — Produces:**
```cpp
// core/include/gittide/merge.hpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

// git_merge_analysis result, reduced to the cases GitTide acts on.
enum class MergeAnalysis
{
    UpToDate,    // nothing to merge
    FastForward, // HEAD can be fast-forwarded to the target
    Normal,      // a real merge (may or may not conflict)
};

// Result of starting a merge (mergeBranch).
struct MergeOutcome
{
    MergeAnalysis analysis   = MergeAnalysis::UpToDate;
    bool          conflicted = false; // Normal merge that left conflict entries
    std::string   newOid;             // FF/clean: the new HEAD/merge-commit oid; else empty
};

// Merge-in-progress state, ALWAYS derived from the repository (D30).
struct MergeState
{
    bool        inProgress = false; // git_repository_state == MERGE
    std::string mergedRef;          // e.g. "feature/x", parsed from MERGE_MSG; may be empty
    std::vector<std::filesystem::path> conflictedPaths;       // all conflicted entries
    std::vector<std::filesystem::path> conflictedSubmodules;  // gitlink subset of the above
};

} // namespace gittide
```
```cpp
// gitrepo.hpp (public, near head())
// Read merge-in-progress state from the repository (state == MERGE, MERGE_MSG,
// and the index conflict iterator). Derived every call — never cached (D30).
Expected<MergeState> mergeState() const;
```

- [ ] **Step 1: Write the failing test.**

```cpp
TEST_CASE("mergeState reports not-in-progress for a clean repo", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE_FALSE(ms->inProgress);
    REQUIRE(ms->conflictedPaths.empty());
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Implement.** Helper to read the conflicted paths via the index
  conflict iterator, flagging gitlink entries; `mergedRef` parsed from `MERGE_MSG`.

```cpp
#include <fstream> // add near the top of gitrepo.cpp if not present

namespace {
// Parse the branch name out of a MERGE_MSG first line: "Merge branch 'feature/x'".
std::string parseMergedRef(const std::filesystem::path& gitdir)
{
    std::ifstream in(gitdir / "MERGE_MSG");
    if (!in) return {};
    std::string line;
    std::getline(in, line);
    const auto a = line.find('\'');
    if (a == std::string::npos) return {};
    const auto b = line.find('\'', a + 1);
    if (b == std::string::npos) return {};
    return line.substr(a + 1, b - a - 1);
}
} // namespace

Expected<MergeState> GitRepo::mergeState() const
{
    MergeState st;
    st.inProgress = git_repository_state(m_repo) == GIT_REPOSITORY_STATE_MERGE;

    git_index* index = nullptr;
    int rc           = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> index_guard(index, git_index_free);

    if (git_index_has_conflicts(index))
    {
        git_index_conflict_iterator* it = nullptr;
        rc = git_index_conflict_iterator_new(&it, index);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_index_conflict_iterator,
                        decltype(&git_index_conflict_iterator_free)>
            it_guard(it, git_index_conflict_iterator_free);

        const git_index_entry *ancestor = nullptr, *our = nullptr, *their = nullptr;
        while (git_index_conflict_next(&ancestor, &our, &their, it) == 0)
        {
            const git_index_entry* e = our ? our : (their ? their : ancestor);
            if (!e || !e->path)
                continue;
            std::filesystem::path p = fromGitPath(e->path);
            st.conflictedPaths.push_back(p);
            // A gitlink (submodule) conflict: any side carrying the commit mode.
            const bool gitlink =
                (our && our->mode == GIT_FILEMODE_COMMIT) ||
                (their && their->mode == GIT_FILEMODE_COMMIT) ||
                (ancestor && ancestor->mode == GIT_FILEMODE_COMMIT);
            if (gitlink)
                st.conflictedSubmodules.push_back(p);
        }
    }

    if (st.inProgress)
    {
        const char* gp = git_repository_path(m_repo); // the .git dir
        if (gp)
            st.mergedRef = parseMergedRef(fromGitPath(gp));
    }
    return st;
}
```

- [ ] **Step 4: Run — expect PASS.** Full core suite green.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): merge.hpp model + mergeState() derived from disk"`

---

## Task 3: `mergeBranch()` — analyse + perform

Analyse the merge of a local branch into current `HEAD` and perform it:
up-to-date → no-op; fast-forward → move HEAD + checkout; otherwise a normal merge
that, on conflict, writes `<<<<<<< ======= >>>>>>>` markers into the worktree and
leaves conflict entries in the index. Does **not** stash (the controller owns
that, D31) and does **not** auto-create the merge commit on conflict.

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `mergeBranch`)
- Modify: `core/src/gitrepo.cpp` (implement)
- Test: `tests/test_git_repo_merge.cpp` (append)

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near checkoutBranch())
// Analyse and perform a merge of local branch `name` into current HEAD.
// FF when possible (moves HEAD, no merge commit); otherwise a normal merge,
// writing conflict markers into the worktree on conflict. Caller handles a dirty
// tree (stash) and, on conflict, drives resolution + commitMerge. See MergeOutcome.
Expected<MergeOutcome> mergeBranch(std::string name);
```
**Consumes:** `createBranch`/`checkoutBranch` (Plan 8) in tests.

- [ ] **Step 1: Write the failing tests** (fast-forward, clean merge, conflict).

```cpp
TEST_CASE("mergeBranch fast-forwards when HEAD is an ancestor", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "one\ntwo\n");
    tmp.commitAll("c2 on feature");
    REQUIRE(repo->checkoutBranch("main").has_value()); // or "master"

    auto out = repo->mergeBranch("feature");
    REQUIRE(out.has_value());
    REQUIRE(out->analysis == gittide::MergeAnalysis::FastForward);
    REQUIRE_FALSE(out->conflicted);
    // HEAD now sees the feature content.
    auto d = repo->diff(gittide::DiffTarget::WorktreeVsHead, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.empty()); // worktree matches the fast-forwarded HEAD
}

TEST_CASE("mergeBranch leaves conflict markers + entries on a real conflict", "[merge]")
{
    gittide::test::TempRepo tmp;
    auto repo = conflictingRepo(tmp); // helper from Task 1
    REQUIRE(repo.has_value());

    auto out = repo->mergeBranch("feature");
    REQUIRE(out.has_value());
    REQUIRE(out->analysis == gittide::MergeAnalysis::Normal);
    REQUIRE(out->conflicted);

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE(ms->inProgress);
    REQUIRE(ms->conflictedPaths.size() == 1);
    REQUIRE(ms->conflictedPaths[0].generic_string() == "a.txt");

    // The worktree file carries conflict markers.
    std::ifstream in(tmp.path() / "a.txt");
    std::string body((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(body.find("<<<<<<<") != std::string::npos);
    REQUIRE(body.find(">>>>>>>") != std::string::npos);
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Implement.**

```cpp
Expected<MergeOutcome> GitRepo::mergeBranch(std::string name)
{
    // Resolve the local branch to an annotated commit (merge analysis input).
    git_reference* ref = nullptr;
    int rc = git_branch_lookup(&ref, m_repo, name.c_str(), GIT_BRANCH_LOCAL);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> ref_guard(ref, git_reference_free);

    git_annotated_commit* their = nullptr;
    rc = git_annotated_commit_from_ref(&their, m_repo, ref);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_annotated_commit, decltype(&git_annotated_commit_free)>
        their_guard(their, git_annotated_commit_free);

    const git_annotated_commit* heads[] = {their};
    git_merge_analysis_t analysis;
    git_merge_preference_t pref;
    rc = git_merge_analysis(&analysis, &pref, m_repo, heads, 1);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    MergeOutcome out;
    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE)
    {
        out.analysis = MergeAnalysis::UpToDate;
        return out;
    }

    if (analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)
    {
        out.analysis = MergeAnalysis::FastForward;
        const git_oid* target = git_annotated_commit_id(their);

        git_commit* tc = nullptr;
        rc = git_commit_lookup(&tc, m_repo, target);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> tc_guard(tc, git_commit_free);

        git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
        copts.checkout_strategy    = GIT_CHECKOUT_SAFE;
        rc = git_checkout_tree(m_repo, reinterpret_cast<const git_object*>(tc), &copts);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        // Move the current branch ref to the target, then point HEAD's workdir at it.
        git_reference* head_ref = nullptr;
        rc = git_repository_head(&head_ref, m_repo);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head_ref, git_reference_free);
        git_reference* new_ref = nullptr;
        rc = git_reference_set_target(&new_ref, head_ref, target, "merge: fast-forward");
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        git_reference_free(new_ref);

        char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
        git_oid_tostr(hex, sizeof(hex), target);
        out.newOid = hex;
        return out;
    }

    // Normal merge: writes into index + worktree, leaving conflicts if any.
    out.analysis = MergeAnalysis::Normal;
    git_merge_options mopts   = GIT_MERGE_OPTIONS_INIT;
    git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
    copts.checkout_strategy    = GIT_CHECKOUT_SAFE | GIT_CHECKOUT_ALLOW_CONFLICTS;
    rc = git_merge(m_repo, heads, 1, &mopts, &copts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_index* index = nullptr;
    rc = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> index_guard(index, git_index_free);
    out.conflicted = git_index_has_conflicts(index) != 0;
    return out;
}
```

> Note: a clean normal merge (no conflicts) leaves a staged, conflict-free index
> and a `MERGE_HEAD` — the controller then calls `commitMerge` (Task 4) to finish
> it. We deliberately do **not** auto-commit inside `mergeBranch`, so FF, clean,
> and conflicting merges share one resolution path (commitMerge) in the UI.

- [ ] **Step 4: Run — expect PASS** (all three cases). Full core suite green.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): mergeBranch — analyse + perform FF/clean/conflicting merge"`

---

## Task 4: `commitMerge()` — finish the merge

Create the merge commit from the current index (parents: `HEAD` + each
`MERGE_HEAD`), then clean up the merge state. Errors if conflict entries remain.

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `commitMerge`)
- Modify: `core/src/gitrepo.cpp` (implement)
- Test: `tests/test_git_repo_merge.cpp` (append)

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near commit())
// Create the merge commit from the current index (parents HEAD + MERGE_HEAD),
// then clear merge state. Errors if unresolved conflict entries remain. req.message
// defaults at the UI layer to "Merge branch '<x>' into <current>". Returns the oid.
Expected<std::string> commitMerge(CommitRequest req);
```

- [ ] **Step 1: Write the failing test.** Resolve the conflict (overwrite the file
  + stage), then commitMerge yields a two-parent commit and clears state.

```cpp
TEST_CASE("commitMerge creates a 2-parent commit and clears merge state", "[merge]")
{
    gittide::test::TempRepo tmp;
    auto repo = conflictingRepo(tmp);
    REQUIRE(repo.has_value());
    REQUIRE(repo->mergeBranch("feature")->conflicted);

    // Resolve: write a merged file and stage it (clears the conflict entry).
    tmp.writeFile("a.txt", "resolved\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto oid = repo->commitMerge(gittide::CommitRequest{"Merge branch 'feature' into main"});
    REQUIRE(oid.has_value());

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE_FALSE(ms->inProgress);          // state cleared

    auto hist = repo->log();
    REQUIRE(hist.has_value());
    // The newest commit is the merge commit (its first line matches the message).
    REQUIRE(hist->front().summary == "Merge branch 'feature' into main");
}

TEST_CASE("commitMerge refuses while conflicts remain", "[merge]")
{
    gittide::test::TempRepo tmp;
    auto repo = conflictingRepo(tmp);
    REQUIRE(repo.has_value());
    REQUIRE(repo->mergeBranch("feature")->conflicted);

    auto oid = repo->commitMerge(gittide::CommitRequest{"premature"});
    REQUIRE_FALSE(oid.has_value());          // still conflicted → error
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Implement.**

```cpp
Expected<std::string> GitRepo::commitMerge(CommitRequest req)
{
    git_index* index = nullptr;
    int rc           = git_repository_index(&index, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)> index_guard(index, git_index_free);

    if (git_index_has_conflicts(index))
        return std::unexpected(GitError{-1, "cannot commit: unresolved conflicts remain"});

    git_oid tree_oid;
    rc = git_index_write_tree(&tree_oid, index);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_tree* tree = nullptr;
    rc = git_tree_lookup(&tree, m_repo, &tree_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);

    // Parents: current HEAD, then every MERGE_HEAD.
    std::vector<git_commit*> parents;
    auto free_parents = [&]() { for (auto* p : parents) git_commit_free(p); };

    git_oid head_oid;
    rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* head_commit = nullptr;
    rc = git_commit_lookup(&head_commit, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    parents.push_back(head_commit);

    struct CbData { GitRepo* self; std::vector<git_commit*>* parents; int rc; };
    CbData cb{this, &parents, 0};
    git_repository_mergehead_foreach(
        m_repo,
        [](const git_oid* oid, void* payload) -> int {
            auto* d = static_cast<CbData*>(payload);
            git_commit* c = nullptr;
            int r = git_commit_lookup(&c, d->self->m_repo, oid);
            if (r < 0) { d->rc = r; return r; }
            d->parents->push_back(c);
            return 0;
        },
        &cb);
    if (cb.rc < 0) { free_parents(); return std::unexpected(lastGitError(cb.rc)); }

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig) { free_parents(); return std::unexpected(GitError{-1, "no signature for merge commit"}); }
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    git_oid commit_oid;
    rc = git_commit_create(&commit_oid, m_repo, "HEAD", sig, sig, nullptr,
                           req.message.c_str(), tree,
                           parents.size(),
                           const_cast<const git_commit**>(parents.data()));
    free_parents();
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_repository_state_cleanup(m_repo); // clears MERGE_HEAD / MERGE_MSG

    char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(hex, sizeof(hex), &commit_oid);
    return std::string(hex);
}
```

> `git_repository_mergehead_foreach` takes a C callback; `m_repo` is private, so
> the lambda captures nothing and reaches it through the `CbData::self` pointer.

- [ ] **Step 4: Run — expect PASS** (both cases). Full core suite green.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): commitMerge — 2-parent merge commit + state cleanup"`

---

## Task 5: `abortMerge()` — back to pre-merge

Reset `--hard` to `HEAD` (the merge has not committed, so `HEAD` is still the
pre-merge commit) and clear the merge state, discarding markers and the half-merged
worktree.

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `abortMerge`)
- Modify: `core/src/gitrepo.cpp` (implement)
- Test: `tests/test_git_repo_merge.cpp` (append)

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near mergeBranch())
// Abort an in-progress merge: reset --hard to HEAD and clear MERGE_HEAD/MERGE_MSG,
// returning the worktree to its pre-merge state.
Expected<void> abortMerge();
```

- [ ] **Step 1: Write the failing test.**

```cpp
TEST_CASE("abortMerge restores the pre-merge state", "[merge]")
{
    gittide::test::TempRepo tmp;
    auto repo = conflictingRepo(tmp);
    REQUIRE(repo.has_value());
    const std::string before = repo->head()->oid;
    REQUIRE(repo->mergeBranch("feature")->conflicted);

    REQUIRE(repo->abortMerge().has_value());

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE_FALSE(ms->inProgress);
    REQUIRE(repo->head()->oid == before);     // HEAD unchanged
    std::ifstream in(tmp.path() / "a.txt");
    std::string body((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(body == "main\n");                 // markers gone, our side restored
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Implement.**

```cpp
Expected<void> GitRepo::abortMerge()
{
    git_oid head_oid;
    int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* head = nullptr;
    rc = git_commit_lookup(&head, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> head_guard(head, git_commit_free);

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy    = GIT_CHECKOUT_FORCE; // discard the half-merged worktree
    rc = git_reset(m_repo, reinterpret_cast<const git_object*>(head), GIT_RESET_HARD, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_repository_state_cleanup(m_repo);
    return {};
}
```

- [ ] **Step 4: Run — expect PASS.** Full core suite green.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): abortMerge — reset --hard HEAD + state cleanup"`

---

## Task 6: `stashSave()` / `stashPop()` — controller-owned auto-stash

The merge auto-stash pop is **deferred past a conflicted merge until commitMerge**
(D31), so the stash lifecycle spans multiple controller calls and cannot live
inside one core method. Expose the two primitives the controller (Plan 14b) drives.
These reuse the exact stash mechanics already proven in `safeSwitch`.

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare `stashSave`, `stashPop`)
- Modify: `core/src/gitrepo.cpp` (implement)
- Test: `tests/test_git_repo_merge.cpp` (append)

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near checkoutBranch())
// Stash the working tree (including untracked) if it is dirty. Returns true if a
// stash was created, false if the tree was already clean (nothing stashed).
Expected<bool> stashSave(std::string message);

// Pop the most-recent stash onto the working tree. Errors (and preserves the
// stash) if the pop conflicts.
Expected<void> stashPop();
```

- [ ] **Step 1: Write the failing test.**

```cpp
TEST_CASE("stashSave then stashPop round-trips a dirty worktree", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "dirty\n");
    auto saved = repo->stashSave("gittide: test");
    REQUIRE(saved.has_value());
    REQUIRE(*saved);                              // it was dirty → stashed
    REQUIRE(repo->status()->empty());             // clean after stash

    REQUIRE(repo->stashPop().has_value());
    std::ifstream in(tmp.path() / "a.txt");
    std::string body((std::istreambuf_iterator<char>(in)), {});
    REQUIRE(body == "dirty\n");                    // change restored
}

TEST_CASE("stashSave returns false on a clean worktree", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto saved = repo->stashSave("gittide: test");
    REQUIRE(saved.has_value());
    REQUIRE_FALSE(*saved);                         // nothing to stash
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Implement** (mirror `safeSwitch`'s stash block).

```cpp
Expected<bool> GitRepo::stashSave(std::string message)
{
    auto st = status();
    if (!st)
        return std::unexpected(st.error());
    if (st->empty())
        return false; // clean — nothing stashed

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "could not build a signature for the stash"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    git_oid stash_oid;
    int rc = git_stash_save(&stash_oid, m_repo, sig, message.c_str(), GIT_STASH_INCLUDE_UNTRACKED);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return true;
}

Expected<void> GitRepo::stashPop()
{
    git_stash_apply_options aopts = GIT_STASH_APPLY_OPTIONS_INIT;
    int rc = git_stash_pop(m_repo, 0, &aopts);
    if (rc < 0)
        // Stash is intentionally preserved — the caller can inspect it.
        return std::unexpected(GitError{rc, "Your changes conflict and are kept in the stash"});
    return {};
}
```

- [ ] **Step 4: Run — expect PASS** (both cases). Full core suite green.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): stashSave/stashPop primitives for deferred merge auto-stash"`

---

## Task 7: `deinitSubmodule()` / `reinitSubmodule()` — reactive submodule retry

Support the reactive submodule-conflict path (D31): de-initialise a conflicted
submodule (empty its working dir so its gitlink merges as a plain pointer) and,
later, re-initialise + update it to its pinned commit. libgit2 has **no
first-class `submodule deinit`**, so de-init is emulated: clear the submodule's
working-tree contents (the gitlink stays in the index/tree). Re-init uses
`git_submodule_update` with init enabled.

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare both)
- Modify: `core/src/gitrepo.cpp` (implement; `<git2/submodule.h>` already included)
- Test: `tests/test_git_repo_submodule_merge.cpp` (new file)
- Modify: `tests/CMakeLists.txt` (add the new test)

**Interfaces — Produces:**
```cpp
// gitrepo.hpp (public, near submoduleTree())
// De-initialise a submodule: remove its working-tree contents so its gitlink
// merges as a plain pointer (the gitlink stays in the index). path is repo-relative.
Expected<void> deinitSubmodule(std::filesystem::path path);

// Re-initialise + update a submodule to its pinned commit (git_submodule_update,
// init enabled). path is repo-relative.
Expected<void> reinitSubmodule(std::filesystem::path path);
```

- [ ] **Step 1: Write the failing test.** Build a superproject with one submodule
  (clone a local `file://` temp repo as the submodule), then deinit empties its
  working dir and reinit restores its content.

```cpp
// tests/test_git_repo_submodule_merge.cpp
#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using gittide::GitRepo;

TEST_CASE("deinit empties a submodule working dir; reinit restores it", "[submodule][merge]")
{
    namespace fs = std::filesystem;
    // 1. A repo to be used as the submodule source.
    gittide::test::TempRepo sub;
    sub.setIdentity("Test", "test@example.com");
    sub.writeFile("lib.txt", "v1\n");
    sub.commitAll("sub c1");

    // 2. A superproject that adds `sub` as a submodule at "vendor/lib".
    gittide::test::TempRepo super;
    super.setIdentity("Test", "test@example.com");
    super.writeFile("top.txt", "top\n");
    super.commitAll("super c1");
    // Add + clone the submodule via libgit2 directly in the test (no GitRepo API
    // for `submodule add` yet — this is test scaffolding, not the unit under test).
    // See note below for the exact git_submodule_add_setup/_clone/_add_finalize calls.
    addSubmodule(super.path(), "file://" + sub.path().generic_string(), "vendor/lib");

    auto repo = GitRepo::open(super.path());
    REQUIRE(repo.has_value());
    REQUIRE(fs::exists(super.path() / "vendor/lib/lib.txt"));

    REQUIRE(repo->deinitSubmodule("vendor/lib").has_value());
    REQUIRE_FALSE(fs::exists(super.path() / "vendor/lib/lib.txt")); // working dir emptied

    REQUIRE(repo->reinitSubmodule("vendor/lib").has_value());
    REQUIRE(fs::exists(super.path() / "vendor/lib/lib.txt"));       // restored
}
```

> `addSubmodule(...)` is a small test helper (put it at the top of this test file)
> using `git_submodule_add_setup` → `git_submodule_clone` →
> `git_submodule_add_finalize`, then committing. It is **test scaffolding** for
> building a fixture; `submodule add` is not part of this plan's product surface.
> Write it once here; it depends only on `<git2.h>` (the test target links libgit2).

- [ ] **Step 2: Run — expect FAIL** (undeclared).

- [ ] **Step 3: Implement.**

```cpp
Expected<void> GitRepo::deinitSubmodule(std::filesystem::path path)
{
    // Verify it is actually a submodule (gives a clear error otherwise).
    git_submodule* sm = nullptr;
    int rc = git_submodule_lookup(&sm, m_repo, toGitPath(path).c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_submodule_free(sm);

    // Emulate `submodule deinit`: remove the working-tree contents (including the
    // nested .git) but keep the directory itself. The gitlink stays in the index.
    std::error_code ec;
    const std::filesystem::path wd = workdir() / path;
    for (const auto& entry : std::filesystem::directory_iterator(wd, ec))
        std::filesystem::remove_all(entry.path(), ec);
    if (ec)
        return std::unexpected(GitError{-1, "failed to clear submodule working dir: " + ec.message()});
    return {};
}

Expected<void> GitRepo::reinitSubmodule(std::filesystem::path path)
{
    git_submodule* sm = nullptr;
    int rc = git_submodule_lookup(&sm, m_repo, toGitPath(path).c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_submodule, decltype(&git_submodule_free)> sm_guard(sm, git_submodule_free);

    git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
    rc = git_submodule_update(sm, /*init=*/1, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}
```

- [ ] **Step 4: Run — expect PASS.** Full core suite green. Add the new test to
  `gittide_core_tests`.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(core): submodule deinit/reinit for reactive merge retry"`

---

## Task 8: Close-out

- [ ] **Step 1:** Build + full core suite green; no new warnings.
  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
- [ ] **Step 2:** Confirm Doxygen comments on every new `gitrepo.hpp` method and on
  the `merge.hpp` types. Confirm the engineering spec §Merge & conflict resolution
  matches the shipped API; fix any drift (code is ground truth).
- [ ] **Step 3:** Tick this plan's boxes, fill **Outcome**, set `Status` to `done`
  here and add the row to [`plans/index.md`](index.md).
- [ ] **Step 4: Commit.**
  `git commit -am "docs: close Plan 14a — core merge engine"`

---

## Outcome

> Fill in when the plan reaches `done`. Expected: `GitRepo::{mergeBranch,
> mergeState, commitMerge, abortMerge, stashSave, stashPop, deinitSubmodule,
> reinitSubmodule}`, `StatusFlag::Conflicted`, and `core/include/gittide/merge.hpp`
> (`MergeAnalysis` / `MergeOutcome` / `MergeState`). Spec: engineering §Merge &
> conflict resolution. Code: `core/include/gittide/{merge.hpp,filestatus.hpp,
> gitrepo.hpp}`, `core/src/gitrepo.cpp`; tests `tests/test_git_repo_merge.cpp`,
> `tests/test_git_repo_submodule_merge.cpp`.
