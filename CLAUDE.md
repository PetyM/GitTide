# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Language

The user may write to you in Czech, but **all of your output must be in English**.

## Read the spec first

GitTide's design lives in a **living specification** under [`docs/spec/`](docs/spec/spec.md).
Start at [`docs/spec/spec.md`](docs/spec/spec.md) before changing anything — it is
the source of truth for cross-cutting design (layering, invariants, the async
model, user flows, the visual system).

Division of documentation, and where to put what you learn:

- **Symbol-level facts** (what a class/function does, parameter meaning, a type's
  contract) → **Doxygen comments next to the code**. Never restate these in the
  spec.
- **Cross-cutting design** (a new flow, a changed layer boundary, a new
  invariant or design token) → update the relevant `docs/spec/` section in the
  same change.
- **How a feature was built**, task-by-task → a plan in
  [`docs/plans/`](docs/plans/index.md) (template at `docs/plans/TEMPLATE.md`).
- **How work flows** (wish → spec → plan → build) and what "done" means →
  [`docs/workflow.md`](docs/workflow.md).
- **Why** behind major choices and rejected alternatives →
  [`docs/decisions.md`](docs/decisions.md).
- If code and spec disagree, **code is ground truth** — fix the spec.

## Build & test

Configure / build / test commands — including how to run a single test — live in
[`README.md`](README.md#build--test). The essentials: `cmake -S . -B build`,
`cmake --build build`, `ctest --test-dir build --output-on-failure`. How tests are
structured (the `TempRepo` helper, the headless UI runner) and how to add one:
[`docs/spec/engineering/testing.md`](docs/spec/engineering/testing.md).

## Architecture in one breath

`app → ui → core → libgit2`, dependencies downward only. `core/` is pure C++23
(no Qt) — the git engine and JSON persistence; `ui/` is Qt Widgets + ViewModel
controllers + the `AsyncRepo` bridge; `app/` is process-wide composition
(`WindowManager`). Full detail: [`docs/spec/engineering/engineering.md`](docs/spec/engineering/engineering.md).

## Invariants you must not break

(Full statements in `docs/spec/engineering/engineering.md`.)

- **No Qt in `core/`.** It compiles and tests without Qt on the include path.
- **libgit2 and nlohmann/json are PRIVATE to `core/`** — no public header
  includes them (`GitRepo.hpp` only forward-declares `git_repository`).
- **Core speaks `std`** (`std::string` UTF-8, `std::filesystem::path`,
  `std::expected`); Qt types only at the ViewModel boundary.
- **Errors are values:** core returns `Expected<T>` = `std::expected<T, GitError>`;
  no exceptions across layers.
- **One owner per `GitRepo`** — move-only, not thread-safe; parallelism is
  per-worker repo instances, never a shared one.
- **Paths via `generic_u8string()`, never `.string()`** (Windows ANSI corrupts
  non-ASCII). Never build git command strings; use the libgit2 API.
- **Colour comes from a theme token**, never a hex literal in a widget.
- **TDD:** write the failing test first. New `ui/` sources → `ui/CMakeLists.txt`;
  new tests → the matching list in `tests/CMakeLists.txt`.
- **Code style:** follow [`docs/spec/engineering/code-style.md`](docs/spec/engineering/code-style.md)
  (KISS/DRY/SOLID/YAGNI; `m_` members; lowercase file names; Allman braces via
  `.clang-format`). It is authoritative for new code; **conform any existing file
  you touch** (clang-format + naming), and split a rename from content changes
  into two commits to preserve git history.
