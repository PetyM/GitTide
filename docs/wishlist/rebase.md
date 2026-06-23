# Rebase

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `idea` |
| **Touches** | product (rebase action + in-progress flow), engineering (core: rebase driver + conflict state on `GitRepo`), design (rebase-in-progress UI, per-step conflict resolution) |

## What

Replay the current branch's commits onto a new base. Per repo:

- **Rebase** the current branch onto a selected branch or commit (the plain,
  non-interactive case first): linearise history instead of a merge commit.
- Drive the rebase **step by step** — when a step conflicts, resolve it (reusing
  the merge/conflict UI), then **continue**; or **skip** a step; or **abort** to
  return to the pre-rebase state.
- A clear **rebase-in-progress** indicator (which step of how many).

A later iteration adds **interactive rebase** (reorder / squash / fixup / drop /
reword across a range) — see also [history editing](history-editing.md), which
shares the squash/fixup mechanics.

## Why

Rebase is how many users prefer to keep history linear and clean before sharing
work. It's local, and it reuses the conflict-resolution UI built for
[merge](shipped/merge.md), so the incremental cost after merge is mostly the
**stepwise driver** (continue/skip/abort) and its in-progress state. Offering a
safe, visual rebase — with abort always available — removes one of the scariest
terminal operations from the user's day.

## Notes

- **Depends on merge/conflict UI.** Per-step conflicts are resolved with the same
  machinery as [merge](shipped/merge.md); build that first. This wish is largely the
  *driver* around it.
- **Layering.** `core/` on `GitRepo`: libgit2 `git_rebase_init` /
  `git_rebase_next` / `git_rebase_commit` / `git_rebase_finish` /
  `git_rebase_abort`, surfacing each operation and its conflict state as
  `Expected<T>` values. The step state machine is core; the UI reflects it.
- **In-progress state is first-class.** A rebase spans many commits and can pause
  on each. The repo is in a special state (`rebase-merge/` dir); status, actions,
  and the commit box all change. The UI must show "rebasing step k/n" and offer
  **continue / skip / abort** at every pause without losing work.
- **Interactive rebase is a big, separate iteration.** Reorder/squash/fixup/drop/
  reword needs a todo-list editor UI and careful UX. Keep the **first cut to plain
  rebase onto a target**; interactive (and its overlap with
  [history editing](history-editing.md)) is a follow-on. Resist building the
  interactive editor now (YAGNI).
- **Safety.** Rebasing rewrites commits → only ever the *local* branch, dirty-tree
  guard like checkout, abort always reachable, and (once [network
  sync](network-sync.md) exists) a loud warning before rebasing already-pushed
  commits. Never force the user into a state they can't back out of.
- **Refresh cascade.** Each step rewrites tree/index/history → refresh like
  checkout/merge, per step.
- **Scope creep to resist.** No `--onto` gymnastics, no autosquash, no rebase of
  remote branches in the first cut. Plain rebase + continue/skip/abort first.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (rebase action + in-progress flow), spec/engineering (core rebase driver + step/conflict state) · plan: plans/<file>
- Interactive-rebase deferral and its overlap with history-editing → note in decisions.md if it forks the design
-->
