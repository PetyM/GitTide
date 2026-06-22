# Stash management — stackable

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `idea` |
| **Touches** | product (stash actions + a stash list), engineering (core: stash save/list/apply/pop/drop on `GitRepo`), design (stash list UI, stash-and-switch affordance) |

## What

Set work aside without committing it, and bring it back later — the full
**stack**, not just one slot. Per repo:

- **Stash** the current working changes (optionally including untracked files,
  optionally with a message), leaving a clean tree.
- See the **list** of stashes (the stack: `stash@{0}`, `{1}`, …) with their
  messages and the branch they came from.
- **Apply** a stash (keep it on the stack) or **pop** it (apply + drop), **drop**
  a single stash, or **clear** all.
- Preview a stash's diff before applying (reuse the existing diff viewer).

"Stackable" is just git's native model: stash is a stack, and GitTide should
expose all of it, not a single hidden slot.

## Why

Stash is one of the most-used local git verbs — "I need to switch context *now*,
park this." It's small, entirely local (no network, no credentials), and high
value per line of code. It also directly enables a clean **stash-and-switch** flow
for [branch management](branch-management.md): the safest answer to "you have
uncommitted changes, can't checkout" is "stash them, switch, optionally restore."

## Notes

- **Layering.** Pure git → `core/` on `GitRepo` (libgit2 `git_stash_save`,
  `git_stash_foreach`, `git_stash_apply`, `git_stash_pop`, `git_stash_drop`),
  returning `Expected<T>`. No Qt; surfaced via ViewModel/`AsyncRepo`.
- **Refresh cascade.** Stash and pop/apply both rewrite the working tree → status
  + diff must refresh, same cascade as discard/checkout.
- **Apply conflicts.** `stash apply`/`pop` can conflict against the current tree.
  Decide first-cut behaviour: surface the conflict (depends on
  [merge](shipped/merge.md)/conflict UI) or just report it and leave the user in the
  conflicted state. At minimum, never silently lose the stash on a failed pop.
- **Untracked / index handling.** Expose the common toggles: include untracked
  (`--include-untracked`), keep index. Don't over-build — a checkbox or two.
- **UI surface — open question.** A stash list where? A panel/section in the
  Changes tab, or a menu off the repo header. Keep it close to where the user
  sees their working changes.
- **Multi-repo angle (later).** "Stash all dirty repos in the project" is a
  natural Project-level follow-on; out of scope for the first cut (YAGNI).
- **Scope creep to resist.** No stash *branch* (`git stash branch`), no partial/
  interactive stash in the first cut. Save / list / apply / pop / drop / clear is
  plenty.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (stash actions + list), spec/engineering (core stash ops on GitRepo, refresh cascade) · plan: plans/<file>
-->
