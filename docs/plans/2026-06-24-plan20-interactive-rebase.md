# Plan 20 — Interactive Rebase Engine (Tier 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

| | |
|--|--|
| **Date** | 2026-06-24 |
| **Status** | `planned` |
| **Spec** | [`docs/spec/product/rebase-interactive.md`](../spec/product/rebase-interactive.md) |
| **Depends on** | Plan 19 (plain rebase driver, Tier 1), history-editing round (reword-tip, `RewordDialog.qml`, multi-select), Plan 14 (merge/conflict UI), Plan 16 (context menus) |

**Goal:** Add an interactive rebase engine — a todo-list editor over `base..HEAD` supporting reorder / drop / squash / fixup / reword — driven by a manual cherry-pick loop with two pause reasons (conflict + mid-rebase message), reusing the Tier 1 banner and conflict UI.

**Architecture:** libgit2's `git_rebase_*` cannot drive a user-edited todo (no API to inject reorder/squash/drop), so the engine is **manual**: detach HEAD at `base`, `git_cherrypick` each kept commit on the detached HEAD (the branch ref is left untouched until the very end, making abort trivial), `git_commit_amend` HEAD for squash/fixup, skip for drop. State lives in a GitTide-private dir `<gitdir>/gittide-rebase/`; `RebaseState` is still derived from disk every call (D30). Tier 1's `continueRebase`/`skipRebase`/`abortRebase` are extended to dispatch to the interactive engine when our state dir is present — one verb set, two engines. The UI is purely additive: a new `RebaseTodoDialog`, a commit-menu entry, the existing `RebaseBanner` extended for a Message pause, and `RewordDialog` reused as the message editor.

**Tech Stack:** C++23, libgit2 (`git2/cherrypick.h`, `git2/commit.h`, `git2/graph.h`, `git2/reset.h`), `std::expected`, `std::optional`; Qt Quick/QML, QCoro, `Q_INVOKABLE`/`Q_PROPERTY`; Catch2 (core), QTest (ui).

## Global Constraints

