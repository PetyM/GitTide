# Plan 3a — Core Git Ops (diff / stage / unstage / discard / commit) Implementation Plan

| | |
|--|--|
| **Date** | 2026-06-17 |
| **Status** | `done` |
| **Spec** | [engineering](../spec/engineering/engineering.md) · [product](../spec/product/product.md) |
| **Depends on** | [Core foundation](2026-06-16-core-foundation.md) |

**Goal:** Add diff, partial staging (file / hunk / line), unstage, discard, and commit to the Qt-free Core `GitRepo`, plus a `DiffEngine` that parses libgit2 diffs — all unit-tested with Catch2.

**Architecture:** New plain-data diff/selection types in `gitgui/Diff.hpp`. A pure `DiffEngine::parse(git_diff*)` turns a libgit2 diff into a `DiffResult`. `GitRepo` gains `diff/stage/unstage/discard/commit`. Whole-file staging uses libgit2 index calls; hunk/line staging is done by synthesizing a minimal unified-diff buffer (`build_patch`, a pure testable function) and applying it with `git_apply` to the index (stage/unstage) or worktree (discard). No Qt anywhere in this plan.

**Tech Stack:** C++23, libgit2 v1.8.1, Catch2 v3, CMake. `std::expected<T, GitError>` for all fallible ops.

---

## File Structure

- **Create** `core/include/gitgui/Diff.hpp` — `DiffTarget`, `DiffLineOrigin`, `DiffLine`, `DiffHunk`, `DiffResult`, `StageSelection`, `CommitRequest`. Plain data, no libgit2/Qt.
- **Create** `core/include/gitgui/DiffEngine.hpp` / `core/src/DiffEngine.cpp` — `DiffEngine::parse(git_diff*) -> Expected<DiffResult>` and the pure `build_patch(...)` helper used for partial staging.
- **Modify** `core/include/gitgui/GitRepo.hpp` / `core/src/GitRepo.cpp` — add `diff/stage/unstage/discard/commit`.
- **Modify** `tests/support/TempRepo.hpp` / `.cpp` — add `set_identity()` so commit tests have a config author.
- **Create** tests: `tests/test_diff_engine.cpp`, `tests/test_build_patch.cpp`, `tests/test_git_repo_diff.cpp`, `tests/test_git_repo_stage.cpp`, `tests/test_git_repo_discard.cpp`, `tests/test_git_repo_commit.cpp`.
- **Modify** `core/CMakeLists.txt` (add `DiffEngine.cpp`) and `tests/CMakeLists.txt` (add the new test sources).

`DiffEngine.cpp` is the only new translation unit; `build_patch` lives there beside the parser because both share the hunk model and nothing else needs them.

---

## Task 1: Diff & selection data types

**Files:**
- Create: `core/include/gitgui/Diff.hpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gitgui {

// Which pair of trees a diff compares.
enum class DiffTarget {
    WorktreeVsIndex,  // unstaged changes
    IndexVsHead,      // staged changes
};

enum class DiffLineOrigin { Context, Added, Removed };

struct DiffLine {
    DiffLineOrigin origin = DiffLineOrigin::Context;
    int oldLineno = -1;   // -1 when the line does not exist on that side
    int newLineno = -1;
    std::string text;     // line content WITHOUT trailing newline
};

struct DiffHunk {
    int oldStart = 0, oldLines = 0;
    int newStart = 0, newLines = 0;
    std::vector<DiffLine> lines;
};

struct DiffResult {
    std::vector<DiffHunk> hunks;
};

// A selection within ONE file.
//   hunkIndex == nullopt              -> whole file
//   hunkIndex set, lineIndices empty  -> the whole hunk
//   hunkIndex set, lineIndices filled -> those line indices within the hunk
// lineIndices are indices into DiffHunk::lines.
struct StageSelection {
    std::filesystem::path path;          // repo-relative
    std::optional<int> hunkIndex;
    std::vector<int> lineIndices;
};

struct CommitRequest {
    std::string message;
    // author / committer come from git config (git_signature_default).
};

}  // namespace gitgui
```

- [ ] **Step 2: Verify it compiles**

Run: `cmake --build build --target gitgui_core 2>&1 | tail -5`
Expected: builds (header is not yet included anywhere, so this just confirms no syntax error once used; if no build dir yet, run `cmake -S . -B build -DGITGUI_BUILD_UI=OFF` first).

- [ ] **Step 3: Commit**

```bash
git add core/include/gitgui/Diff.hpp
git commit -m "feat(core): add diff/selection plain-data types"
```

---

## Task 2: DiffEngine::parse

**Files:**
- Create: `core/include/gitgui/DiffEngine.hpp`
- Create: `core/src/DiffEngine.cpp`
- Modify: `core/CMakeLists.txt`
- Test: `tests/test_diff_engine.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include "gitgui/Diff.hpp"
#include "gitgui/GitError.hpp"

struct git_diff;

namespace gitgui {

class DiffEngine {
public:
    // Parse a SINGLE-file libgit2 diff (delta 0) into hunks and lines.
    // Returns an empty DiffResult (no hunks) if the diff has no deltas.
    static Expected<DiffResult> parse(git_diff* diff);
};

}  // namespace gitgui
```

