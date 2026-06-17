# Plan 5a — Core: Graph Types + GitRepo::log + GraphBuilder

| | |
|--|--|
| **Date** | 2026-06-17 |
| **Status** | `done` |
| **Spec** | [engineering](../spec/engineering/engineering.md) · [product](../spec/product/product.md) |
| **Depends on** | [Core foundation](2026-06-16-core-foundation.md) |

**Goal:** Add `CommitNode`/`GraphLayout` types, `GitRepo::log()` commit walk, and `GraphBuilder::build()` lane-assignment algorithm to the Core library.

**Architecture:** All new code lives in `core/` — pure C++23, Qt-free, Catch2-tested. `GitRepo::log()` is a `const` method using `git_revwalk`. `GraphBuilder::build()` is a pure static function that takes a `vector<CommitNode>` and assigns `lane` indices plus computes per-row passthrough and edge metadata. No global state.

**Tech Stack:** C++23, libgit2, Catch2.

## Global Constraints

- All code in `core/` — zero Qt, zero `#include <Q*>`.
- `Expected<T>` = `std::expected<T, GitError>`; use `last_git_error(rc)` after failing libgit2 calls.
- Path rule: `to_git_path(path)` at libgit2 boundary, never `.string()`.
- Build: `cmake --build build -j` from repo root. Core tests: `ctest --test-dir build -R gitgui_core_tests --output-on-failure`.
- Commit after each task passes.

---

## File Structure

**Created:**
- `core/include/gitgui/Graph.hpp` — `CommitNode`, `GraphEdge`, `GraphRow`, `GraphLayout` types.
- `core/include/gitgui/GraphBuilder.hpp` — `GraphBuilder` class declaration.
- `core/src/GraphBuilder.cpp` — lane-assignment algorithm.
- `tests/test_git_repo_log.cpp` — Catch2 tests for `GitRepo::log`.
- `tests/test_graph_builder.cpp` — Catch2 tests for `GraphBuilder::build`.

**Modified:**
- `core/include/gitgui/GitRepo.hpp` — add `log(unsigned limit)` declaration.
- `core/src/GitRepo.cpp` — implement `log()`.
- `tests/CMakeLists.txt` — add two new test files.

---

## Task 1: Graph types header

**Files:**
- Create: `core/include/gitgui/Graph.hpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace gitgui {
  struct CommitNode { std::string oid, summary, author; int64_t time = 0;
                      std::vector<std::string> parents; int lane = 0; };
  struct GraphEdge  { int fromLane; int toLane;
                      bool operator==(const GraphEdge&) const = default; };
  struct GraphRow   { CommitNode commit; bool lineFromAbove = false;
                      std::vector<int> passThroughs;
                      std::vector<GraphEdge> outEdges; };
  struct GraphLayout { std::vector<GraphRow> rows; int laneCount = 0; };
  }
  ```

- [ ] **Step 1: Create `core/include/gitgui/Graph.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gitgui {

// A single commit as returned by GitRepo::log. lane is filled by GraphBuilder.
struct CommitNode {
    std::string oid;      // full SHA-1 hex
    std::string summary;  // first line of commit message
    std::string author;   // author name
    int64_t     time = 0; // author unix timestamp
    std::vector<std::string> parents;  // parent OIDs (empty = initial commit)
    int lane = 0;         // column index; 0 = leftmost; filled by GraphBuilder
};

// A directed edge in the graph: line from fromLane (this row) to toLane (next row).
struct GraphEdge {
    int fromLane = 0;
    int toLane   = 0;
    bool operator==(const GraphEdge&) const = default;
};

// One row of the rendered graph. The delegate needs all three fields to draw
// the top half (lineFromAbove), the dot (commit.lane), and the bottom half (outEdges).
struct GraphRow {
    CommitNode commit;

    // Draw a vertical line from the top of this cell to the circle?
    // True when a previous commit had an outEdge pointing to this commit's lane.
    // False for branch heads (first occurrence of this commit in the walk).
    bool lineFromAbove = false;

    // Lane indices that carry an unbroken vertical line through this entire row
    // (neither starting nor ending here). Never includes commit.lane.
    std::vector<int> passThroughs;

    // Lines from this commit's circle down to each parent's lane (bottom half).
    // fromLane is always commit.lane. toLane is the parent's assigned lane.
    std::vector<GraphEdge> outEdges;
};

struct GraphLayout {
    std::vector<GraphRow> rows;
    int laneCount = 0;  // max lane index used + 1
};

}  // namespace gitgui
```