- **No Qt in `core/`** — `rebase.hpp`/`gitrepo.*` stay pure `std`; Qt only at the ViewModel boundary.
- **libgit2 and nlohmann/json are PRIVATE to `core/`** — no public header includes libgit2; `rebase.hpp` uses only `std`.
- **Errors are values:** core returns `Expected<T>` = `std::expected<T, GitError>`; no exceptions across layers.
- **State is disk-truth (D30):** `RebaseState` is derived every call from the repository / our state dir, never cached.
- **Auto-stash lives in the controller (D31):** core verbs assume a clean tree; the controller stashes/pops via `stashSave` + the shared `m_pendingStashPop`.
- **Mutual exclusion (D33):** at most one of {merge, plain rebase, interactive rebase} in progress; each guards on the others.
- **Paths via `generic_u8string()`, never `.string()`.** Use the libgit2 API; never build git command strings. OID→hex via `char buf[GIT_OID_SHA1_HEXSIZE + 1]; git_oid_tostr(buf, sizeof(buf), &oid);` (the codebase's idiom).
- **Colour comes from a theme token**, never a hex literal in a widget. Never signal state by colour alone (D19) — pair with an icon/letter.
- **TDD:** write the failing test first.
- New `ui/` C++ sources already exist (modified, not new); new QML files → `ui/qml/qml.qrc` **and** `ui/CMakeLists.txt`; new core tests → `tests/CMakeLists.txt`; new ui tests → `tests/CMakeLists.txt` **and** `tests/ui/main.cpp` (`#include` + `RUN(...)`).
- Build: `cmake --build build --parallel`. Core tests: `ctest --test-dir build --output-on-failure -R gittide_core_tests`. UI tests: `... -R gittide_ui_tests`.

---

## File Structure

**Modified `core/`:**
- `core/include/gittide/rebase.hpp` — add `RebaseAction`, `RebaseTodoEntry`, `RebaseTodo`, `RebasePause`; extend `RebaseState` + `RebaseOutcome`.
- `core/include/gittide/gitrepo.hpp` — declare `startInteractiveRebase`; extend `continueRebase` signature; declare private engine helpers + nested `InteractiveState`.
- `core/src/gitrepo.cpp` — the manual engine; dispatch in `continueRebase`/`skipRebase`/`abortRebase`/`rebaseState`; extend `startRebase` guard.

**Modified `ui/`:** `asyncrepo.*`, `repocontroller.*`, `repoviewmodel.*`, `RebaseBanner.qml`, `CommitContextMenu.qml`, `HistoryPane.qml`, `qml.qrc`, `ui/CMakeLists.txt`.

**New `ui/qml/`:** `RebaseTodoDialog.qml`.

**New tests:** `tests/test_git_repo_interactive_rebase.cpp`; `tests/ui/test_repocontroller_interactive_rebase.cpp`.

---

## Task 1: Plan types — `RebaseAction` / `RebaseTodo` / `RebasePause`, extended state

**Files:**
- Modify: `core/include/gittide/rebase.hpp`
- Test: `tests/test_git_repo_interactive_rebase.cpp` (new), `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `gittide::RebaseAction` (enum: `Pick, Reword, Squash, Fixup, Drop`), `gittide::RebaseTodoEntry { RebaseAction action; std::string oid; }`, `gittide::RebaseTodo { std::string base; std::vector<RebaseTodoEntry> entries; }`, `gittide::RebasePause` (enum: `None, Conflict, Message`).
- Produces: extended `RebaseState` (adds `bool interactive`, `RebasePause pause`, `std::string messagePrefill`) and `RebaseOutcome` (adds `RebasePause pause`).

- [ ] **Step 1: Write the failing test**

Create `tests/test_git_repo_interactive_rebase.cpp`:

```cpp
#include "gittide/rebase.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("RebaseTodo and extended RebaseState have sane defaults", "[rebase-i]")
{
    gittide::RebaseTodo todo;
    REQUIRE(todo.base.empty());
    REQUIRE(todo.entries.empty());

    gittide::RebaseTodoEntry e{gittide::RebaseAction::Squash, "abc"};
    REQUIRE(e.action == gittide::RebaseAction::Squash);
    REQUIRE(e.oid == "abc");

    gittide::RebaseState st;
    REQUIRE_FALSE(st.interactive);
    REQUIRE(st.pause == gittide::RebasePause::None);
    REQUIRE(st.messagePrefill.empty());

    gittide::RebaseOutcome out;
    REQUIRE_FALSE(out.conflicted);
    REQUIRE(out.pause == gittide::RebasePause::None);
}
```

- [ ] **Step 2: Register the test in `tests/CMakeLists.txt`**

Add under the `gittide_core_tests` sources, next to `test_git_repo_rebase.cpp`:

```cmake
  test_git_repo_interactive_rebase.cpp
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: FAIL to compile — `RebaseAction`/`RebaseTodo`/`pause` undefined.

- [ ] **Step 4: Extend `rebase.hpp`**

Replace the file body (inside `namespace gittide`) so it reads:

```cpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace gittide {

/// A single interactive-rebase instruction.
enum class RebaseAction
{
    Pick,    ///< replay the commit as-is
    Reword,  ///< replay, then pause to edit the message
    Squash,  ///< fold into the previous commit, pause to edit the combined message
    Fixup,   ///< fold into the previous commit, keep the previous message (no pause)
    Drop     ///< discard the commit
};

/// Why an interactive rebase is paused (or None when running / finished).
enum class RebasePause
{
    None,
    Conflict,  ///< the current cherry-pick left conflicts to resolve
    Message    ///< a reword/squash step needs a new/combined message
};

struct RebaseTodoEntry
{
    RebaseAction action = RebaseAction::Pick;
    std::string  oid;        ///< 40-char hex of the original commit
};

/// An interactive plan: replay `entries` (in list order) on top of `base`.
struct RebaseTodo
{
    std::string                  base;     ///< oid to detach onto (parent-of-oldest)
    std::vector<RebaseTodoEntry> entries;  ///< oldest first (git todo order)
};

/// Result of advancing a rebase (start/continue/skip).
struct RebaseOutcome
{
    bool        conflicted = false;             ///< true => paused on a conflicting step
    RebasePause pause      = RebasePause::None; ///< why we paused (or None when finished)
};

/// Rebase-in-progress state, ALWAYS derived from the repository (D30).
struct RebaseState
{
    bool        inProgress  = false; ///< a plain (libgit2) OR interactive (our dir) rebase is live
    bool        interactive = false; ///< true => driven by the manual cherry-pick engine
    RebasePause pause       = RebasePause::None;
    std::string ontoRef;             ///< plain driver: target branch shorthand; empty for interactive
    int         current = 0;         ///< current step, 1-based (0 when none)
    int         total   = 0;         ///< total steps (interactive: non-drop entries)
    std::string stepSummary;         ///< summary of the commit being applied; may be empty
    std::string messagePrefill;      ///< pause == Message: prefilled editor text
    std::vector<std::filesystem::path> conflictedPaths;       ///< all conflicted entries
    std::vector<std::filesystem::path> conflictedSubmodules;  ///< gitlink subset of the above
};

} // namespace gittide
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/rebase.hpp tests/test_git_repo_interactive_rebase.cpp tests/CMakeLists.txt
git commit -m "feat(core): interactive rebase plan types + extended RebaseState"
```

---

## Task 2: State-dir helpers + `startInteractiveRebase` (pick / drop / reorder) + finish

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_interactive_rebase.cpp`

**Interfaces:**
- Consumes: Task 1 types; existing `lastGitError`, `fromGitPath`, `commitMessage`.
- Produces (public): `Expected<RebaseOutcome> GitRepo::startInteractiveRebase(RebaseTodo todo);`
- Produces (private): nested `struct InteractiveState { RebaseTodo todo; int done = 0; bool applied = false; std::string branch; std::string origHead; };`; helpers `std::filesystem::path interactiveRebaseDir() const;`, `bool interactiveRebaseInProgress() const;`, `Expected<void> initInteractiveState(const RebaseTodo&, const std::string& branch, const std::string& origHead);`, `Expected<InteractiveState> loadInteractiveState() const;`, `Expected<void> setInteractiveProgress(int done, bool applied) const;`, `void clearInteractiveState() const;`, `Expected<RebaseOutcome> driveInteractive(std::optional<std::string> message);`, `Expected<RebaseOutcome> finishInteractive(const InteractiveState&);`.

> This task drives only `Pick`/`Drop` entries to a clean finish (no conflicts, no reword/squash) — the structural core. Reword/squash/fixup and conflict handling arrive in Tasks 3–6, all inside the same `driveInteractive` loop.

- [ ] **Step 1: Declare in `gitrepo.hpp`**

Add the include near the other git2-free model includes (already present: `#include "gittide/rebase.hpp"`). Add `#include <optional>` to the std includes. In the **public** section, next to `startRebase`:

```cpp
    /// Begin an interactive rebase of the current branch per `todo` (base + ordered
    /// entries, oldest first). Assumes a clean worktree (controller auto-stashes,
    /// D31). Detaches HEAD at todo.base, then drives until the first pause (conflict
    /// or message) or a clean finish. Errors: unborn/detached HEAD, base not an
    /// ancestor of HEAD, first entry squash/fixup, all-drop plan, a rebase/merge
    /// already in progress.
    Expected<RebaseOutcome> startInteractiveRebase(RebaseTodo todo);
```

Change the existing `continueRebase` declaration to accept an optional message (default keeps every existing caller valid):

```cpp
    /// Continue an in-progress rebase. For an interactive Message pause, `message`
    /// MUST be supplied (the reword/combined-squash text). For a Conflict pause the
    /// resolved index is committed; pass nullopt. Errors if not rebasing, conflicts
    /// remain, or a Message pause is continued without a message.
    Expected<RebaseOutcome> continueRebase(std::optional<std::string> message = std::nullopt);
```

In the **private** section, next to `driveRebase`:

```cpp
    // Interactive (manual cherry-pick) engine state, persisted under
    // interactiveRebaseDir(). See rebase-interactive.md §2.3.
    struct InteractiveState
    {
        RebaseTodo  todo;
        int         done    = 0;     ///< fully-committed entries
        bool        applied = false; ///< current entry cherry-picked, awaiting commit/message
        std::string branch;          ///< branch shorthand being rewritten
        std::string origHead;        ///< pre-rebase branch tip (for abort)
    };

    std::filesystem::path interactiveRebaseDir() const;   // <gitdir>/gittide-rebase
    bool interactiveRebaseInProgress() const;             // the dir exists
    Expected<void> initInteractiveState(const RebaseTodo& todo, const std::string& branch,
                                        const std::string& origHead);
    Expected<InteractiveState> loadInteractiveState() const;
    Expected<void> setInteractiveProgress(int done, bool applied) const;
    void clearInteractiveState() const;
    // The manual driver: apply entries from the cursor until a pause or finish.
    Expected<RebaseOutcome> driveInteractive(std::optional<std::string> message);
    // All entries applied → move the branch to detached HEAD and reattach.
    Expected<RebaseOutcome> finishInteractive(const InteractiveState& st);
    // Build a RebaseState from the interactive state dir (D30).
    RebaseState interactiveRebaseState() const;
```

- [ ] **Step 2: Write the failing test**

Append to `tests/test_git_repo_interactive_rebase.cpp`:

```cpp
#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <git2.h>

using gittide::GitRepo;
using gittide::RebaseAction;
using gittide::RebaseTodo;
using gittide::RebaseTodoEntry;

namespace {
// Parent oid of `oid` (first parent), as 40-char hex — the detach base for an
// "edit from oid" plan whose oldest entry is `oid`.
std::string firstParentOf(GitRepo& repo, const std::string& oid)
{
    auto msg = repo.commitMessage(oid); // ensure the commit exists
    REQUIRE(msg.has_value());
    git_repository* raw = nullptr;
    REQUIRE(git_repository_open(&raw, repo.path().string().c_str()) == 0);
    git_oid o; REQUIRE(git_oid_fromstr(&o, oid.c_str()) == 0);
    git_commit* c = nullptr; REQUIRE(git_commit_lookup(&c, raw, &o) == 0);
    const git_oid* p = git_commit_parent_id(c, 0);
    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), p);
    git_commit_free(c); git_repository_free(raw);
    return buf;
}
} // namespace

TEST_CASE("interactive rebase reorders two non-conflicting commits", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    tmp.writeFile("a.txt", "a\n");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.commitAll("A");                       // HEAD~1
    auto headA = repo->head();                // after A
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B");                       // HEAD

    auto hist = repo->log(10);
    REQUIRE(hist.has_value());
    // history newest-first: [B, A, c0]
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(*repo, oidA); // = c0

    RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidB},   // B first now
                     {RebaseAction::Pick, oidA} };  // then A
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);
    REQUIRE(out->pause == gittide::RebasePause::None);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    auto after = repo->log(10);
    REQUIRE(after.has_value());
    // newest-first now [A, B, c0]
    REQUIRE(after->at(0).summary == "A");
    REQUIRE(after->at(1).summary == "B");
    REQUIRE(std::filesystem::exists(tmp.path() / "a.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "b.txt"));
}

TEST_CASE("interactive rebase drops a commit", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A");
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(*repo, oidA);

    RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Drop, oidA},   // drop A
                     {RebaseAction::Pick, oidB} };  // keep B
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);

    REQUIRE_FALSE(std::filesystem::exists(tmp.path() / "a.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "b.txt"));
}
```

> If `GraphRow`'s summary field is named differently than `.summary`, match the existing field (grep `struct GraphRow` in `core/include/gittide/graph.hpp`). `repo.path()` returns the working-dir `std::filesystem::path` (used elsewhere in tests).

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: FAIL — `startInteractiveRebase` undefined.

- [ ] **Step 4: Add includes + state-dir helpers in `gitrepo.cpp`**

Near the other `#include <git2/...>` lines add `#include <git2/cherrypick.h>` and `#include <git2/graph.h>` (if not already present). Ensure `<fstream>`, `<filesystem>`, `<optional>` are included. Implement, near `rebaseOntoName`:

```cpp
std::filesystem::path GitRepo::interactiveRebaseDir() const
{
    const char* gp = git_repository_path(m_repo); // the .git dir (trailing slash)
    if (!gp)
        return {};
    return std::filesystem::path(fromGitPath(gp)) / "gittide-rebase";
}

bool GitRepo::interactiveRebaseInProgress() const
{
    std::error_code ec;
    return std::filesystem::exists(interactiveRebaseDir() / "todo", ec);
}

static const char* actionToken(gittide::RebaseAction a)
{
    using A = gittide::RebaseAction;
    switch (a)
    {
        case A::Pick:   return "pick";
        case A::Reword: return "reword";
        case A::Squash: return "squash";
        case A::Fixup:  return "fixup";
        case A::Drop:   return "drop";
    }
    return "pick";
}

static gittide::RebaseAction tokenToAction(const std::string& t)
{
    using A = gittide::RebaseAction;
    if (t == "reword") return A::Reword;
    if (t == "squash") return A::Squash;
    if (t == "fixup")  return A::Fixup;
    if (t == "drop")   return A::Drop;
    return A::Pick;
}

Expected<void> GitRepo::initInteractiveState(const RebaseTodo& todo, const std::string& branch,
                                             const std::string& origHead)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = interactiveRebaseDir();
    fs::create_directories(dir, ec);
    if (ec)
        return std::unexpected(GitError{-1, "cannot create rebase state dir"});

    { std::ofstream(dir / "base")      << todo.base    << "\n"; }
    { std::ofstream(dir / "branch")    << branch       << "\n"; }
    { std::ofstream(dir / "orig-head") << origHead     << "\n"; }
    {
        std::ofstream todoFile(dir / "todo");
        for (const auto& e : todo.entries)
            todoFile << actionToken(e.action) << ' ' << e.oid << "\n";
    }
    { std::ofstream(dir / "done") << 0 << "\n"; }
    std::error_code rmEc;
    fs::remove(dir / "applied", rmEc); // ensure clean
    return {};
}

Expected<GitRepo::InteractiveState> GitRepo::loadInteractiveState() const
{
    namespace fs = std::filesystem;
    const fs::path dir = interactiveRebaseDir();
    std::error_code ec;
    if (!fs::exists(dir / "todo", ec))
        return std::unexpected(GitError{-1, "no interactive rebase in progress"});

    InteractiveState st;
    auto readLine = [&](const char* name) -> std::string {
        std::ifstream in(dir / name);
        std::string line;
        std::getline(in, line);
        return line;
    };
    st.todo.base = readLine("base");
    st.branch    = readLine("branch");
    st.origHead  = readLine("orig-head");
    {
        std::ifstream todoFile(dir / "todo");
        std::string line;
        while (std::getline(todoFile, line))
        {
            if (line.empty())
                continue;
            const auto sp = line.find(' ');
            if (sp == std::string::npos)
                continue;
            RebaseTodoEntry e;
            e.action = tokenToAction(line.substr(0, sp));
            e.oid    = line.substr(sp + 1);
            st.todo.entries.push_back(e);
        }
    }
    try { st.done = std::stoi(readLine("done")); } catch (...) { st.done = 0; }
    st.applied = fs::exists(dir / "applied", ec);
    return st;
}

Expected<void> GitRepo::setInteractiveProgress(int done, bool applied) const
{
    namespace fs = std::filesystem;
    const fs::path dir = interactiveRebaseDir();
    { std::ofstream(dir / "done") << done << "\n"; }
    std::error_code ec;
    if (applied)
        { std::ofstream(dir / "applied") << "1\n"; }
    else
        fs::remove(dir / "applied", ec);
    return {};
}

void GitRepo::clearInteractiveState() const
{
    std::error_code ec;
    std::filesystem::remove_all(interactiveRebaseDir(), ec);
}
```

- [ ] **Step 5: Implement `startInteractiveRebase` + `driveInteractive` (pick/drop) + `finishInteractive`**

Add near `startRebase`:

```cpp
Expected<RebaseOutcome> GitRepo::startInteractiveRebase(RebaseTodo todo)
{
    // Guards (D33 mutual exclusion + structural).
    if (interactiveRebaseInProgress())
        return std::unexpected(GitError{-1, "cannot rebase: a rebase is already in progress"});
    if (git_repository_state(m_repo) != GIT_REPOSITORY_STATE_NONE)
        return std::unexpected(GitError{-1, "cannot rebase: another operation is in progress"});
    if (git_repository_head_unborn(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is unborn"});
    if (git_repository_head_detached(m_repo) == 1)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is detached"});
    if (todo.entries.empty())
        return std::unexpected(GitError{-1, "cannot rebase: empty plan"});
    if (todo.entries.front().action == RebaseAction::Squash
        || todo.entries.front().action == RebaseAction::Fixup)
        return std::unexpected(GitError{-1, "cannot rebase: first entry cannot be squash/fixup"});
    bool anyKeep = false;
    for (const auto& e : todo.entries)
        if (e.action != RebaseAction::Drop) { anyKeep = true; break; }
    if (!anyKeep)
        return std::unexpected(GitError{-1, "cannot rebase: all entries dropped"});

    // Current branch + tip.
    git_reference* head_ref = nullptr;
    if (git_repository_head(&head_ref, m_repo) < 0)
        return std::unexpected(GitError{-1, "cannot resolve HEAD"});
    std::unique_ptr<git_reference, decltype(&git_reference_free)> hr(head_ref, git_reference_free);
    const char* branch_name = nullptr;
    if (git_branch_name(&branch_name, head_ref) < 0 || !branch_name)
        return std::unexpected(GitError{-1, "cannot rebase: HEAD is not a branch"});
    const std::string branch = branch_name;

    git_oid orig_oid;
    if (git_reference_name_to_id(&orig_oid, m_repo, "HEAD") < 0)
        return std::unexpected(GitError{-1, "cannot resolve HEAD"});
    char origbuf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(origbuf, sizeof(origbuf), &orig_oid);
    const std::string origHead = origbuf;

    // base must be an ancestor of HEAD.
    git_oid base_oid;
    if (git_oid_fromstr(&base_oid, todo.base.c_str()) < 0)
        return std::unexpected(GitError{-1, "cannot rebase: bad base oid"});
    if (git_graph_descendant_of(m_repo, &orig_oid, &base_oid) != 1)
        return std::unexpected(GitError{-1, "cannot rebase: base is not an ancestor of HEAD"});

    // Detach HEAD onto base.
    git_commit* base_commit = nullptr;
    if (git_commit_lookup(&base_commit, m_repo, &base_oid) < 0)
        return std::unexpected(GitError{-1, "cannot rebase: base is not a commit"});
    std::unique_ptr<git_commit, decltype(&git_commit_free)> bg(base_commit, git_commit_free);
    {
        git_checkout_options co = GIT_CHECKOUT_OPTIONS_INIT;
        co.checkout_strategy    = GIT_CHECKOUT_FORCE;
        if (int rc = git_checkout_tree(m_repo, reinterpret_cast<const git_object*>(base_commit), &co); rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    if (int rc = git_repository_set_head_detached(m_repo, &base_oid); rc < 0)
        return std::unexpected(lastGitError(rc));

    if (auto r = initInteractiveState(todo, branch, origHead); !r)
        return std::unexpected(r.error());
    return driveInteractive(std::nullopt);
}

Expected<RebaseOutcome> GitRepo::driveInteractive(std::optional<std::string> message)
{
    auto loaded = loadInteractiveState();
    if (!loaded)
        return std::unexpected(loaded.error());
    InteractiveState st = *loaded;

    git_signature* sig = nullptr;
    if (git_signature_default(&sig, m_repo) < 0)
        git_signature_now(&sig, "GitTide", "gittide@localhost");
    if (!sig)
        return std::unexpected(GitError{-1, "no signature for rebase"});
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(sig, git_signature_free);

    while (st.done < static_cast<int>(st.todo.entries.size()))
    {
        const RebaseTodoEntry& e = st.todo.entries[st.done];

        if (e.action == RebaseAction::Drop)
        {
            st.applied = false;
            ++st.done;
            if (auto r = setInteractiveProgress(st.done, st.applied); !r)
                return std::unexpected(r.error());
            message.reset();
            continue;
        }

        // 1) Apply the commit if not already applied.
        if (!st.applied)
        {
            git_oid pick_oid;
            if (git_oid_fromstr(&pick_oid, e.oid.c_str()) < 0)
                return std::unexpected(GitError{-1, "bad oid in todo"});
            git_commit* pick = nullptr;
            if (int rc = git_commit_lookup(&pick, m_repo, &pick_oid); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_commit, decltype(&git_commit_free)> pg(pick, git_commit_free);

            git_cherrypick_options copts = GIT_CHERRYPICK_OPTIONS_INIT;
            copts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
            if (int rc = git_cherrypick(m_repo, pick, &copts); rc < 0)
                return std::unexpected(lastGitError(rc));

            st.applied = true;
            if (auto r = setInteractiveProgress(st.done, st.applied); !r)
                return std::unexpected(r.error());

            git_index* index = nullptr;
            if (int rc = git_repository_index(&index, m_repo); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
            if (git_index_has_conflicts(index))
                return RebaseOutcome{true, RebasePause::Conflict};
        }
        else
        {
            // Re-entry after a conflict was resolved + staged.
            git_index* index = nullptr;
            if (int rc = git_repository_index(&index, m_repo); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
            if (git_index_has_conflicts(index))
                return std::unexpected(GitError{-1, "cannot continue: unresolved conflicts remain"});
        }

        // 2) reword/squash need a message before we can commit.
        if ((e.action == RebaseAction::Reword || e.action == RebaseAction::Squash)
            && !message.has_value())
            return RebaseOutcome{false, RebasePause::Message};

        // Build the tree from the resolved index.
        git_oid tree_oid;
        {
            git_index* index = nullptr;
            if (int rc = git_repository_index(&index, m_repo); rc < 0)
                return std::unexpected(lastGitError(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)> ig(index, git_index_free);
            if (int rc = git_index_write_tree(&tree_oid, index); rc < 0)
                return std::unexpected(lastGitError(rc));
        }
        git_tree* tree = nullptr;
        if (int rc = git_tree_lookup(&tree, m_repo, &tree_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_tree, decltype(&git_tree_free)> tg(tree, git_tree_free);

        // current detached HEAD commit = parent (pick/reword) or amend target (squash/fixup)
        git_oid head_oid;
        if (int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD"); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_commit* head = nullptr;
        if (int rc = git_commit_lookup(&head, m_repo, &head_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> hg(head, git_commit_free);

        // original commit for author + message
        git_oid orig_oid;
        git_oid_fromstr(&orig_oid, e.oid.c_str());
        git_commit* orig = nullptr;
        if (int rc = git_commit_lookup(&orig, m_repo, &orig_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> og(orig, git_commit_free);

        git_oid newc;
        int rc = 0;
        if (e.action == RebaseAction::Pick)
        {
            const git_commit* parents[1] = {head};
            rc = git_commit_create(&newc, m_repo, "HEAD", git_commit_author(orig), sig,
                                   nullptr, git_commit_message(orig), tree, 1, parents);
        }
        else if (e.action == RebaseAction::Reword)
        {
            const git_commit* parents[1] = {head};
            rc = git_commit_create(&newc, m_repo, "HEAD", git_commit_author(orig), sig,
                                   nullptr, message->c_str(), tree, 1, parents);
        }
        else if (e.action == RebaseAction::Fixup)
        {
            rc = git_commit_amend(&newc, head, "HEAD", git_commit_author(head), sig,
                                  nullptr, git_commit_message(head), tree);
        }
        else // Squash
        {
            rc = git_commit_amend(&newc, head, "HEAD", git_commit_author(head), sig,
                                  nullptr, message->c_str(), tree);
        }
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        git_repository_state_cleanup(m_repo); // clear CHERRY_PICK_HEAD/MERGE_MSG
        st.applied = false;
        ++st.done;
        if (auto r = setInteractiveProgress(st.done, st.applied); !r)
            return std::unexpected(r.error());
        message.reset(); // a message applies only to the step that paused for it
    }

    return finishInteractive(st);
}

Expected<RebaseOutcome> GitRepo::finishInteractive(const InteractiveState& st)
{
    git_oid head_oid;
    if (int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD"); rc < 0)
        return std::unexpected(lastGitError(rc));

    const std::string refname = "refs/heads/" + st.branch;
    git_reference* newref = nullptr;
    if (int rc = git_reference_create(&newref, m_repo, refname.c_str(), &head_oid,
                                      /*force=*/1, "gittide: interactive rebase"); rc < 0)
        return std::unexpected(lastGitError(rc));
    git_reference_free(newref);

    if (int rc = git_repository_set_head(m_repo, refname.c_str()); rc < 0)
        return std::unexpected(lastGitError(rc));

    clearInteractiveState();
    git_repository_state_cleanup(m_repo);
    return RebaseOutcome{false, RebasePause::None};
}
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS (reorder + drop). `rebaseState()` still compiles (interactive detection added in Task 3).

- [ ] **Step 7: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_interactive_rebase.cpp
git commit -m "feat(core): interactive rebase engine — state dir + pick/drop/reorder + finish"
```

---

## Task 3: `rebaseState()` interactive branch + conflict pause + continue/skip/abort dispatch

**Files:**
- Modify: `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_interactive_rebase.cpp`

**Interfaces:**
- Consumes: Task 2 helpers + `driveInteractive`.
- Produces: `RebaseState GitRepo::interactiveRebaseState() const;` (declared in Task 2); dispatch added to `continueRebase`/`skipRebase`/`abortRebase`; `startRebase` guard extended.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_git_repo_interactive_rebase.cpp`:

```cpp
#include <fstream>

TEST_CASE("interactive rebase pauses on conflict, continue finishes after resolve", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "A\n");            // edits same line
    tmp.commitAll("A");
    tmp.writeFile("a.txt", "B\n");            // edits same line again
    tmp.commitAll("B");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(*repo, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidB},   // reorder → B before A conflicts
                     {RebaseAction::Pick, oidA} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);
    REQUIRE(out->pause == gittide::RebasePause::Conflict);

    auto st = repo->rebaseState();
    REQUIRE(st.inProgress);
    REQUIRE(st.interactive);
    REQUIRE(st.pause == gittide::RebasePause::Conflict);
    REQUIRE(st.total == 2);
    REQUIRE(st.conflictedPaths.size() == 1);

    tmp.writeFile("a.txt", "resolved\n");
    REQUIRE(repo->stage(gittide::StageSelection{ {"a.txt"} }).has_value());

    auto cont = repo->continueRebase();
    REQUIRE(cont.has_value());
    // second pick (A) also conflicts against the resolved B → resolve again
    if (cont->conflicted)
    {
        tmp.writeFile("a.txt", "resolved2\n");
        REQUIRE(repo->stage(gittide::StageSelection{ {"a.txt"} }).has_value());
        cont = repo->continueRebase();
        REQUIRE(cont.has_value());
    }
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);
}