- [ ] **Step 2: Add DiffEngine.cpp to the core target**

In `core/CMakeLists.txt`, add to the `target_sources(gitgui_core PRIVATE ...)` list, after `GitRepo.cpp`:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DiffEngine.cpp
```

- [ ] **Step 3: Write the failing test**

Create `tests/test_diff_engine.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/DiffEngine.hpp"
#include "gitgui/PathUtil.hpp"
#include "support/TempRepo.hpp"
#include <git2.h>
#include <memory>

using gitgui::DiffEngine;
using gitgui::DiffLineOrigin;

// Helper: worktree-vs-index diff for a single file, parsed via DiffEngine.
static gitgui::DiffResult diff_file(const std::filesystem::path& repo_path,
                                    const char* file) {
    git_repository* repo = nullptr;
    REQUIRE(git_repository_open(&repo, gitgui::to_git_path(repo_path).c_str()) == 0);

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    char* paths[] = {const_cast<char*>(file)};
    opts.pathspec.strings = paths;
    opts.pathspec.count = 1;

    git_diff* diff = nullptr;
    REQUIRE(git_diff_index_to_workdir(&diff, repo, nullptr, &opts) == 0);
    auto result = DiffEngine::parse(diff);
    git_diff_free(diff);
    git_repository_free(repo);
    REQUIRE(result.has_value());
    return *result;
}

TEST_CASE("DiffEngine parses a single modified hunk", "[diff]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "line1\nline2\nline3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "line1\nCHANGED\nline3\n");

    auto d = diff_file(tmp.path(), "a.txt");
    REQUIRE(d.hunks.size() == 1);
    const auto& h = d.hunks[0];

    // Expect: context line1, -line2, +CHANGED, context line3.
    int added = 0, removed = 0, context = 0;
    for (const auto& ln : h.lines) {
        if (ln.origin == DiffLineOrigin::Added) ++added;
        else if (ln.origin == DiffLineOrigin::Removed) ++removed;
        else ++context;
    }
    REQUIRE(added == 1);
    REQUIRE(removed == 1);
    REQUIRE(context == 2);
}
```

- [ ] **Step 4: Register the test**

In `tests/CMakeLists.txt`, add to the `gitgui_core_tests` source list (after `test_git_repo.cpp`):

```cmake
  test_diff_engine.cpp
```

- [ ] **Step 5: Run the test to verify it fails**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -20`
Expected: link/compile FAIL — `DiffEngine::parse` undefined.

- [ ] **Step 6: Implement DiffEngine::parse**

Create `core/src/DiffEngine.cpp`:

```cpp
#include "gitgui/DiffEngine.hpp"
#include <git2.h>
#include <memory>

namespace gitgui {

Expected<DiffResult> DiffEngine::parse(git_diff* diff) {
    DiffResult out;
    if (git_diff_num_deltas(diff) == 0) return out;

    git_patch* raw = nullptr;
    int rc = git_patch_from_diff(&raw, diff, 0);  // single-file diff: delta 0
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_patch, decltype(&git_patch_free)> patch(raw, git_patch_free);

    size_t nhunks = git_patch_num_hunks(patch.get());
    out.hunks.reserve(nhunks);
    for (size_t hi = 0; hi < nhunks; ++hi) {
        const git_diff_hunk* gh = nullptr;
        size_t nlines = 0;
        rc = git_patch_get_hunk(&gh, &nlines, patch.get(), hi);
        if (rc < 0) return std::unexpected(last_git_error(rc));

        DiffHunk hunk;
        hunk.oldStart = gh->old_start; hunk.oldLines = gh->old_lines;
        hunk.newStart = gh->new_start; hunk.newLines = gh->new_lines;
        hunk.lines.reserve(nlines);

        for (size_t li = 0; li < nlines; ++li) {
            const git_diff_line* gl = nullptr;
            rc = git_patch_get_line_in_hunk(&gl, patch.get(), hi, li);
            if (rc < 0) return std::unexpected(last_git_error(rc));

            DiffLine line;
            switch (gl->origin) {
                case GIT_DIFF_LINE_ADDITION: line.origin = DiffLineOrigin::Added; break;
                case GIT_DIFF_LINE_DELETION: line.origin = DiffLineOrigin::Removed; break;
                default:                     line.origin = DiffLineOrigin::Context; break;
            }
            line.oldLineno = gl->old_lineno;
            line.newLineno = gl->new_lineno;
            // content is NOT null-terminated; strip a single trailing '\n'.
            size_t len = gl->content_len;
            if (len > 0 && gl->content[len - 1] == '\n') --len;
            line.text.assign(gl->content, len);
            hunk.lines.push_back(std::move(line));
        }
        out.hunks.push_back(std::move(hunk));
    }
    return out;
}

}  // namespace gitgui
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[diff\]' --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add core/include/gitgui/DiffEngine.hpp core/src/DiffEngine.cpp core/CMakeLists.txt tests/test_diff_engine.cpp tests/CMakeLists.txt
git commit -m "feat(core): add DiffEngine::parse over libgit2 diff"
```