- [ ] **Step 2: Build — verify it compiles**

```bash
cmake --build build -j 2>&1 | tail -5
```

Expected: no errors (header-only, nothing links to it yet).

- [ ] **Step 3: Commit**

```bash
git add core/include/gitgui/Graph.hpp
git commit -m "feat(core): Graph types — CommitNode, GraphEdge, GraphRow, GraphLayout"
```

---

## Task 2: `GitRepo::log` — commit walk

**Files:**
- Modify: `core/include/gitgui/GitRepo.hpp`
- Modify: `core/src/GitRepo.cpp`
- Create: `tests/test_git_repo_log.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CommitNode` from `Graph.hpp`; `LibGit2Context`, `TempRepo` for tests.
- Produces:
  ```cpp
  // In GitRepo:
  // Walk commits reachable from HEAD in reverse-topological + time order.
  // Returns an empty vector if the repo has no commits (no HEAD).
  // limit = 0 means no limit.
  Expected<std::vector<CommitNode>> log(unsigned limit = 1000) const;
  ```

- [ ] **Step 1: Write failing tests**

Create `tests/test_git_repo_log.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/GitRepo.hpp"
#include "gitgui/Graph.hpp"
#include "gitgui/LibGit2Context.hpp"
#include "support/TempRepo.hpp"
#include <filesystem>
#include <random>

namespace {
std::filesystem::path unique_empty_dir_log() {
    std::mt19937_64 rng{std::random_device{}()};
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui_log_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}
}  // namespace

TEST_CASE("GitRepo::log on empty repo returns empty vector", "[git_repo][log]") {
    gitgui::LibGit2Context ctx;
    auto dir = unique_empty_dir_log();
    auto repo = gitgui::GitRepo::init(dir);
    REQUIRE(repo.has_value());

    auto result = repo->log();
    REQUIRE(result.has_value());
    REQUIRE(result->empty());

    std::filesystem::remove_all(dir);
}

TEST_CASE("GitRepo::log returns commits newest-first", "[git_repo][log]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "t@t.test");
    tmp.write_file("a.txt", "a");
    tmp.commit_all("first");
    tmp.write_file("b.txt", "b");
    tmp.commit_all("second");

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto result = repo->log();
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    REQUIRE(result->at(0).summary == "second");
    REQUIRE(result->at(1).summary == "first");
    REQUIRE(!result->at(0).oid.empty());
    REQUIRE(result->at(0).oid.size() == 40);           // full SHA-1 hex
    REQUIRE(result->at(0).parents.size() == 1);
    REQUIRE(result->at(0).parents[0] == result->at(1).oid);
    REQUIRE(result->at(1).parents.empty());
    REQUIRE(!result->at(0).author.empty());
    REQUIRE(result->at(0).time > 0);
}

TEST_CASE("GitRepo::log respects the limit parameter", "[git_repo][log]") {
    gitgui::test::TempRepo tmp;
    tmp.set_identity("Test", "t@t.test");
    for (int i = 0; i < 5; ++i) {
        tmp.write_file("f.txt", std::to_string(i));
        tmp.commit_all("commit " + std::to_string(i));
    }

    auto repo = gitgui::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto result = repo->log(3);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);
}
```

- [ ] **Step 2: Add test file to CMakeLists**

In `tests/CMakeLists.txt`, add `test_git_repo_log.cpp` to `add_executable(gitgui_core_tests ...)` after `test_git_repo_init_clone.cpp`:

```cmake
  test_git_repo_log.cpp
```

- [ ] **Step 3: Run — verify FAIL to compile**

```bash
cmake --build build -j 2>&1 | tail -10
```

Expected: compile error — `GitRepo` has no `log`.

- [ ] **Step 4: Declare `log` in `GitRepo.hpp`**

Add `#include "gitgui/Graph.hpp"` after the existing includes. Add after the `discard` declaration:

```cpp
    // Walk commits reachable from HEAD, newest first (topological + time).
    // Returns empty vector if repo has no commits. limit=0 means unlimited.
    Expected<std::vector<CommitNode>> log(unsigned limit = 1000) const;
```

- [ ] **Step 5: Implement `log` in `GitRepo.cpp`**

Add at the top with other includes:
```cpp
#include <git2/revwalk.h>
#include <git2/commit.h>
```

Add the implementation before the closing `}  // namespace gitgui`:

```cpp
Expected<std::vector<CommitNode>> GitRepo::log(unsigned limit) const {
    git_revwalk* walk = nullptr;
    int rc = git_revwalk_new(&walk, repo_);
    if (rc < 0) return std::unexpected(last_git_error(rc));

    git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

    rc = git_revwalk_push_head(walk);
    if (rc < 0) {
        git_revwalk_free(walk);
        if (rc == GIT_ENOTFOUND) return std::vector<CommitNode>{};  // no HEAD yet
        return std::unexpected(last_git_error(rc));
    }

    std::vector<CommitNode> result;
    git_oid oid;
    unsigned count = 0;

    while ((limit == 0 || count < limit) && git_revwalk_next(&oid, walk) == 0) {
        git_commit* c = nullptr;
        if (git_commit_lookup(&c, repo_, &oid) < 0) continue;

        CommitNode node;

        char hex[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(hex, sizeof(hex), &oid);
        node.oid = hex;

        const char* msg = git_commit_summary(c);
        node.summary = msg ? msg : "";

        const git_signature* author = git_commit_author(c);
        node.author = author ? author->name : "";
        node.time   = author ? author->when.time : 0;

        unsigned nparents = git_commit_parentcount(c);
        node.parents.reserve(nparents);
        for (unsigned i = 0; i < nparents; ++i) {
            const git_oid* pid = git_commit_parent_id(c, i);
            char phex[GIT_OID_SHA1_HEXSIZE + 1];
            git_oid_tostr(phex, sizeof(phex), pid);
            node.parents.push_back(phex);
        }

        git_commit_free(c);
        result.push_back(std::move(node));
        ++count;
    }

    git_revwalk_free(walk);
    return result;
}
```

- [ ] **Step 6: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_core_tests --output-on-failure
```

Expected: all 3 new log tests pass, no regressions.

- [ ] **Step 7: Commit**

```bash
git add core/include/gitgui/GitRepo.hpp core/src/GitRepo.cpp \
        tests/test_git_repo_log.cpp tests/CMakeLists.txt
git commit -m "feat(core): GitRepo::log — commit walk via git_revwalk"
```

---

## Task 3: `GraphBuilder` — linear history

**Files:**
- Create: `core/include/gitgui/GraphBuilder.hpp`
- Create: `core/src/GraphBuilder.cpp`
- Create: `tests/test_graph_builder.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CommitNode`, `GraphEdge`, `GraphRow`, `GraphLayout` from `Graph.hpp`.
- Produces:
  ```cpp
  namespace gitgui {
  class GraphBuilder {
  public:
      // Assign lane indices and compute per-row edge metadata.
      // Input commits must be in walk order (newest first, topological).
      // Returns GraphLayout with rows in the same order as input.
      static GraphLayout build(std::vector<CommitNode> commits);
  };
  }
  ```

- [ ] **Step 1: Write failing tests (linear history only)**

Create `tests/test_graph_builder.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "gitgui/Graph.hpp"
#include "gitgui/GraphBuilder.hpp"

// Helpers
static gitgui::CommitNode make_commit(std::string oid,
                                      std::vector<std::string> parents) {
    gitgui::CommitNode n;
    n.oid     = std::move(oid);
    n.summary = n.oid;
    n.parents = std::move(parents);
    return n;
}

TEST_CASE("GraphBuilder: empty input returns empty layout", "[graph]") {
    auto layout = gitgui::GraphBuilder::build({});
    REQUIRE(layout.rows.empty());
    REQUIRE(layout.laneCount == 0);
}