TEST_CASE("interactive abort restores the exact pre-rebase tip", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "A\n");
    tmp.commitAll("A");
    tmp.writeFile("a.txt", "B\n");
    tmp.commitAll("B");
    auto before = repo->head();
    REQUIRE(before.has_value());

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(*repo, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidB}, {RebaseAction::Pick, oidA} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);

    REQUIRE(repo->abortRebase().has_value());
    REQUIRE_FALSE(repo->rebaseState().inProgress);
    auto after = repo->head();
    REQUIRE(after.has_value());
    REQUIRE(after->oid == before->oid);
}
```

> Confirm `StageSelection`'s shape (grep `struct StageSelection` in `core/include/gittide/diff.hpp`) — if it is not a brace-initializable `{ std::vector<std::filesystem::path> paths; }`, use whatever single-path stage the merge tests use to stage a resolved file.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: FAIL — `rebaseState()` reports `inProgress == false` for the interactive engine; abort errors "no rebase in progress".

- [ ] **Step 3: Implement `interactiveRebaseState` + dispatch**

Add `interactiveRebaseState()` (uses `commitMessage`, already a member):

```cpp
RebaseState GitRepo::interactiveRebaseState() const
{
    RebaseState st;
    auto loaded = loadInteractiveState();
    if (!loaded)
        return st; // dir vanished mid-read → not in progress
    const InteractiveState& s = *loaded;
    st.inProgress  = true;
    st.interactive = true;

    const auto& entries = s.todo.entries;
    for (const auto& e : entries)
        if (e.action != RebaseAction::Drop)
            ++st.total;
    int committedKeep = 0;
    for (int i = 0; i < s.done && i < static_cast<int>(entries.size()); ++i)
        if (entries[i].action != RebaseAction::Drop)
            ++committedKeep;
    st.current = committedKeep + 1;
    if (st.current > st.total)
        st.current = st.total;

    const RebaseTodoEntry* cur =
        (s.done < static_cast<int>(entries.size())) ? &entries[s.done] : nullptr;
    if (cur)
    {
        git_oid o;
        if (git_oid_fromstr(&o, cur->oid.c_str()) == 0)
        {
            git_commit* c = nullptr;
            if (git_commit_lookup(&c, m_repo, &o) == 0)
            {
                std::unique_ptr<git_commit, decltype(&git_commit_free)> cg(c, git_commit_free);
                if (const char* sm = git_commit_summary(c))
                    st.stepSummary = sm;
            }
        }
    }

    // Conflicts — identical derivation to mergeState()/rebaseState().
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
                    const git_index_entry* en = our ? our : (their ? their : anc);
                    if (!en || !en->path)
                        continue;
                    std::filesystem::path p = fromGitPath(en->path);
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

    // Pause reason.
    if (!st.conflictedPaths.empty())
        st.pause = RebasePause::Conflict;
    else if (s.applied && cur
             && (cur->action == RebaseAction::Reword || cur->action == RebaseAction::Squash))
    {
        st.pause = RebasePause::Message;
        if (cur->action == RebaseAction::Reword)
        {
            if (auto m = commitMessage(cur->oid))
                st.messagePrefill = *m;
        }
        else // Squash: HEAD's accumulated message + this commit's message
        {
            std::string head;
            git_oid head_oid;
            if (git_reference_name_to_id(&head_oid, m_repo, "HEAD") == 0)
            {
                char hb[GIT_OID_SHA1_HEXSIZE + 1] = {0};
                git_oid_tostr(hb, sizeof(hb), &head_oid);
                if (auto hm = commitMessage(hb))
                    head = *hm;
            }
            std::string mine;
            if (auto m = commitMessage(cur->oid))
                mine = *m;
            st.messagePrefill = head + "\n\n" + mine;
        }
    }
    return st;
}
```

At the **top** of the existing `rebaseState()`, before the libgit2-state logic, add:

```cpp
    if (interactiveRebaseInProgress())
        return interactiveRebaseState();