---

## Task 3: GitRepo::diff

**Files:**
- Modify: `core/include/gitgui/GitRepo.hpp`
- Modify: `core/src/GitRepo.cpp`
- Test: `tests/test_git_repo_diff.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Declare diff() in the header**

In `core/include/gitgui/GitRepo.hpp`, add `#include "gitgui/Diff.hpp"` near the top includes, and inside the `public:` section after `status()`:

```cpp
    // Diff a single file against the chosen target.
    Expected<DiffResult> diff(DiffTarget target,
                              const std::filesystem::path& file) const;
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_git_repo_diff.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"

using gitgui::DiffTarget;
using gitgui::DiffLineOrigin;

TEST_CASE("GitRepo::diff WorktreeVsIndex shows unstaged edit", "[diff]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "x\ny\nz\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "x\nY2\nz\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    bool has_added = false, has_removed = false;
    for (const auto& ln : d->hunks[0].lines) {
        has_added   |= ln.origin == DiffLineOrigin::Added;
        has_removed |= ln.origin == DiffLineOrigin::Removed;
    }
    REQUIRE(has_added);
    REQUIRE(has_removed);
}

TEST_CASE("GitRepo::diff IndexVsHead is empty with nothing staged", "[diff]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "x\n");
    tmp.commit_all("init");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.empty());
}
```

- [ ] **Step 3: Register the test**

In `tests/CMakeLists.txt`, add `test_git_repo_diff.cpp` to the `gitgui_core_tests` list.

- [ ] **Step 4: Run to verify it fails**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -20`
Expected: FAIL — `GitRepo::diff` undefined.

- [ ] **Step 5: Implement diff()**

In `core/src/GitRepo.cpp`, add `#include "gitgui/DiffEngine.hpp"` to the includes, and this method inside `namespace gitgui`:

```cpp
Expected<DiffResult> GitRepo::diff(DiffTarget target,
                                   const std::filesystem::path& file) const {
    std::string git_file = to_git_path(file);
    char* paths[] = {git_file.data()};

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count = 1;
    opts.flags = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT;

    git_diff* raw = nullptr;
    int rc;
    if (target == DiffTarget::WorktreeVsIndex) {
        rc = git_diff_index_to_workdir(&raw, repo_, nullptr, &opts);
    } else {
        // IndexVsHead: compare HEAD's tree to the index. Unborn HEAD -> null tree.
        git_object* head_obj = nullptr;
        git_tree* head_tree = nullptr;
        if (git_revparse_single(&head_obj, repo_, "HEAD^{tree}") == 0) {
            head_tree = reinterpret_cast<git_tree*>(head_obj);
        }
        rc = git_diff_tree_to_index(&raw, repo_, head_tree, nullptr, &opts);
        if (head_tree) git_tree_free(head_tree);
    }
    if (rc < 0) return std::unexpected(last_git_error(rc));

    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff(raw, git_diff_free);
    return DiffEngine::parse(diff.get());
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[diff\]' --output-on-failure`
Expected: PASS (all `[diff]` cases).

- [ ] **Step 7: Commit**

```bash
git add core/include/gitgui/GitRepo.hpp core/src/GitRepo.cpp tests/test_git_repo_diff.cpp tests/CMakeLists.txt
git commit -m "feat(core): add GitRepo::diff (worktree/index/HEAD)"
```

---

## Task 4: TempRepo::set_identity + GitRepo::commit

**Files:**
- Modify: `tests/support/TempRepo.hpp`, `tests/support/TempRepo.cpp`
- Modify: `core/include/gitgui/GitRepo.hpp`, `core/src/GitRepo.cpp`
- Test: `tests/test_git_repo_commit.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add set_identity to TempRepo header**

In `tests/support/TempRepo.hpp`, add inside the public section after `commit_all`:

```cpp
    // Write user.name / user.email into the repo's git config.
    void set_identity(std::string_view name, std::string_view email);
```

- [ ] **Step 2: Implement set_identity**

In `tests/support/TempRepo.cpp`, add inside `namespace gitgui::test`:

```cpp
void TempRepo::set_identity(std::string_view name, std::string_view email) {
    git_config* cfg = nullptr;
    check(git_repository_config(&cfg, repo_), "git_repository_config failed");
    check(git_config_set_string(cfg, "user.name", std::string(name).c_str()),
          "set user.name failed");
    check(git_config_set_string(cfg, "user.email", std::string(email).c_str()),
          "set user.email failed");
    git_config_free(cfg);
}
```

- [ ] **Step 3: Declare commit() in GitRepo header**

In `core/include/gitgui/GitRepo.hpp`, inside `public:`:

```cpp
    // Commit the current index. Author/committer come from git config
    // (user.name/user.email). Returns the new commit's hex oid.
    Expected<std::string> commit(const CommitRequest& req);
```

- [ ] **Step 4: Write the failing test**

Create `tests/test_git_repo_commit.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"
#include <git2.h>

