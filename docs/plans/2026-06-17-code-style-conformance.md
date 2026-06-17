# Plan — Code-style conformance pass

| | |
|--|--|
| **Date** | 2026-06-17 |
| **Status** | `done` |
| **Spec** | [`spec/engineering/code-style.md`](../spec/engineering/code-style.md) |
| **Depends on** | all prior plans (whole-codebase pass) |

**Goal:** Bring the entire existing codebase into conformance with
[`code-style.md`](../spec/engineering/code-style.md) in one deliberate refactor —
superseding the earlier *opportunistic, when-touched* reconciliation policy — and
update the spec/docs to match the result.

**Architecture:** No behaviour change. Purely mechanical: file renames, member and
function renames, and formatting. Done in phases with a green build + full test
suite (`ctest`, 60 tests) between phases so each commit is independently sound.
History is preserved by keeping renames separate from content changes (rule
`(739)`).

**Tech stack:** `git mv`, `clang-format` (the repo `.clang-format`), targeted
`perl -i` rewrites.

## Global constraints

- No semantic change; build green and all 60 tests pass between phases.
- Preserve git history: `git mv` for renames; on the case-insensitive dev
  filesystem a case-only rename must go through a temporary name, else a later
  `git add -A` silently reverts it.
- Type names (`GitRepo`, `AsyncRepo`, …) stay PascalCase — only file names change.
- The test layer keeps descriptive snake_case (test case names + test-local
  helpers), per [`testing.md`](../spec/engineering/testing.md).

---

## Phase 1: Lowercase source file names `(743)`

`git mv` every PascalCase `.cpp`/`.hpp` under `core/`, `ui/`, `tests/support` to
lowercase (via temp name), bottom-up (core → ui → support) so each layer commit
builds. Updated all `#include` paths and the `CMakeLists.txt` source lists.

## Phase 2: Allman formatting `(701)`

Ran `clang-format` across `core/`, `ui/`, `app/`, then `tests/`. Sorting the UI
test runner's includes exposed a latent ordering dependency in
`test_main_window.cpp` (used `RepoListModel` complete via a transitive include);
fixed by making it self-sufficient `(941)`.

## Phase 3: `m_` members and camelCase functions `(743)`

Renamed trailing-underscore members → `m_camelCase` and snake_case
functions/methods → camelCase in production code and the `TempRepo` helper.
Also fixed a path invariant: a submodule display name used `.string()` →
`.generic_string()`.

## Phase 4: Spec/docs alignment

Rewrote `code-style.md` "Reconciliation" → "Conformance"; dropped the
"conform-when-touched / no repo-wide reformat" wording in `engineering.md` and
`CLAUDE.md`; lowercased all source-file references across the living docs.

---

## Outcome

- **Shipped:** the whole codebase conforms to `code-style.md` — lowercase files,
  `m_` members, camelCase functions, Allman braces. Build green, 60/60 tests pass.
- **Spec updated:** [`code-style.md`](../spec/engineering/code-style.md) now states
  conformance (with the test-layer exception and the case-only rename guidance);
  [`engineering.md`](../spec/engineering/engineering.md) and `CLAUDE.md` updated to
  match.
- **Code:** every `.cpp`/`.hpp` under `core/`, `ui/`, `app/`, `tests/` (renames +
  formatting + naming); no public behaviour changed.
