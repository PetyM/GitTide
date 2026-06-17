# Plan 3 Design — Changes Tab + Async Git Ops

Refinement of the master design (`2026-06-16-gitgui-design.md`) for the third
implementation phase. The master spec fixes the overall architecture; this
document records the Plan-3-specific decisions reached in brainstorming.

## Scope

In scope:

- **Core git operations**: per-file diff, stage / unstage / discard at
  **file, hunk, and line granularity**, and commit.
- **DiffEngine**: parse a libgit2 `git_diff` into a display model.
- **Async layer**: parallel dashboard status and off-main-thread diff/commit via
  QtConcurrent + QCoro.
- **Changes tab UI**: staged/unstaged file lists, diff view with partial-staging
  interactions, commit message box.

Out of scope (later plans): commit graph / history view and its virtualized
rendering (Plan 4); network operations (fetch/push); submodule trees.

## Key Decisions

1. **Full file + hunk + line staging granularity** in this phase (complete spec
   goal), not a file-only subset.
2. **Concurrency = QtConcurrent + QCoro.** `QtConcurrent::run` dispatches blocking
   `GitRepo` work onto Qt's global thread pool; `qCoro(QFuture)` turns it into a
   `co_await`-able task. No heavy new dependency (QtConcurrent ships with Qt6);
   QCoro added via FetchContent. Rejected: `std::execution par_unseq` (drags in
   TBB on libstdc++/Linux) and a hand-rolled thread pool (reinvents QtConcurrent).
3. **Patch synthesis lives in Core, never UI.** `GitRepo::stage(selection)` and
   friends take a `StageSelection` and build + apply the patch internally via
   `git_apply`. `DiffEngine`/`DiffView` only parse and display diffs; they never
   construct git patches. Keeps partial-staging logic Qt-free and Catch2-testable.
4. **Dashboard thread-safety by isolation.** Each parallel status task opens its
   **own** `GitRepo` instance in the worker — no shared mutable state across
   threads, satisfying the master spec's "one owner per repo" rule for free. The
   active window's single `GitRepo` (diff/stage/commit) serializes through a
   per-repo single-slot guard so two `co_await`-ed ops cannot overlap on the pool.

## Core API Additions (`GitRepo`, no Qt)

```cpp
// Diff target selects which pair of trees to compare.
enum class DiffTarget { WorktreeVsIndex, IndexVsHead };

// A selection within a file: empty hunk+lines => whole file;
// hunkIndex set, lineIndices empty => whole hunk; lineIndices set => those lines.
struct StageSelection {
    std::filesystem::path path;          // repo-relative
    std::optional<int> hunkIndex;
    std::vector<int> lineIndices;        // indices into the hunk's line vector
};

struct CommitRequest { std::string message; };  // author/committer from git config

Expected<DiffResult> diff(DiffTarget, const std::filesystem::path& file) const;
Expected<void>       stage(const StageSelection&);
Expected<void>       unstage(const StageSelection&);
Expected<void>       discard(const StageSelection&);   // revert worktree
Expected<std::string> commit(const CommitRequest&);    // returns new commit oid
```

### DiffEngine model

```cpp
enum class DiffLineOrigin { Context, Added, Removed };
struct DiffLine { DiffLineOrigin origin; int oldLineno; int newLineno; std::string text; };
struct DiffHunk { int oldStart; int newStart; std::vector<DiffLine> lines; };
struct DiffResult { std::vector<DiffHunk> hunks; };
```

`DiffEngine::parse(git_diff*)` is a pure function over a libgit2 diff — testable
against synthetic repos. Patch synthesis for partial staging reuses the same hunk
model to emit a minimal `git_diff` buffer fed to `git_apply` (index location for
stage/unstage, worktree for discard).

## Async Layer (UI side)

- **`AsyncRepo`** — thin helper wrapping each blocking `GitRepo` call in
  `QtConcurrent::run` and exposing a `QCoro` task. Holds the per-repo single-slot
  guard so overlapping operations queue rather than race.
- **`DashboardModel::refreshAsync(repos)`** — fans out one `QtConcurrent::run`
  per repo (each opens its own `GitRepo`), gathers results, updates rows. Row
  layout and roles are unchanged from the Plan-2 synchronous version (drop-in
  replacement, as the existing header comment promised).

## Changes Tab UI

- **`ChangesView`** — left: staged and unstaged file `QListView`s populated from
  `GitRepo::status()`; right: `DiffView`; bottom: commit message
  `QPlainTextEdit` + Commit button (enabled when a message is present and
  something is staged).
- **`DiffView`** — renders a `DiffResult`'s hunks. Selecting a hunk or a line
  range emits `stageRequested(StageSelection)` / `unstageRequested` /
  `discardRequested`. Single-file diffs are small; virtualized rendering is a
  graph concern (Plan 4), not needed here.
- **Wiring** — `RepoController` gains `stage / unstage / discard / commit /
  refreshDiff` slots that `co_await` `AsyncRepo`, then emits `statusChanged`,
  which drives `ChangesView` to refresh its file lists and the active diff.

## Build

- QCoro via FetchContent (Qt6 backend). Link `QCoro6::Core` and `Qt6::Concurrent`.
  Coroutines already enabled (project is C++23).

## Testing (Catch2, Core only — no Qt)

- **DiffEngine**: synthetic repos → assert exact hunks / lines / origins.
- **GitRepo stage/unstage/discard**: file, single hunk, and line-subset cases;
  assert resulting `status()` flags round-trip correctly.
- **GitRepo commit**: author/committer pulled from git config; parent linkage;
  index cleared after commit.

## Plan Decomposition

Executed as two plan documents in sequence:

- **Plan 3a** — Core git ops (`diff`, `stage`/`unstage`/`discard`, `commit`) +
  `DiffEngine` + Catch2 tests. No Qt; fully unit-testable in isolation.
- **Plan 3b** — QCoro/`AsyncRepo` + async `DashboardModel` + Changes tab UI,
  built on the verified Core layer from 3a.