TEST_CASE("GitRepo::commit creates a commit from the staged index", "[commit]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Ada", "ada@example.com");
    tmp.write_file("a.txt", "hello\n");
    tmp.commit_all("init");                 // first commit (HEAD now exists)
    tmp.write_file("a.txt", "hello\nworld\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto oid = repo->commit(gitgui::CommitRequest{"second"});
    REQUIRE(oid.has_value());
    REQUIRE(oid->size() == 40);             // full sha-1 hex

    // The new commit is HEAD, has the right message and author.
    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gitgui::to_git_path(tmp.path()).c_str()) == 0);
    git_oid head; REQUIRE(git_reference_name_to_id(&head, r, "HEAD") == 0);
    git_commit* c = nullptr; REQUIRE(git_commit_lookup(&c, r, &head) == 0);
    REQUIRE(std::string(git_commit_message(c)) == "second");
    REQUIRE(std::string(git_commit_author(c)->name) == "Ada");
    REQUIRE(git_commit_parentcount(c) == 1);
    git_commit_free(c);
    git_repository_free(r);
}
```

This test depends on `stage` (Task 5). If executing in strict order, implement Task 5 Step 5 (whole-file stage) before running this test green. The commit implementation itself does not depend on stage.

- [ ] **Step 5: Register the test**

In `tests/CMakeLists.txt`, add `test_git_repo_commit.cpp` (needs `gitgui/PathUtil.hpp` already transitively; the test includes `<git2.h>` and uses `to_git_path`, so add `#include "gitgui/PathUtil.hpp"` at the top of the test — add that include now).

- [ ] **Step 6: Run to verify it fails**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -20`
Expected: FAIL — `GitRepo::commit` undefined.

- [ ] **Step 7: Implement commit()**

In `core/src/GitRepo.cpp`, add inside `namespace gitgui`:

```cpp
Expected<std::string> GitRepo::commit(const CommitRequest& req) {
    git_signature* sig = nullptr;
    int rc = git_signature_default(&sig, repo_);  // reads user.name/user.email
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_signature, decltype(&git_signature_free)>
        sig_guard(sig, git_signature_free);

    git_index* index = nullptr;
    rc = git_repository_index(&index, repo_);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)>
        idx_guard(index, git_index_free);

    git_oid tree_oid;
    rc = git_index_write_tree(&tree_oid, index);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    git_tree* tree = nullptr;
    rc = git_tree_lookup(&tree, repo_, &tree_oid);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_tree, decltype(&git_tree_free)>
        tree_guard(tree, git_tree_free);

    // Parent = current HEAD commit, if the branch is born.
    git_commit* parent = nullptr;
    git_oid parent_oid;
    const git_commit* parents[1] = {nullptr};
    size_t parent_count = 0;
    if (git_reference_name_to_id(&parent_oid, repo_, "HEAD") == 0 &&
        git_commit_lookup(&parent, repo_, &parent_oid) == 0) {
        parents[0] = parent;
        parent_count = 1;
    }

    git_oid commit_oid;
    rc = git_commit_create(&commit_oid, repo_, "HEAD", sig, sig,
                           nullptr, req.message.c_str(), tree,
                           parent_count, parents);
    if (parent) git_commit_free(parent);
    if (rc < 0) return std::unexpected(last_git_error(rc));

    char buf[GIT_OID_HEXSZ + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), &commit_oid);
    return std::string(buf);
}
```

- [ ] **Step 8: Run to verify it passes** (after Task 5 whole-file stage exists)

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[commit\]' --output-on-failure`
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add tests/support/TempRepo.hpp tests/support/TempRepo.cpp core/include/gitgui/GitRepo.hpp core/src/GitRepo.cpp tests/test_git_repo_commit.cpp tests/CMakeLists.txt
git commit -m "feat(core): add GitRepo::commit + TempRepo::set_identity"
```

---

## Task 5: GitRepo::stage / unstage — whole file

**Files:**
- Modify: `core/include/gitgui/GitRepo.hpp`, `core/src/GitRepo.cpp`
- Test: `tests/test_git_repo_stage.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Declare stage/unstage in the header**

In `core/include/gitgui/GitRepo.hpp`, inside `public:`:

```cpp
    // Stage / unstage the selection (whole file, hunk, or specific lines).
    Expected<void> stage(const StageSelection& sel);
    Expected<void> unstage(const StageSelection& sel);
```

- [ ] **Step 2: Write the failing test (whole-file)**

Create `tests/test_git_repo_stage.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"
#include <algorithm>

using gitgui::StatusFlag;
using gitgui::has_flag;

static StatusFlag flags_for(const gitgui::GitRepo& repo, const char* file) {
    auto st = repo.status();
    REQUIRE(st.has_value());
    auto it = std::find_if(st->begin(), st->end(), [&](const gitgui::FileStatus& f) {
        return f.path == std::filesystem::path(file);
    });
    return it == st->end() ? StatusFlag::None : it->flags;
}

TEST_CASE("stage whole file moves WtModified to IndexModified", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "1\n2\n3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "1\nTWO\n3\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::WtModified));

    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::IndexModified));
}

TEST_CASE("unstage whole file moves IndexModified back to WtModified", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("a.txt", "1\n2\n3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "1\nTWO\n3\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::IndexModified));

    REQUIRE(repo->unstage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "a.txt"), StatusFlag::WtModified));
}

TEST_CASE("stage whole file handles deletion", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.write_file("gone.txt", "bye\n");
    tmp.commit_all("init");
    std::filesystem::remove(tmp.path() / "gone.txt");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(has_flag(flags_for(*repo, "gone.txt"), StatusFlag::WtDeleted));

    REQUIRE(repo->stage(gitgui::StageSelection{"gone.txt", std::nullopt, {}}).has_value());
    REQUIRE(has_flag(flags_for(*repo, "gone.txt"), StatusFlag::IndexDeleted));
}
```