```

In `continueRebase`, `skipRebase`, `abortRebase`, add an interactive branch as the **first** statement (before the libgit2 `git_repository_state` check). `continueRebase` becomes (note the new signature):

```cpp
Expected<RebaseOutcome> GitRepo::continueRebase(std::optional<std::string> message)
{
    if (interactiveRebaseInProgress())
    {
        // Resolved conflicts must be staged; driveInteractive re-checks.
        return driveInteractive(std::move(message));
    }
    // ---- existing plain-driver body below, unchanged ----
    const int state = git_repository_state(m_repo);
    // ... (unchanged) ...
}
```

`abortRebase`:

```cpp
Expected<void> GitRepo::abortRebase()
{
    if (interactiveRebaseInProgress())
    {
        auto loaded = loadInteractiveState();
        if (!loaded)
        {
            clearInteractiveState();
            return std::unexpected(GitError{-1, "no rebase in progress"});
        }
        const std::string refname = "refs/heads/" + loaded->branch;
        // The branch ref was never moved during the drive → it still points at
        // orig-head. Reattach HEAD and hard-reset the worktree to it.
        if (int rc = git_repository_set_head(m_repo, refname.c_str()); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_oid orig_oid;
        if (git_oid_fromstr(&orig_oid, loaded->origHead.c_str()) < 0)
            return std::unexpected(GitError{-1, "bad orig-head"});
        git_object* obj = nullptr;
        if (int rc = git_object_lookup(&obj, m_repo, &orig_oid, GIT_OBJECT_COMMIT); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_object, decltype(&git_object_free)> og(obj, git_object_free);
        if (int rc = git_reset(m_repo, obj, GIT_RESET_HARD, nullptr); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_repository_state_cleanup(m_repo);
        clearInteractiveState();
        return {};
    }
    // ---- existing plain-driver body below, unchanged ----
    const int state = git_repository_state(m_repo);
    // ... (unchanged) ...
}
```

`skipRebase`:

```cpp
Expected<RebaseOutcome> GitRepo::skipRebase()
{
    if (interactiveRebaseInProgress())
    {
        auto loaded = loadInteractiveState();
        if (!loaded)
            return std::unexpected(loaded.error());
        InteractiveState st = *loaded;
        // Discard the half-applied cherry-pick: hard-reset to detached HEAD.
        git_oid head_oid;
        if (int rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD"); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_commit* head = nullptr;
        if (int rc = git_commit_lookup(&head, m_repo, &head_oid); rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_commit, decltype(&git_commit_free)> hg(head, git_commit_free);
        if (int rc = git_reset(m_repo, reinterpret_cast<const git_object*>(head),
                               GIT_RESET_HARD, nullptr); rc < 0)
            return std::unexpected(lastGitError(rc));
        git_repository_state_cleanup(m_repo);
        // Advance past the current entry without committing it.
        st.applied = false;
        ++st.done;
        if (auto r = setInteractiveProgress(st.done, st.applied); !r)
            return std::unexpected(r.error());
        return driveInteractive(std::nullopt);
    }
    // ---- existing plain-driver body below, unchanged ----
    const int state = git_repository_state(m_repo);
    // ... (unchanged) ...
}
```

Finally, extend the **Tier 1** `startRebase` guard so a paused interactive rebase blocks a plain rebase. After its existing `state != GIT_REPOSITORY_STATE_NONE` check, add:

```cpp
    if (interactiveRebaseInProgress())
        return std::unexpected(GitError{-1, "cannot rebase: a rebase is already in progress"});
```

> Also confirm `mergeBranch` (D33) refuses to start while an interactive rebase is live: grep its guard; if it only checks `git_repository_state`, add `if (interactiveRebaseInProgress()) return std::unexpected(GitError{-1, "cannot merge: a rebase is in progress"});` alongside.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS (conflict pause + abort + Task 2 cases still green).

- [ ] **Step 5: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_interactive_rebase.cpp
git commit -m "feat(core): interactive rebaseState + conflict pause + continue/skip/abort dispatch"
```

---

## Task 4: Reword (Message pause)

**Files:**
- Modify: (none — engine already handles `Reword`; this task is the test that proves it)
- Test: `tests/test_git_repo_interactive_rebase.cpp`

**Interfaces:**
- Consumes: `startInteractiveRebase`, `continueRebase(message)`, `rebaseState()`.

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("interactive reword of an older commit pauses for a message", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A original");
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(*repo, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Reword, oidA}, {RebaseAction::Pick, oidB} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);
    REQUIRE(out->pause == gittide::RebasePause::Message);

    auto st = repo->rebaseState();
    REQUIRE(st.pause == gittide::RebasePause::Message);
    REQUIRE(st.messagePrefill.rfind("A original", 0) == 0); // prefilled with the old message

    auto cont = repo->continueRebase("A reworded\n");
    REQUIRE(cont.has_value());
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    auto after = repo->log(10);
    REQUIRE(after->at(1).summary == "A reworded");
    REQUIRE(after->at(0).summary == "B");
}

TEST_CASE("continue without a message errors on a reword pause", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A");

    auto hist = repo->log(10);
    const std::string oidA = hist->at(0).oid;
    const std::string base = firstParentOf(*repo, oidA);
    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Reword, oidA} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->pause == gittide::RebasePause::Message);

    // Continuing with no message re-pauses (does not finish, does not error-out the repo).
    auto cont = repo->continueRebase(); // nullopt
    REQUIRE(cont.has_value());
    REQUIRE(cont->pause == gittide::RebasePause::Message);
    REQUIRE(repo->rebaseState().inProgress);
}
```

- [ ] **Step 2: Run test to verify it passes (engine already supports reword)**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS. If the prefill assertion fails, verify `interactiveRebaseState()` computes the reword prefill from `commitMessage(cur->oid)`.

- [ ] **Step 3: Commit**

```bash
git add tests/test_git_repo_interactive_rebase.cpp
git commit -m "test(core): interactive reword-older pauses for a message"
```

---

## Task 5: Squash

**Files:**
- Modify: (none — engine handles `Squash`; this is the proof)
- Test: `tests/test_git_repo_interactive_rebase.cpp`

**Interfaces:**
- Consumes: `startInteractiveRebase`, `continueRebase(message)`, `commitFiles`, `log`.

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("interactive squash folds a commit into the previous, pausing for message", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A");
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(*repo, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidA}, {RebaseAction::Squash, oidB} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE(out->pause == gittide::RebasePause::Message);

    auto st = repo->rebaseState();
    REQUIRE(st.pause == gittide::RebasePause::Message);
    // Combined prefill carries both A's and B's messages.
    REQUIRE(st.messagePrefill.find("A") != std::string::npos);
    REQUIRE(st.messagePrefill.find("B") != std::string::npos);

    auto cont = repo->continueRebase("A and B combined\n");
    REQUIRE(cont.has_value());
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    auto after = repo->log(10);
    // newest-first: [combined, c0] — two commits, not three.
    REQUIRE(after->size() == 2);
    REQUIRE(after->at(0).summary == "A and B combined");
    // both files present in the single squashed commit.
    REQUIRE(std::filesystem::exists(tmp.path() / "a.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "b.txt"));
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_git_repo_interactive_rebase.cpp
git commit -m "test(core): interactive squash folds into previous with combined message"
```

---

## Task 6: Fixup (no message pause) + guard cases

**Files:**
- Modify: (none — engine handles `Fixup` + guards from Task 2)
- Test: `tests/test_git_repo_interactive_rebase.cpp`

**Interfaces:**
- Consumes: `startInteractiveRebase`, `continueRebase`, `log`.

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("interactive fixup folds in without a message pause", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A keep");
    tmp.writeFile("b.txt", "b\n");
    tmp.commitAll("B discard msg");

    auto hist = repo->log(10);
    const std::string oidB = hist->at(0).oid;
    const std::string oidA = hist->at(1).oid;
    const std::string base = firstParentOf(*repo, oidA);

    gittide::RebaseTodo todo;
    todo.base = base;
    todo.entries = { {RebaseAction::Pick, oidA}, {RebaseAction::Fixup, oidB} };
    auto out = repo->startInteractiveRebase(todo);
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);
    REQUIRE(out->pause == gittide::RebasePause::None);     // fixup never pauses for a message
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    auto after = repo->log(10);
    REQUIRE(after->size() == 2);
    REQUIRE(after->at(0).summary == "A keep");            // B's message discarded
    REQUIRE(std::filesystem::exists(tmp.path() / "b.txt"));
}

TEST_CASE("interactive rebase rejects invalid plans", "[rebase-i]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    tmp.writeFile("a.txt", "a\n");
    tmp.commitAll("A");

    auto hist = repo->log(10);
    const std::string oidA = hist->at(0).oid;
    const std::string base = firstParentOf(*repo, oidA);

    // first entry squash → error
    {
        gittide::RebaseTodo bad;
        bad.base = base;
        bad.entries = { {RebaseAction::Squash, oidA} };
        REQUIRE_FALSE(repo->startInteractiveRebase(bad).has_value());
    }
    // all-drop → error
    {
        gittide::RebaseTodo bad;
        bad.base = base;
        bad.entries = { {RebaseAction::Drop, oidA} };
        REQUIRE_FALSE(repo->startInteractiveRebase(bad).has_value());
    }
    // base not an ancestor of HEAD → error
    {
        gittide::RebaseTodo bad;
        bad.base = std::string(40, '0'); // nonexistent/zero oid
        bad.entries = { {RebaseAction::Pick, oidA} };
        REQUIRE_FALSE(repo->startInteractiveRebase(bad).has_value());
    }
}
```

- [ ] **Step 2: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_core_tests`
Expected: PASS. Run the whole tag too: `ctest --test-dir build --output-on-failure -R gittide_core_tests`.

- [ ] **Step 3: Commit**

```bash
git add tests/test_git_repo_interactive_rebase.cpp
git commit -m "test(core): interactive fixup (no pause) + plan-validation guards"
```

---

## Task 7: AsyncRepo wrappers

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp`, `ui/src/asyncrepo.cpp`
- Test: covered via the controller test (Task 9); no standalone test.

**Interfaces:**
- Consumes: core verbs.
- Produces on `AsyncRepo`: `QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> startInteractiveRebase(gittide::RebaseTodo todo);` and the extended `continueRebase(QString message = QString())`.

- [ ] **Step 1: Declare in `asyncrepo.hpp`**

Next to `startRebase`:

```cpp
    /// Begin an interactive rebase (clean tree assumed; controller stashes).
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> startInteractiveRebase(gittide::RebaseTodo todo);
```

Change `continueRebase()` to:

```cpp
    /// Continue an in-progress rebase. `message` is non-empty only for an
    /// interactive Message pause (reword/squash); empty otherwise.
    QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> continueRebase(QString message = QString());
```

- [ ] **Step 2: Implement in `asyncrepo.cpp`**

Add next to `startRebase`, and replace the body of `continueRebase`:

```cpp
QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> AsyncRepo::startInteractiveRebase(gittide::RebaseTodo todo)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, todo = std::move(todo)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.startInteractiveRebase(todo);
        });
}