TEST_CASE("GraphBuilder: single commit (initial) at lane 0", "[graph]") {
    auto layout = gitgui::GraphBuilder::build({ make_commit("a", {}) });

    REQUIRE(layout.rows.size() == 1);
    REQUIRE(layout.rows[0].commit.lane == 0);
    REQUIRE(!layout.rows[0].lineFromAbove);  // no predecessor
    REQUIRE(layout.rows[0].passThroughs.empty());
    REQUIRE(layout.rows[0].outEdges.empty());
    REQUIRE(layout.laneCount == 1);
}

TEST_CASE("GraphBuilder: linear chain — all commits at lane 0", "[graph]") {
    std::vector<gitgui::CommitNode> commits = {
        make_commit("c", {"b"}),
        make_commit("b", {"a"}),
        make_commit("a", {}),
    };
    auto layout = gitgui::GraphBuilder::build(commits);

    REQUIRE(layout.rows.size() == 3);
    for (const auto& row : layout.rows)
        REQUIRE(row.commit.lane == 0);
    REQUIRE(layout.laneCount == 1);
}

TEST_CASE("GraphBuilder: linear chain — outEdges and lineFromAbove", "[graph]") {
    std::vector<gitgui::CommitNode> commits = {
        make_commit("b", {"a"}),
        make_commit("a", {}),
    };
    auto layout = gitgui::GraphBuilder::build(commits);

    // b: HEAD, no incoming line
    REQUIRE(!layout.rows[0].lineFromAbove);
    REQUIRE(layout.rows[0].outEdges.size() == 1);
    REQUIRE(layout.rows[0].outEdges[0] == (gitgui::GraphEdge{0, 0}));
    REQUIRE(layout.rows[0].passThroughs.empty());

    // a: line arrives from above
    REQUIRE(layout.rows[1].lineFromAbove);
    REQUIRE(layout.rows[1].outEdges.empty());
    REQUIRE(layout.rows[1].passThroughs.empty());
}
```

- [ ] **Step 2: Add test file to CMakeLists**

In `tests/CMakeLists.txt`, add after `test_git_repo_log.cpp`:

```cmake
  test_graph_builder.cpp
```

- [ ] **Step 3: Run — verify FAIL to compile**

```bash
cmake --build build -j 2>&1 | tail -10
```

Expected: compile error — `GraphBuilder` not declared.

- [ ] **Step 4: Create `GraphBuilder.hpp`**

```cpp
// core/include/gitgui/GraphBuilder.hpp
#pragma once
#include "gitgui/Graph.hpp"
#include <vector>

namespace gitgui {

class GraphBuilder {
public:
    // Assign lane indices and compute per-row passthrough/edge metadata.
    // commits must be in topological walk order (newest first).
    static GraphLayout build(std::vector<CommitNode> commits);
};

}  // namespace gitgui
```

- [ ] **Step 5: Create `GraphBuilder.cpp` — linear-only stub**

```cpp
// core/src/GraphBuilder.cpp
#include "gitgui/GraphBuilder.hpp"
#include <algorithm>
#include <string>