- [ ] **Step 3: Register the test**

In `tests/CMakeLists.txt`, add `test_git_repo_stage.cpp`.

- [ ] **Step 4: Run to verify it fails**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -20`
Expected: FAIL — `GitRepo::stage`/`unstage` undefined.

- [ ] **Step 5: Implement whole-file stage/unstage**

In `core/src/GitRepo.cpp`, add a file-scoped helper and the two methods. The partial (hunk/line) path is filled in Task 7; for now route only whole-file and return a clear error for partial selections so the code compiles and whole-file tests pass.

```cpp
namespace {
// True when the selection targets the whole file (no hunk chosen).
bool is_whole_file(const StageSelection& sel) { return !sel.hunkIndex.has_value(); }
}  // namespace

Expected<void> GitRepo::stage(const StageSelection& sel) {
    git_index* index = nullptr;
    int rc = git_repository_index(&index, repo_);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_index, decltype(&git_index_free)>
        idx_guard(index, git_index_free);

    if (is_whole_file(sel)) {
        std::string p = to_git_path(sel.path);
        // If the file is gone from the worktree, stage its deletion.
        if (!std::filesystem::exists(sel.path.is_absolute()
                                         ? sel.path
                                         : workdir() / sel.path)) {
            rc = git_index_remove_bypath(index, p.c_str());
        } else {
            rc = git_index_add_bypath(index, p.c_str());
        }
        if (rc < 0) return std::unexpected(last_git_error(rc));
        rc = git_index_write(index);
        if (rc < 0) return std::unexpected(last_git_error(rc));
        return {};
    }
    return apply_partial(sel, /*stage=*/true);  // Task 7
}

Expected<void> GitRepo::unstage(const StageSelection& sel) {
    if (is_whole_file(sel)) {
        // Reset the index entry for this path back to HEAD.
        git_object* head = nullptr;
        int rc = git_revparse_single(&head, repo_, "HEAD");
        if (rc < 0) {
            // Unborn branch: there is no HEAD, so unstaging == removing from index.
            git_index* index = nullptr;
            rc = git_repository_index(&index, repo_);
            if (rc < 0) return std::unexpected(last_git_error(rc));
            std::unique_ptr<git_index, decltype(&git_index_free)>
                idx_guard(index, git_index_free);
            std::string p = to_git_path(sel.path);
            rc = git_index_remove_bypath(index, p.c_str());
            if (rc < 0) return std::unexpected(last_git_error(rc));
            rc = git_index_write(index);
            if (rc < 0) return std::unexpected(last_git_error(rc));
            return {};
        }
        std::unique_ptr<git_object, decltype(&git_object_free)>
            head_guard(head, git_object_free);

        std::string p = to_git_path(sel.path);
        char* paths[] = {p.data()};
        git_strarray pathspec = {paths, 1};
        rc = git_reset_default(repo_, head, &pathspec);
        if (rc < 0) return std::unexpected(last_git_error(rc));
        return {};
    }
    return apply_partial(sel, /*stage=*/false);  // Task 7
}
```

Add a private `workdir()` helper and forward declarations. In `GitRepo.hpp` private section:

```cpp
    std::filesystem::path workdir() const;            // repo working directory
    Expected<void> apply_partial(const StageSelection& sel, bool stage);  // Task 7
```

In `GitRepo.cpp`, implement `workdir()`:

```cpp
std::filesystem::path GitRepo::workdir() const {
    const char* wd = git_repository_workdir(repo_);
    return wd ? from_git_path(wd) : std::filesystem::path{};
}
```

And a **temporary** `apply_partial` stub so the build links until Task 7 replaces it:

```cpp
Expected<void> GitRepo::apply_partial(const StageSelection&, bool) {
    return std::unexpected(GitError{-1, "partial staging not yet implemented"});
}
```

- [ ] **Step 6: Run to verify whole-file passes**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[stage\]' --output-on-failure`
Expected: PASS (whole-file cases). Now also re-run `[commit]` — it should pass.

- [ ] **Step 7: Commit**

```bash
git add core/include/gitgui/GitRepo.hpp core/src/GitRepo.cpp tests/test_git_repo_stage.cpp tests/CMakeLists.txt
git commit -m "feat(core): add whole-file GitRepo::stage/unstage"
```

---

## Task 6: build_patch — pure partial-diff serializer