QCoro::Task<gittide::Expected<gittide::RebaseOutcome>> AsyncRepo::continueRebase(QString message)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, message]() -> gittide::Expected<gittide::RebaseOutcome>
        {
            std::scoped_lock lock(impl->mutex);
            std::optional<std::string> msg;
            if (!message.isEmpty())
                msg = message.toStdString();
            return impl->repo.continueRebase(std::move(msg));
        });
}
```

Ensure `#include <optional>` is present in `asyncrepo.cpp`.

- [ ] **Step 3: Build to verify it compiles**

Run: `cmake --build build --parallel`
Expected: success.

- [ ] **Step 4: Commit**

```bash
git add ui/include/gittide/ui/asyncrepo.hpp ui/src/asyncrepo.cpp
git commit -m "feat(ui): AsyncRepo wrappers for interactive rebase + message continue"
```

---

## Task 8: Controller — `buildRebaseTodo` + `rebaseTodoReady`

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp`, `ui/src/repocontroller.cpp`
- Test: `tests/ui/test_repocontroller_interactive_rebase.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `AsyncRepo::log` (or the existing history fetch), `head()`.
- Produces on `RepoController`: `QCoro::Task<void> buildRebaseTodo(QString fromOid);` and signal `void rebaseTodoReady(QString base, QVariantList entries);` where each entry is a `QVariantMap{ {"oid", QString}, {"summary", QString} }`, oldest-first.

> The editor needs the commits `fromOid..HEAD` (oldest first), each with its summary, plus the detach base = `fromOid`'s first parent. The controller walks the existing history (newest-first) from HEAD down to and including `fromOid`, reverses it, and resolves the base.

- [ ] **Step 1: Declare in `repocontroller.hpp`**

Add to the public tasks group:

```cpp
    /// Build the interactive-rebase todo for fromOid..HEAD (oldest first) and emit
    /// rebaseTodoReady with the detach base (fromOid's first parent).
    QCoro::Task<void> buildRebaseTodo(QString fromOid);
```

Add to signals:

```cpp
    /// Emitted with the seed plan for the interactive editor. `entries` is a list of
    /// QVariantMap{oid, summary}, oldest first; `base` is the detach commit oid.
    void rebaseTodoReady(QString base, QVariantList entries);
```

- [ ] **Step 2: Write the failing test**

Create `tests/ui/test_repocontroller_interactive_rebase.cpp` (mirror the harness of `test_repocontroller_rebase.cpp` — copy its includes, `QObject`/slots boilerplate, and TempRepo+AsyncRepo+RepoController setup verbatim, then this body):

```cpp
#pragma once
#include "gittide/ui/asyncrepo.hpp"
#include "gittide/ui/repocontroller.hpp"
#include "support/temprepo.hpp"
#include <QObject>
#include <QtTest>
#include <QSignalSpy>

class TestRepoControllerInteractiveRebase : public QObject
{
    Q_OBJECT
private slots:
    void build_todo_lists_range_oldest_first()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("base.txt", "base\n");
        tmp.commitAll("c0");
        tmp.writeFile("a.txt", "a\n");
        tmp.commitAll("A");
        tmp.writeFile("b.txt", "b\n");
        tmp.commitAll("B");

        std::string oidA;
        {
            auto r = gittide::GitRepo::open(tmp.path());
            QVERIFY(r.has_value());
            auto hist = r->log(10);
            QVERIFY(hist.has_value());
            oidA = hist->at(1).oid; // A
        }

        gittide::ui::AsyncRepo repo;
        QVERIFY(repo.open(tmp.path()));
        gittide::ui::RepoController ctrl(&repo);

        QSignalSpy ready(&ctrl, &gittide::ui::RepoController::rebaseTodoReady);
        bool done = false;
        [&]() -> QCoro::Task<void> {
            co_await ctrl.buildRebaseTodo(QString::fromStdString(oidA));
            done = true;
        }();
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

        QCOMPARE(ready.count(), 1);
        const QVariantList entries = ready.last().at(1).toList();
        QCOMPARE(entries.size(), 2);                       // A, B
        QCOMPARE(entries.at(0).toMap().value("summary").toString(), QString("A"));
        QCOMPARE(entries.at(1).toMap().value("summary").toString(), QString("B"));
    }
};
```