namespace gitgui {

namespace {

// Index of first slot containing oid, or -1 if not found.
int lane_of(const std::vector<std::string>& active, const std::string& oid) {
    for (int i = 0; i < static_cast<int>(active.size()); ++i)
        if (active[i] == oid) return i;
    return -1;
}

// Index of first empty ("") slot, appending one if needed.
int alloc_lane(std::vector<std::string>& active) {
    for (int i = 0; i < static_cast<int>(active.size()); ++i)
        if (active[i].empty()) return i;
    active.push_back("");
    return static_cast<int>(active.size()) - 1;
}

}  // namespace

GraphLayout GraphBuilder::build(std::vector<CommitNode> commits) {
    GraphLayout layout;
    if (commits.empty()) return layout;

    std::vector<std::string> active;  // active[i] = OID expected at lane i
    int max_lane = 0;

    for (auto& commit : commits) {
        // 1. Find or assign this commit's lane.
        int cl = lane_of(active, commit.oid);
        const bool was_in_active = (cl >= 0);
        if (!was_in_active) {
            cl = alloc_lane(active);
        }
        commit.lane = cl;
        max_lane = std::max(max_lane, cl);

        // 2. Update active: first parent inherits this lane, extra parents get new lanes.
        if (commit.parents.empty()) {
            active[cl] = "";
        } else {
            active[cl] = commit.parents[0];
        }
        for (std::size_t pi = 1; pi < commit.parents.size(); ++pi) {
            if (lane_of(active, commit.parents[pi]) < 0) {
                int slot = alloc_lane(active);
                active[slot] = commit.parents[pi];
                max_lane = std::max(max_lane, slot);
            }
        }

        // 3. outEdges: for each parent, find its lane now in active.
        std::vector<GraphEdge> out;
        for (const auto& p : commit.parents) {
            int pl = lane_of(active, p);
            if (pl >= 0)
                out.push_back({cl, pl});
        }

        // 4. passThroughs: every non-empty active lane that is not this commit's lane.
        std::vector<int> pass;
        for (int i = 0; i < static_cast<int>(active.size()); ++i)
            if (!active[i].empty() && i != cl)
                pass.push_back(i);

        layout.rows.push_back(GraphRow{
            std::move(commit),
            was_in_active,
            std::move(pass),
            std::move(out),
        });
    }

    layout.laneCount = max_lane + 1;
    return layout;
}

}  // namespace gitgui
```

- [ ] **Step 6: Add `GraphBuilder.cpp` to core CMakeLists**

In `core/CMakeLists.txt`, add to `add_library(gitgui_core ...)` sources:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/GraphBuilder.cpp
```

- [ ] **Step 7: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_core_tests --output-on-failure
```

Expected: all linear-history tests pass.

- [ ] **Step 8: Commit**

```bash
git add core/include/gitgui/GraphBuilder.hpp core/src/GraphBuilder.cpp \
        core/CMakeLists.txt \
        tests/test_graph_builder.cpp tests/CMakeLists.txt
git commit -m "feat(core): GraphBuilder::build — lane assignment for linear history"
```

---

## Task 4: `GraphBuilder` — branch and merge

**Files:**
- Modify: `tests/test_graph_builder.cpp` — append branch/merge tests.

No implementation changes needed — the algorithm written in Task 3 already handles multiple parents. These tests verify correctness.

**Interfaces:**
- Consumes: `GraphBuilder::build`, `GraphEdge::operator==`, all from Task 3.

- [ ] **Step 1: Append tests to `tests/test_graph_builder.cpp`**

```cpp
TEST_CASE("GraphBuilder: diamond topology (fork then merge)", "[graph]") {
    // Topological order (newest first): A(merge)→{B,C}, B→D, C→D, D(initial)
    std::vector<gitgui::CommitNode> commits = {
        make_commit("a", {"b", "c"}),  // merge commit
        make_commit("b", {"d"}),
        make_commit("c", {"d"}),
        make_commit("d", {}),
    };
    auto layout = gitgui::GraphBuilder::build(commits);
    REQUIRE(layout.rows.size() == 4);

    // A: merge commit at lane 0, no predecessor, two outEdges
    const auto& rowA = layout.rows[0];
    REQUIRE(rowA.commit.lane == 0);
    REQUIRE(!rowA.lineFromAbove);
    REQUIRE(rowA.outEdges.size() == 2);
    REQUIRE(rowA.outEdges[0] == (gitgui::GraphEdge{0, 0}));  // A→B stays lane 0
    REQUIRE(rowA.outEdges[1] == (gitgui::GraphEdge{0, 1}));  // A→C goes to lane 1
    REQUIRE(rowA.passThroughs.empty());

    // B: lane 0, line from above, passthrough lane 1 (C waiting)
    const auto& rowB = layout.rows[1];
    REQUIRE(rowB.commit.lane == 0);
    REQUIRE(rowB.lineFromAbove);
    REQUIRE(rowB.passThroughs == std::vector<int>{1});
    REQUIRE(rowB.outEdges.size() == 1);
    REQUIRE(rowB.outEdges[0] == (gitgui::GraphEdge{0, 0}));  // B→D, D at lane 0

    // C: lane 1, line from above, passthrough lane 0 (D waiting)
    const auto& rowC = layout.rows[2];
    REQUIRE(rowC.commit.lane == 1);
    REQUIRE(rowC.lineFromAbove);
    REQUIRE(rowC.passThroughs == std::vector<int>{0});
    REQUIRE(rowC.outEdges.size() == 1);
    REQUIRE(rowC.outEdges[0] == (gitgui::GraphEdge{1, 0}));  // C→D at lane 0

    // D: lane 0, line from above (two branches converge here), no outEdges
    const auto& rowD = layout.rows[3];
    REQUIRE(rowD.commit.lane == 0);
    REQUIRE(rowD.lineFromAbove);
    REQUIRE(rowD.passThroughs.empty());
    REQUIRE(rowD.outEdges.empty());

    REQUIRE(layout.laneCount == 2);
}