A pure function that emits a minimal unified-diff buffer for a selection. This is the hard, error-prone part, so it is isolated and tested on its own before `GitRepo` uses it.

**Semantics (git add -p rules), staging an unstaged diff into the index:**
- Output one `diff --git` / `---` / `+++` / `@@` block for the file.
- Selected `Added` lines are kept as `+`. Selected `Removed` lines are kept as `-`.
- **Unselected** `Added` lines are dropped entirely (they stay only in the worktree).
- **Unselected** `Removed` lines become context ` ` lines (we are not removing them yet).
- `Context` lines stay context.
- `reverse=true` swaps `+`/`-` roles (used for unstage and discard) and swaps old/new in the header.
- Recompute the `@@ -oldStart,oldLines +newStart,newLines @@` counts from the emitted body.

**Files:**
- Modify: `core/include/gitgui/DiffEngine.hpp`, `core/src/DiffEngine.cpp`
- Test: `tests/test_build_patch.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Declare build_patch**

In `core/include/gitgui/DiffEngine.hpp`, inside `namespace gitgui`, add a free function:

```cpp
// Serialize a selection within a single hunk to a minimal unified-diff buffer
// suitable for git_apply. `gitPath` is the libgit2 (forward-slash) path.
// If sel.lineIndices is empty, the whole hunk is taken.
std::string build_patch(const std::string& gitPath,
                        const DiffHunk& hunk,
                        const StageSelection& sel,
                        bool reverse);
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_build_patch.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/DiffEngine.hpp"

using namespace gitgui;

static DiffHunk sample_hunk() {
    // Original file: a / b / c    New file: a / B2 / c
    // Lines: ctx 'a', -'b', +'B2', ctx 'c'
    DiffHunk h;
    h.oldStart = 1; h.oldLines = 3; h.newStart = 1; h.newLines = 3;
    h.lines = {
        {DiffLineOrigin::Context, 1, 1, "a"},
        {DiffLineOrigin::Removed, 2, -1, "b"},
        {DiffLineOrigin::Added,  -1,  2, "B2"},
        {DiffLineOrigin::Context, 3, 3, "c"},
    };
    return h;
}

TEST_CASE("build_patch whole hunk (forward) round-trips structure", "[patch]") {
    StageSelection sel{"f.txt", 0, {}};  // whole hunk
    std::string p = build_patch("f.txt", sample_hunk(), sel, /*reverse=*/false);

    REQUIRE(p.find("diff --git a/f.txt b/f.txt") != std::string::npos);
    REQUIRE(p.find("--- a/f.txt") != std::string::npos);
    REQUIRE(p.find("+++ b/f.txt") != std::string::npos);
    REQUIRE(p.find("@@ -1,3 +1,3 @@") != std::string::npos);
    REQUIRE(p.find("\n a\n") != std::string::npos);   // context
    REQUIRE(p.find("\n-b\n") != std::string::npos);
    REQUIRE(p.find("\n+B2\n") != std::string::npos);
}

TEST_CASE("build_patch unselected added line is dropped, removed becomes context",
          "[patch]") {
    // Select only the removed line (index 1). The added line (index 2) is NOT
    // selected -> dropped. Result: a / -b / c  => oldLines 3, newLines 2.
    StageSelection sel{"f.txt", 0, {1}};
    std::string p = build_patch("f.txt", sample_hunk(), sel, /*reverse=*/false);

    REQUIRE(p.find("@@ -1,3 +1,2 @@") != std::string::npos);
    REQUIRE(p.find("\n-b\n") != std::string::npos);
    REQUIRE(p.find("+B2") == std::string::npos);       // dropped
}

TEST_CASE("build_patch reverse swaps + and -", "[patch]") {
    StageSelection sel{"f.txt", 0, {}};
    std::string p = build_patch("f.txt", sample_hunk(), sel, /*reverse=*/true);
    // Reversed: the '+B2' becomes '-B2', the '-b' becomes '+b'.
    REQUIRE(p.find("\n-B2\n") != std::string::npos);
    REQUIRE(p.find("\n+b\n") != std::string::npos);
    REQUIRE(p.find("@@ -1,3 +1,3 @@") != std::string::npos);
}
```

- [ ] **Step 3: Register the test**

In `tests/CMakeLists.txt`, add `test_build_patch.cpp`.

- [ ] **Step 4: Run to verify it fails**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -20`
Expected: FAIL — `build_patch` undefined.

- [ ] **Step 5: Implement build_patch**

In `core/src/DiffEngine.cpp`, add `#include <sstream>` and `#include <set>` to the includes and this function inside `namespace gitgui`:

```cpp
std::string build_patch(const std::string& gitPath,
                        const DiffHunk& hunk,
                        const StageSelection& sel,
                        bool reverse) {
    const bool whole_hunk = sel.lineIndices.empty();
    std::set<int> selected(sel.lineIndices.begin(), sel.lineIndices.end());
    auto is_selected = [&](int i) { return whole_hunk || selected.count(i) > 0; };

    // Build the body and count old/new lines as we go.
    std::ostringstream body;
    int oldCount = 0, newCount = 0;
    for (int i = 0; i < static_cast<int>(hunk.lines.size()); ++i) {
        const DiffLine& ln = hunk.lines[i];
        DiffLineOrigin origin = ln.origin;
        // Reverse swaps added<->removed.
        if (reverse) {
            if (origin == DiffLineOrigin::Added)        origin = DiffLineOrigin::Removed;
            else if (origin == DiffLineOrigin::Removed) origin = DiffLineOrigin::Added;
        }

        if (origin == DiffLineOrigin::Context) {
            body << ' ' << ln.text << '\n';
            ++oldCount; ++newCount;
        } else if (origin == DiffLineOrigin::Added) {
            if (is_selected(i)) { body << '+' << ln.text << '\n'; ++newCount; }
            // unselected added line: drop entirely.
        } else {  // Removed
            if (is_selected(i)) { body << '-' << ln.text << '\n'; ++oldCount; }
            else { body << ' ' << ln.text << '\n'; ++oldCount; ++newCount; }  // keep as context
        }
    }

    std::ostringstream out;
    out << "diff --git a/" << gitPath << " b/" << gitPath << '\n'
        << "--- a/" << gitPath << '\n'
        << "+++ b/" << gitPath << '\n'
        << "@@ -" << hunk.oldStart << ',' << oldCount
        << " +" << hunk.newStart << ',' << newCount << " @@\n"
        << body.str();
    return out.str();
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[patch\]' --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add core/include/gitgui/DiffEngine.hpp core/src/DiffEngine.cpp tests/test_build_patch.cpp tests/CMakeLists.txt
git commit -m "feat(core): add build_patch partial-diff serializer"
```

---

## Task 7: GitRepo partial stage/unstage via git_apply

**Files:**
- Modify: `core/src/GitRepo.cpp`
- Test: extend `tests/test_git_repo_stage.cpp`

- [ ] **Step 1: Write the failing test (hunk + line)**

Append to `tests/test_git_repo_stage.cpp`:

```cpp
TEST_CASE("stage a single hunk stages only that change", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "test@example.com");
    // Two separate change regions far apart so they form two hunks.
    tmp.write_file("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "ONE\n2\n3\n4\n5\n6\n7\n8\nNINE\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 2);

    // Stage only the first hunk.
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", 0, {}}).has_value());

    // Staged diff (index vs HEAD) now contains exactly one hunk (the first).
    auto staged = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);

    // The worktree still has the second change unstaged.
    auto unstaged = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(unstaged.has_value());
    REQUIRE(unstaged->hunks.size() == 1);
}

TEST_CASE("unstage a staged hunk returns it to the worktree", "[stage]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "test@example.com");
    tmp.write_file("a.txt", "1\n2\n3\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "1\nTWO\n3\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto staged = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(staged.has_value());
    REQUIRE(staged->hunks.size() == 1);

    // Unstage that one hunk.
    REQUIRE(repo->unstage(gitgui::StageSelection{"a.txt", 0, {}}).has_value());

    auto after = repo->diff(gitgui::DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(after.has_value());
    REQUIRE(after->hunks.empty());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[stage\]' --output-on-failure`
Expected: FAIL — partial selections hit the Task-5 stub returning "partial staging not yet implemented".

- [ ] **Step 3: Replace the apply_partial stub**

In `core/src/GitRepo.cpp`, add `#include "gitgui/DiffEngine.hpp"` (already added in Task 3) and replace the stub with the real implementation. For **stage**, diff worktree-vs-index, select the hunk, build a forward patch, and `git_apply` to the index. For **unstage**, diff index-vs-HEAD, build a **reverse** patch, and `git_apply` to the index.

```cpp
Expected<void> GitRepo::apply_partial(const StageSelection& sel, bool stage) {
    // 1. Get the diff that contains the selected hunk.
    DiffTarget target = stage ? DiffTarget::WorktreeVsIndex : DiffTarget::IndexVsHead;
    auto fileDiff = diff(target, sel.path);
    if (!fileDiff) return std::unexpected(fileDiff.error());

    int hi = sel.hunkIndex.value_or(-1);
    if (hi < 0 || hi >= static_cast<int>(fileDiff->hunks.size()))
        return std::unexpected(GitError{-1, "hunk index out of range"});

    // 2. Build the patch buffer. Unstage reverses the index->HEAD diff.
    std::string patch =
        build_patch(to_git_path(sel.path), fileDiff->hunks[hi], sel, /*reverse=*/!stage);

    // 3. Parse the buffer into a git_diff and apply it to the index.
    git_diff* raw = nullptr;
    int rc = git_diff_from_buffer(&raw, patch.data(), patch.size());
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    rc = git_apply(repo_, raw, GIT_APPLY_LOCATION_INDEX, nullptr);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    return {};
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[stage\]' --output-on-failure`
Expected: PASS (all stage cases, whole-file + hunk).

- [ ] **Step 5: Commit**

```bash
git add core/src/GitRepo.cpp tests/test_git_repo_stage.cpp
git commit -m "feat(core): add hunk/line partial stage/unstage via git_apply"
```

---

## Task 8: GitRepo::discard