> Match the real `AsyncRepo`/`RepoController` construction from `test_repocontroller_rebase.cpp` (constructor args, namespaces). If `RepoController` takes the repo differently, copy that file's setup exactly.

- [ ] **Step 3: Register the ui test**

`tests/CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_interactive_rebase.cpp`.
`tests/ui/main.cpp`: add `#include "test_repocontroller_interactive_rebase.cpp"` and `RUN(TestRepoControllerInteractiveRebase);`.

- [ ] **Step 4: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL — `buildRebaseTodo` undefined.

- [ ] **Step 5: Implement `buildRebaseTodo` in `repocontroller.cpp`**

The controller already fetches history into a model; reuse the async `log`-style call it uses for `refreshHistory`. The simplest path is a dedicated AsyncRepo helper that returns the rows; but to avoid new core surface, walk via `m_repo->history(...)` if that exists, else add a tiny AsyncRepo `commitRange`. Implement using the existing history fetch the controller already calls (grep `refreshHistory` to see the exact accessor — it returns `std::vector<GraphRow>`-like rows with `.oid`/`.summary`). Assuming an `AsyncRepo::log(int)` returning `Expected<std::vector<gittide::GraphRow>>` exists (it backs `refreshHistory`):

```cpp
QCoro::Task<void> RepoController::buildRebaseTodo(QString fromOid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    auto hist = co_await m_repo->log(1000); // newest-first
    if (!self)
        co_return;
    if (!hist)
    {
        emit operationFailed(QString::fromStdString(hist.error().message));
        co_return;
    }

    const std::string from = fromOid.toStdString();
    QVariantList entries;          // built oldest-first
    QString base;
    bool found = false;
    // Collect HEAD..fromOid (newest-first), stop after fromOid.
    std::vector<std::pair<QString, QString>> picked; // (oid, summary), newest-first
    for (const auto& row : *hist)
    {
        picked.emplace_back(QString::fromStdString(row.oid),
                            QString::fromStdString(row.summary));
        if (row.oid == from)
        {
            found = true;
            // base = fromOid's first parent (row carries parents oldest? use core)
            break;
        }
    }
    if (!found)
    {
        emit operationFailed("commit is not on the current branch history");
        co_return;
    }
    // Resolve base = first parent of fromOid via head/parent lookup.
    auto baseOid = co_await m_repo->firstParent(fromOid);
    if (!self)
        co_return;
    if (!baseOid)
    {
        emit operationFailed(QString::fromStdString(baseOid.error().message));
        co_return;
    }
    base = QString::fromStdString(*baseOid);

    // Reverse to oldest-first.
    for (auto it = picked.rbegin(); it != picked.rend(); ++it)
    {
        QVariantMap m;
        m.insert("oid", it->first);
        m.insert("summary", it->second);
        entries.push_back(m);
    }
    emit rebaseTodoReady(base, entries);
}
```

This references a small core/AsyncRepo helper `firstParent(oid) -> Expected<std::string>`. Add it in core (`GitRepo::firstParent`) + AsyncRepo wrapper:

In `gitrepo.hpp` (public):

```cpp
    /// First-parent oid (40-char hex) of `oid`. Errors if `oid` is a root commit.
    Expected<std::string> firstParent(std::string oid) const;
```

In `gitrepo.cpp`:

```cpp
Expected<std::string> GitRepo::firstParent(std::string oid) const
{
    git_oid o;
    if (git_oid_fromstr(&o, oid.c_str()) < 0)
        return std::unexpected(GitError{-1, "bad oid"});
    git_commit* c = nullptr;
    if (int rc = git_commit_lookup(&c, m_repo, &o); rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> cg(c, git_commit_free);
    if (git_commit_parentcount(c) == 0)
        return std::unexpected(GitError{-1, "cannot edit history from a root commit"});
    const git_oid* p = git_commit_parent_id(c, 0);
    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), p);
    return std::string(buf);
}
```

In `asyncrepo.hpp`/`asyncrepo.cpp`:

```cpp
    QCoro::Task<gittide::Expected<std::string>> firstParent(QString oid);
```
```cpp
QCoro::Task<gittide::Expected<std::string>> AsyncRepo::firstParent(QString oid)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, s = oid.toStdString()]()
        { std::scoped_lock lock(impl->mutex); return impl->repo.firstParent(s); });
}
```

> Match `AsyncRepo::log`'s real name/return type used by `refreshHistory` (grep it). If history rows are not `GraphRow` with `.oid`/`.summary`, adapt the field access. The root-commit guard in `firstParent` is what makes "Edit history from here…" on the initial commit fail cleanly (spec §2.6).

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp ui/include/gittide/ui/asyncrepo.hpp ui/src/asyncrepo.cpp ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp tests/ui/test_repocontroller_interactive_rebase.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui,core): buildRebaseTodo + firstParent for the interactive editor seed"
```

---

## Task 9: Controller — `startInteractiveRebase` task + reuse refresh/auto-stash

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp`, `ui/src/repocontroller.cpp`
- Test: `tests/ui/test_repocontroller_interactive_rebase.cpp`

**Interfaces:**
- Consumes: `AsyncRepo::startInteractiveRebase`, the existing `continueRebase`/`skipRebase`/`abortRebase` controller tasks (which call the now-dispatching core verbs — no change needed), `refreshAfterRebase`, `m_pendingStashPop`, `popPendingStash`.
- Produces on `RepoController`: `QCoro::Task<void> startInteractiveRebase(QString base, QStringList actions, QStringList oids);`

> `continueRebase`/`skipRebase`/`abortRebase` controller tasks already exist (Tier 1) and call the core verbs that now dispatch to the interactive engine — they need **no change** beyond `continueRebase` forwarding a message (Task 11 adds the message arg). This task adds only the start.

- [ ] **Step 1: Declare in `repocontroller.hpp`**

```cpp
    /// Start an interactive rebase from a seed plan. Auto-stashes (D31), drives the
    /// first run; clean finish emits rebaseFinished + pops the stash; a pause leaves
    /// the repo mid-rebase. `actions[i]` is one of pick/reword/squash/fixup/drop.
    QCoro::Task<void> startInteractiveRebase(QString base, QStringList actions, QStringList oids);
```

- [ ] **Step 2: Write the failing test**

Append a slot to `tests/ui/test_repocontroller_interactive_rebase.cpp`:

```cpp
    void clean_interactive_reorder_finishes_and_idles()
    {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("base.txt", "base\n");
        tmp.commitAll("c0");
        tmp.writeFile("a.txt", "a\n");
        tmp.commitAll("A");
        tmp.writeFile("b.txt", "b\n");
        tmp.commitAll("B");

        std::string oidA, oidB, base;
        {
            auto r = gittide::GitRepo::open(tmp.path());
            auto hist = r->log(10);
            oidB = hist->at(0).oid;
            oidA = hist->at(1).oid;
            base = r->firstParent(oidA).value();
        }

        gittide::ui::AsyncRepo repo;
        QVERIFY(repo.open(tmp.path()));
        gittide::ui::RepoController ctrl(&repo);

        QSignalSpy finished(&ctrl, &gittide::ui::RepoController::rebaseFinished);
        bool done = false;
        [&]() -> QCoro::Task<void> {
            co_await ctrl.startInteractiveRebase(
                QString::fromStdString(base),
                QStringList{"pick", "pick"},
                QStringList{QString::fromStdString(oidB), QString::fromStdString(oidA)});
            done = true;
        }();
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(finished.count(), 1);
    }
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL — `startInteractiveRebase` undefined.

- [ ] **Step 4: Implement in `repocontroller.cpp`**

Mirror the Tier 1 `startRebase` body (auto-stash → start → branch on outcome → refresh):

```cpp
QCoro::Task<void> RepoController::startInteractiveRebase(QString base, QStringList actions, QStringList oids)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;

    gittide::RebaseTodo todo;
    todo.base = base.toStdString();
    for (int i = 0; i < actions.size() && i < oids.size(); ++i)
    {
        gittide::RebaseTodoEntry e;
        const QString a = actions.at(i);
        e.action = a == "reword" ? gittide::RebaseAction::Reword
                 : a == "squash" ? gittide::RebaseAction::Squash
                 : a == "fixup"  ? gittide::RebaseAction::Fixup
                 : a == "drop"   ? gittide::RebaseAction::Drop
                                 : gittide::RebaseAction::Pick;
        e.oid = oids.at(i).toStdString();
        todo.entries.push_back(e);
    }

    auto saved = co_await m_repo->stashSave("gittide: auto-stash before rebase");
    if (!self)
        co_return;
    if (!saved)
    {
        emit operationFailed(QString::fromStdString(saved.error().message));
        co_return;
    }
    m_pendingStashPop = *saved;

    auto out = co_await m_repo->startInteractiveRebase(todo);
    if (!self)
        co_return;
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await popPendingStash();
        if (!self)
            co_return;
        co_await refreshAfterRebase();
        co_return;
    }

    if (out->pause == gittide::RebasePause::None) // finished in one run
    {
        co_await popPendingStash();
        if (!self)
            co_return;
        auto head = co_await m_repo->head();
        if (self && head)
            emit rebaseFinished(QString::fromStdString(head->oid));
    }
    // else: paused (conflict or message) → banner drives; deferred pop waits.

    co_await refreshAfterRebase();
}
```

> `m_pendingStashPop` and `popPendingStash()` are shared with merge/Tier-1 rebase (safe — one operation at a time, D33). The Tier 1 `continueRebase`/`skipRebase` controller tasks already pop on a clean finish; because they branch on `out->conflicted`, also update them to treat `out->pause == None` as "finished" (Task 11 touches `continueRebase` for the message arg — fold this in there).

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp tests/ui/test_repocontroller_interactive_rebase.cpp
git commit -m "feat(ui): controller startInteractiveRebase task (auto-stash + refresh reuse)"
```

---