TEST_CASE("GraphBuilder: two independent branches, no merge", "[graph]") {
    // A and B are both branch heads (independent); C is base of B; D is base of A.
    // Walk order: A, B, C, D  (A and B are unrelated)
    std::vector<gitgui::CommitNode> commits = {
        make_commit("a", {"d"}),
        make_commit("b", {"c"}),
        make_commit("c", {}),
        make_commit("d", {}),
    };
    auto layout = gitgui::GraphBuilder::build(commits);
    REQUIRE(layout.rows.size() == 4);

    // A: lane 0, no predecessor, outEdge to D (also lane 0)
    REQUIRE(layout.rows[0].commit.lane == 0);
    REQUIRE(!layout.rows[0].lineFromAbove);

    // B: lane 1 (new branch), no predecessor
    REQUIRE(layout.rows[1].commit.lane == 1);
    REQUIRE(!layout.rows[1].lineFromAbove);

    // C: lane 1 (B's parent), line from above
    REQUIRE(layout.rows[2].commit.lane == 1);
    REQUIRE(layout.rows[2].lineFromAbove);

    // D: lane 0 (A's parent), line from above
    REQUIRE(layout.rows[3].commit.lane == 0);
    REQUIRE(layout.rows[3].lineFromAbove);

    REQUIRE(layout.laneCount == 2);
}
```

- [ ] **Step 2: Build + run — verify PASS**

```bash
cmake --build build -j
ctest --test-dir build -R gitgui_core_tests --output-on-failure
```

Expected: all graph builder tests pass (linear + diamond + two-branch), no regressions.

- [ ] **Step 3: Run full suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all Core + UI tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_graph_builder.cpp
git commit -m "test(core): GraphBuilder branch+merge tests — diamond and independent branches"
```

---

## Self-Review

**1. Spec coverage:**
- `CommitNode` with `lane`, `summary`, `author`, `time`, `parents` — ✅ Task 1.
- `GraphLayout` / `GraphRow` / `GraphEdge` — ✅ Task 1.
- `GitRepo::log` commit walk, newest-first, with limit — ✅ Task 2.
- `GraphBuilder::build` pure function, testable with synthetic DAGs — ✅ Tasks 3–4.
- Linear history (all lane 0) — ✅ Task 3.
- Fork, merge, diamond topology — ✅ Task 4.
- Empty repo / empty input — ✅ Tasks 2, 3.

**2. Placeholder scan:** None. All code shown inline.

**3. Type consistency:**
- `GraphEdge{fromLane, toLane}` declared in Task 1, used in Tasks 3–4 with `{cl, pl}` and `operator==` — consistent.
- `GraphRow{commit, lineFromAbove, passThroughs, outEdges}` — aggregate-initialized in that order in Task 3 impl, matches struct declaration order in Task 1.
- `lane_of` / `alloc_lane` helpers are file-scope in anonymous namespace — no leakage.
- `git_commit_summary` returns the first line (what the spec calls "summary") — correct libgit2 call.

**Notes:**
- `git_revwalk_push_head` returns `GIT_ENOTFOUND` for repos with no commits (no HEAD yet). Handled as empty-vector, not an error.
- `GraphBuilder` does NOT call `GitRepo::log`; it is a pure transformation. Tests use hand-crafted vectors so they run without libgit2.
- `laneCount` is `max_lane + 1` where `max_lane` is tracked during the walk. For an empty input it stays 0.

---

## Outcome

Added `CommitNode`/`GraphLayout` types, `GitRepo::log()` (revwalk), and `GraphBuilder::build()` lane-assignment. Core-only, Catch2-tested.