**Files:**
- Modify: `core/include/gitgui/GitRepo.hpp`, `core/src/GitRepo.cpp`
- Test: `tests/test_git_repo_discard.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Declare discard**

In `core/include/gitgui/GitRepo.hpp`, inside `public:`:

```cpp
    // Revert worktree changes for the selection (whole file or hunk/lines).
    Expected<void> discard(const StageSelection& sel);
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_git_repo_discard.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "support/TempRepo.hpp"
#include <fstream>
#include <sstream>

static std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("discard whole file restores committed content", "[discard]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "test@example.com");
    tmp.write_file("a.txt", "orig\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "changed\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->discard(gitgui::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    REQUIRE(read_file(tmp.path() / "a.txt") == "orig\n");
}

TEST_CASE("discard a hunk reverts only that region", "[discard]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "test@example.com");
    tmp.write_file("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    tmp.commit_all("init");
    tmp.write_file("a.txt", "ONE\n2\n3\n4\n5\n6\n7\n8\nNINE\n");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(gitgui::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 2);

    // Discard only the first hunk (the ONE change); NINE stays.
    REQUIRE(repo->discard(gitgui::StageSelection{"a.txt", 0, {}}).has_value());

    std::string after = read_file(tmp.path() / "a.txt");
    REQUIRE(after.find("ONE") == std::string::npos);  // reverted
    REQUIRE(after.find("NINE") != std::string::npos); // kept
    REQUIRE(after.substr(0, 2) == "1\n");
}
```

- [ ] **Step 3: Register the test**

In `tests/CMakeLists.txt`, add `test_git_repo_discard.cpp`.

- [ ] **Step 4: Run to verify it fails**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -20`
Expected: FAIL — `GitRepo::discard` undefined.

- [ ] **Step 5: Implement discard**

In `core/src/GitRepo.cpp`, add inside `namespace gitgui`:

```cpp
Expected<void> GitRepo::discard(const StageSelection& sel) {
    std::string p = to_git_path(sel.path);

    if (is_whole_file(sel)) {
        // Force-checkout the file from the index/HEAD, overwriting the worktree.
        git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
        opts.checkout_strategy = GIT_CHECKOUT_FORCE;
        char* paths[] = {p.data()};
        opts.paths.strings = paths;
        opts.paths.count = 1;
        int rc = git_checkout_head(repo_, &opts);
        if (rc < 0) return std::unexpected(last_git_error(rc));
        return {};
    }

    // Hunk/line: reverse-apply the worktree-vs-index patch to the WORKDIR.
    auto fileDiff = diff(DiffTarget::WorktreeVsIndex, sel.path);
    if (!fileDiff) return std::unexpected(fileDiff.error());
    int hi = sel.hunkIndex.value_or(-1);
    if (hi < 0 || hi >= static_cast<int>(fileDiff->hunks.size()))
        return std::unexpected(GitError{-1, "hunk index out of range"});

    std::string patch =
        build_patch(p, fileDiff->hunks[hi], sel, /*reverse=*/true);

    git_diff* raw = nullptr;
    int rc = git_diff_from_buffer(&raw, patch.data(), patch.size());
    if (rc < 0) return std::unexpected(last_git_error(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    rc = git_apply(repo_, raw, GIT_APPLY_LOCATION_WORKDIR, nullptr);
    if (rc < 0) return std::unexpected(last_git_error(rc));
    return {};
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build -R '\[discard\]' --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add core/include/gitgui/GitRepo.hpp core/src/GitRepo.cpp tests/test_git_repo_discard.cpp tests/CMakeLists.txt
git commit -m "feat(core): add GitRepo::discard (file + hunk/line)"
```

---

## Task 9: Full suite green + final review

- [ ] **Step 1: Build and run the entire Core test suite**

Run: `cmake --build build --target gitgui_core_tests 2>&1 | tail -5 && ctest --test-dir build --output-on-failure`
Expected: all tests PASS (diff, patch, stage, discard, commit, plus the pre-existing repo/status/store tests).

- [ ] **Step 2: Confirm no Qt leaked into Core**

Run: `grep -rn "Q[A-Z]\|#include <Q" core/ || echo "clean: no Qt in core"`
Expected: `clean: no Qt in core`.

- [ ] **Step 3: Commit any final fixups** (only if Step 1/2 required edits)

```bash
git add -A && git commit -m "test(core): finalize Plan 3a git-ops suite"
```

---

## Notes for Plan 3b (not in scope here)

The verified Core surface this plan delivers — `GitRepo::diff / stage / unstage / discard / commit`, `DiffResult`/`DiffHunk`/`DiffLine`, `StageSelection`, `CommitRequest` — is the exact contract Plan 3b will wrap in `AsyncRepo` (QtConcurrent + QCoro), drive from `DashboardModel::refreshAsync`, and surface in the Changes tab. Plan 3b is authored after 3a is green so its signatures match reality.
```

---

## Outcome

`GitRepo` gained `diff`/`stage`/`unstage`/`discard`/`commit` at file/hunk/line granularity, plus `DiffEngine` and the pure `build_patch` helper. Core-only, Catch2-tested.