## Task 10: ViewModel — interactive properties, verbs, todo seed

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`, `ui/src/repoviewmodel.cpp`
- Test: covered via QML in Task 12; build-only here.

**Interfaces:**
- Consumes: controller `startInteractiveRebase`, `buildRebaseTodo`/`rebaseTodoReady`, the extended `continueRebase(message)`.
- Produces on `RepoViewModel`: properties `rebaseInteractive`, `rebasePauseReason`, `rebaseMessagePrefill` (all under `rebaseStateChanged`); `Q_INVOKABLE void startInteractiveRebase(QString base, QStringList actions, QStringList oids)`, `Q_INVOKABLE void requestRebaseTodo(QString fromOid)`, `Q_INVOKABLE void continueRebase(const QString& message = QString())` (replacing the no-arg overload); signal `rebaseTodoReady(QString base, QVariantList entries)`.

- [ ] **Step 1: Declare in `repoviewmodel.hpp`**

Add properties next to the existing rebase ones:

```cpp
    /// True while the in-progress rebase is interactive (manual engine).
    Q_PROPERTY(bool    rebaseInteractive    READ rebaseInteractive    NOTIFY rebaseStateChanged)
    /// Pause reason: "none" | "conflict" | "message".
    Q_PROPERTY(QString rebasePauseReason    READ rebasePauseReason    NOTIFY rebaseStateChanged)
    /// Prefilled text for a Message pause (reword/squash); empty otherwise.
    Q_PROPERTY(QString rebaseMessagePrefill READ rebaseMessagePrefill NOTIFY rebaseStateChanged)
```

Getters next to the existing ones:

```cpp
    bool    rebaseInteractive() const { return m_rebase.interactive; }
    QString rebasePauseReason() const
    {
        switch (m_rebase.pause)
        {
            case gittide::RebasePause::Conflict: return "conflict";
            case gittide::RebasePause::Message:  return "message";
            default:                             return "none";
        }
    }
    QString rebaseMessagePrefill() const { return QString::fromStdString(m_rebase.messagePrefill); }
```

Change the `continueRebase` invokable and add the new verbs/signal:

```cpp
    Q_INVOKABLE void startInteractiveRebase(QString base, QStringList actions, QStringList oids);
    /// Ask the controller for the editable plan fromOid..HEAD; reply on rebaseTodoReady.
    Q_INVOKABLE void requestRebaseTodo(QString fromOid);
    Q_INVOKABLE void continueRebase(const QString& message = QString());
```

In signals: `void rebaseTodoReady(QString base, QVariantList entries);`

- [ ] **Step 2: Implement in `repoviewmodel.cpp`**

Replace `continueRebase` and add the new verbs; wire `rebaseTodoReady` through from the controller (in the VM constructor, connect `m_controller`'s `rebaseTodoReady` to the VM's signal — mirror how `commitMessageReady` is forwarded):

```cpp
void RepoViewModel::startInteractiveRebase(QString base, QStringList actions, QStringList oids)
{
    QCoro::connect(m_controller->startInteractiveRebase(base, actions, oids), this, [] {});
}

void RepoViewModel::requestRebaseTodo(QString fromOid)
{
    QCoro::connect(m_controller->buildRebaseTodo(fromOid), this, [] {});
}

void RepoViewModel::continueRebase(const QString& message)
{
    QCoro::connect(m_controller->continueRebase(message), this, [] {});
}
```

In the constructor, next to the existing controller→VM forwards:

```cpp
    connect(m_controller, &RepoController::rebaseTodoReady,
            this, &RepoViewModel::rebaseTodoReady);
```

> The controller `continueRebase` must now accept a `QString message = QString()` and forward it to `m_repo->continueRebase(message)`. Update its signature in `repocontroller.hpp`/`.cpp` (the body already pops the stash on a non-conflicted outcome; also treat `out->pause == None` as finished). The QML `repo.continueRebase()` (banner conflict path) still calls it with no arg.

- [ ] **Step 3: Build to verify it compiles**

Run: `cmake --build build --parallel`
Expected: success. (UI behaviour exercised in Task 12.)

- [ ] **Step 4: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp
git commit -m "feat(ui): RepoViewModel interactive rebase properties, verbs, todo seed"
```

---

## Task 11: `RebaseTodoDialog.qml` + commit-menu entry + HistoryPane wiring

**Files:**
- Create: `ui/qml/RebaseTodoDialog.qml`
- Modify: `ui/qml/CommitContextMenu.qml`, `ui/qml/HistoryPane.qml`, `ui/qml/qml.qrc`, `ui/CMakeLists.txt`
- Test: `tests/ui/test_qml_rebase_todo.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `repoVm.requestRebaseTodo`, `repoVm.rebaseTodoReady`, `repoVm.startInteractiveRebase`.

- [ ] **Step 1: Write the failing QML test**

Create `tests/ui/test_qml_rebase_todo.cpp` (mirror an existing `test_qml_*` harness — copy its `QQmlApplicationEngine`/`createComponent` boilerplate). Core assertions: the dialog builds, populates rows from a model, and the *Start* button is disabled when the first row's action is "squash".

```cpp
#pragma once
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>

class TestQmlRebaseTodo : public QObject
{
    Q_OBJECT
private slots:
    void start_disabled_when_first_row_is_squash()
    {
        QQmlApplicationEngine engine;
        QQmlComponent comp(&engine, QUrl("qrc:/qml/RebaseTodoDialog.qml"));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QObject* dlg = comp.create();
        QVERIFY(dlg);
        // Seed two entries, set row 0 action = squash → invalid.
        QMetaObject::invokeMethod(dlg, "seed",
            Q_ARG(QString, "base0"),
            Q_ARG(QVariant, QVariantList{
                QVariantMap{{"oid","a"},{"summary","A"}},
                QVariantMap{{"oid","b"},{"summary","B"}} }));
        QMetaObject::invokeMethod(dlg, "setActionForTest", Q_ARG(int, 0), Q_ARG(QString, "squash"));
        QVariant valid = dlg->property("planValid");
        QVERIFY(!valid.toBool());
        delete dlg;
    }
};
```

> Match the exact QML-test boilerplate from an existing `tests/ui/test_qml_*.cpp` (engine setup, `qmlRegisterType` of `theme`, the `repoVm` context stub). The dialog must expose `function seed(base, entries)`, a test helper `function setActionForTest(i, a)`, and a `property bool planValid`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL — component missing.

- [ ] **Step 3: Create `ui/qml/RebaseTodoDialog.qml`**

```qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Interactive-rebase todo editor (spec rebase-interactive.md §3.2). Rows are oldest
// at top (git order). Reorder via up/down buttons; per-row action dropdown. Start is
// disabled while the plan is invalid (first row squash/fixup, or all-drop).
OverlayCard {
    id: root
    objectName: "rebaseTodoDialog"

    property string base: ""
    property ListModel model: ListModel {}
    readonly property var actions: ["pick", "reword", "squash", "fixup", "drop"]

    // Plan validity (mirrors the core guards).
    property bool planValid: {
        if (model.count === 0)
            return false
        var first = model.get(0).action
        if (first === "squash" || first === "fixup")
            return false
        for (var i = 0; i < model.count; ++i)
            if (model.get(i).action !== "drop")
                return true
        return false
    }

    function seed(b, entries) {
        base = b
        model.clear()
        for (var i = 0; i < entries.length; ++i)
            model.append({ oid: entries[i].oid, summary: entries[i].summary, action: "pick" })
    }
    function setActionForTest(i, a) { model.setProperty(i, "action", a) }

    function moveRow(from, to) {
        if (to < 0 || to >= model.count) return
        model.move(from, to, 1)
    }

    function collectActions() {
        var out = []
        for (var i = 0; i < model.count; ++i) out.push(model.get(i).action)
        return out
    }
    function collectOids() {
        var out = []
        for (var i = 0; i < model.count; ++i) out.push(model.get(i).oid)
        return out
    }

    Connections {
        target: repoVm
        function onRebaseTodoReady(b, entries) { root.seed(b, entries); root.open() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            text: "Edit history"
            color: theme.textPrimary
            font.pixelSize: 15
            font.bold: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.model
            spacing: 2
            delegate: RowLayout {
                width: ListView.view.width
                spacing: 8

                ComboBox {
                    objectName: "actionCombo"
                    model: root.actions
                    currentIndex: root.actions.indexOf(action)
                    onActivated: root.model.setProperty(index, "action", root.actions[currentIndex])
                    Layout.preferredWidth: 96
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: action === "drop" ? theme.textSecondary : theme.textPrimary
                    font.strikeout: action === "drop"
                    text: oid.substring(0, 7) + "  " + summary
                }
                Button { text: "↑"; onClicked: root.moveRow(index, index - 1) }
                Button { text: "↓"; onClicked: root.moveRow(index, index + 1) }
            }
        }

        Label {
            visible: !root.planValid
            color: theme.stateConflict
            font.pixelSize: 12
            text: "First entry can't be squash/fixup, and at least one commit must be kept."
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button { text: "Cancel"; onClicked: root.close() }
            Button {
                objectName: "rebaseStartButton"
                text: "Start rebase"
                enabled: root.planValid
                onClicked: {
                    repoVm.startInteractiveRebase(root.base, root.collectActions(), root.collectOids())
                    root.close()
                }
            }
        }
    }
}
```

> If `OverlayCard` is not the project's dialog base (check `RewordDialog.qml`'s root), use the same base and `open()`/`close()` convention that file uses. Match `theme` token names (`textSecondary`, `stateConflict`) to the actual theme object.

- [ ] **Step 4: Register the QML file**

In `ui/qml/qml.qrc`, add `<file>RebaseTodoDialog.qml</file>` next to `RewordDialog.qml`.
In `ui/CMakeLists.txt`, add `qml/RebaseTodoDialog.qml` to the QML files list.

- [ ] **Step 5: Add the commit-menu entry + HistoryPane wiring**

In `ui/qml/CommitContextMenu.qml`, add a signal and item (after the `reword()` item):

```qml
    signal editHistory()
```
```qml
    AppMenuItem {
        objectName: "editHistoryItem"
        text: "Edit history from here…"
        onTriggered: menu.editHistory()
    }
```

In `ui/qml/HistoryPane.qml`, instantiate the dialog and wire the menu signal (mirror how `onReword` opens `rewordDialog`):

```qml
    RebaseTodoDialog { id: rebaseTodoDialog }
```
```qml
    // inside the CommitContextMenu instantiation:
    onEditHistory: repoVm.requestRebaseTodo(commitMenu.oid)
```

(The dialog opens itself via its `Connections { onRebaseTodoReady }` handler.)

- [ ] **Step 6: Register the QML test**

`tests/CMakeLists.txt`: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_rebase_todo.cpp`.
`tests/ui/main.cpp`: `#include "test_qml_rebase_todo.cpp"` + `RUN(TestQmlRebaseTodo);`.

- [ ] **Step 7: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add ui/qml/RebaseTodoDialog.qml ui/qml/CommitContextMenu.qml ui/qml/HistoryPane.qml ui/qml/qml.qrc ui/CMakeLists.txt tests/ui/test_qml_rebase_todo.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): RebaseTodoDialog + Edit-history-from-here entry point"
```

---

## Task 12: Banner Message-pause variant + RewordDialog message wiring

**Files:**
- Modify: `ui/qml/RebaseBanner.qml`, `ui/qml/HistoryPane.qml`
- Test: `tests/ui/test_qml_rebase_banner_message.cpp` (new), `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `repoVm.rebasePauseReason`, `repoVm.rebaseMessagePrefill`, `repoVm.continueRebase(message)`.

- [ ] **Step 1: Write the failing QML test**

Create `tests/ui/test_qml_rebase_banner_message.cpp` — stub a `repoVm` with `rebaseInProgress=true`, `rebasePauseReason="message"`, and assert the banner's Continue button, when clicked, triggers the message editor (i.e. emits a request to open `RewordDialog`) rather than calling `continueRebase()` directly. Mirror the existing `test_qml_rebase_banner.cpp` harness (copy its stub `repoVm` QObject with `Q_PROPERTY`s + `Q_INVOKABLE` spies).

```cpp
// Assert: with rebasePauseReason == "message", the banner headline mentions
// "editing message" and the Continue button opens the editor (a signal/flag the
// test stub records), not a direct continueRebase().
```

> Copy `test_qml_rebase_banner.cpp` verbatim as the starting point; add the `rebasePauseReason`/`rebaseMessagePrefill` properties to its stub and the new assertions. Keep the existing conflict-pause assertions passing.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: FAIL.

- [ ] **Step 3: Extend `RebaseBanner.qml`**

Make the headline and the Continue action pause-aware. Replace the headline `text:` to add a message variant, and route Continue:

```qml
        // headline
        text: repo
              ? (repo.rebasePauseReason === "message"
                 ? ("Rebasing — step " + repo.rebaseStep + "/" + repo.rebaseTotal
                    + " — editing message"
                    + (repo.rebaseStepSummary.length ? (" (" + repo.rebaseStepSummary + ")") : ""))
                 : ("Rebasing onto " + (repo.rebaseOnto.length ? repo.rebaseOnto : "target")
                    + " — step " + repo.rebaseStep + "/" + repo.rebaseTotal
                    + (repo.rebaseStepSummary.length ? (" — " + repo.rebaseStepSummary) : "")
                    + (repo.rebaseConflictedCount > 0
                       ? (" — " + repo.rebaseConflictedCount + " conflicted file"
                          + (repo.rebaseConflictedCount === 1 ? "" : "s"))
                       : "")))
              : ""
```

Add a signal `signal requestMessageEdit()` to the banner root, and change the Continue button:

```qml
        Button
        {
            objectName: "rebaseContinueButton"
            text: "Continue"
            // Conflict pause: enabled only when nothing is unresolved.
            // Message pause: always enabled (opens the editor).
            enabled: repo && (repo.rebasePauseReason === "message"
                              || repo.rebaseConflictedCount === 0)
            onClicked: {
                if (repo.rebasePauseReason === "message")
                    root.requestMessageEdit()
                else
                    repo.continueRebase()
            }
        }
```

(Skip and Abort buttons stay; Abort is present in both pause states — the always-reachable guarantee.)

- [ ] **Step 4: Wire the editor in `HistoryPane.qml`**

Reuse `RewordDialog` for the message pause. Where the banner is hosted, connect its new signal to open `RewordDialog` prefilled from `repoVm.rebaseMessagePrefill`, and on Save call `repoVm.continueRebase(message)`:

```qml
    RebaseBanner {
        repo: repoVm
        onRequestMessageEdit: {
            rebaseMessageDialog.prefill = repoVm.rebaseMessagePrefill
            rebaseMessageDialog.open()
        }
    }

    RewordDialog {
        id: rebaseMessageDialog
        onSaved: function(message) { repoVm.continueRebase(message) }
    }
```

> Match `RewordDialog`'s real API (grep it): the property that pre-fills (`prefill`/`message`) and the save signal name/shape. If it emits `accepted(string)` instead of `saved(message)`, use that. RewordDialog is the existing reword-tip dialog; reuse it unchanged.

- [ ] **Step 5: Register the QML test**

`tests/CMakeLists.txt` + `tests/ui/main.cpp`: add the new test (`#include` + `RUN(TestQmlRebaseBannerMessage);`).

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure -R gittide_ui_tests`
Expected: PASS. Run the full ui suite too.

- [ ] **Step 7: Commit**

```bash
git add ui/qml/RebaseBanner.qml ui/qml/HistoryPane.qml tests/ui/test_qml_rebase_banner_message.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): rebase banner message-pause variant + RewordDialog wiring"
```

---

## Task 13: Docs — graduate wishes, D34, spec links, context-menus table

**Files:**
- Modify: `docs/decisions.md`, `docs/wishlist/rebase.md`, `docs/wishlist/history-editing.md`, `docs/spec/product/rebase.md`, `docs/spec/product/history-editing.md`, `docs/spec/product/context-menus.md`, `docs/plans/index.md`
- Test: none (docs).

- [ ] **Step 1: Add D34 to `docs/decisions.md`**

In the **Engineering** section (after D33), add:

```markdown
- **D34 — Interactive rebase is a manual cherry-pick engine over a GitTide-private
  todo, with a mid-rebase message pause.** libgit2's `git_rebase_init` only
  generates a `PICK`-only operation list in original order and exposes no API to
  inject a reordered / squashed / dropped / reworded todo, so the interactive
  engine is built by hand: detach HEAD at the base, `git_cherrypick` each kept
  commit on the detached HEAD (the branch ref is moved only at finish, making abort
  trivial), `git_commit_amend` for squash/fixup, skip for drop. State lives in
  `<gitdir>/gittide-rebase/` (todo + `done` cursor + `applied` marker + orig-head +
  branch), so `RebaseState` stays disk-truth (D30) and a paused rebase survives a
  restart. Reword/squash pause mid-rebase for a message (git-CLI style) rather than
  collecting messages up-front. The Tier 1 `continueRebase`/`skipRebase`/`abortRebase`
  verbs dispatch to whichever engine is live (libgit2 plain vs. our dir). *Rejected:*
  re-`init`'ing libgit2 per step (no todo API); shelling out to `git rebase -i`
  (violates the no-git-command-strings invariant, loses structured conflict state);
  up-front message collection (the user chose git-faithful mid-rebase pausing). →
  [`engineering`](spec/engineering/engineering.md), [`product`](spec/product/rebase-interactive.md)
```

- [ ] **Step 2: Graduate the wishes**

`docs/wishlist/rebase.md` — change the Status note: the interactive editor moves from `idea` to designed/shipped, link `spec/product/rebase-interactive.md` and this plan.
`docs/wishlist/history-editing.md` — its deferred trio (reword-older, squash, reorder) now ships via the interactive engine; update the Status banner to point at `rebase-interactive.md`.

- [ ] **Step 3: Cross-link the specs**

`docs/spec/product/rebase.md` — add a line under Overview: Tier 2 (interactive) graduated → see `rebase-interactive.md`.
`docs/spec/product/history-editing.md` — update §7 ("Deferred — the interactive-rebase engine") to "Shipped in `rebase-interactive.md`".
`docs/spec/product/context-menus.md` — add the *Edit history from here…* row to the `CommitContextMenu` table.

- [ ] **Step 4: Update the plan index**

`docs/plans/index.md` — add the Plan 20 row.

- [ ] **Step 5: Flip this plan's Status + the spec's**

Set this plan's `Status` to `shipped (2026-06-24)` and `docs/spec/product/rebase-interactive.md`'s `Status` from `spec` to `shipped`.

- [ ] **Step 6: Commit**

```bash
git add docs/
git commit -m "docs: graduate interactive rebase (Tier 2) — D34, wishlist, specs, plan index"
```

---

## Self-Review notes (for the implementer)

- **Spec coverage:** §1 (D34 rationale) → Task 13. §2.1 plan types → Task 1. §2.2 state → Task 1+3. §2.3 state dir → Task 2. §2.4 verbs → Tasks 2/3/7. §2.5 engine semantics (pick/reword/squash/fixup/drop, conflict+message interleave) → Tasks 2–6. §2.6 guards → Tasks 2/6/8(root). §3.1 entry → Task 11. §3.2 dialog → Task 11. §3.3 VM → Task 10. §3.4 banner → Task 12. §3.5 message reuse → Task 12. §3.6 controller → Tasks 8/9. §4 reuse → Tasks 3/12. §5 safety (abort/mutual-excl/disk-truth) → Tasks 2/3. §6 files / §7 → all.
- **Naming consistency:** core `continueRebase(std::optional<std::string>)` ↔ AsyncRepo `continueRebase(QString)` ↔ VM `continueRebase(const QString&)` ↔ QML `repo.continueRebase()` / `repo.continueRebase(msg)`. `RebasePause::{None,Conflict,Message}` ↔ VM string `"none"/"conflict"/"message"`. Engine helpers all defined in Task 2's Interfaces block.
- **Grep-before-you-code reminders** are inlined as `>` notes where a neighbouring API's exact name must be confirmed (`GraphRow.summary`, `StageSelection`, `AsyncRepo::log`, `RewordDialog` save signal, `OverlayCard` base, `RepoController` ctor).
