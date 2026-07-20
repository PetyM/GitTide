# Show local-only vs pushed commits in History

| | |
|--|--|
| **Added** | 2026-07-20 |
| **Status** | `shipped` |
| **Shipped** | 2026-07-20 |
| **Touches** | product (History/Graph cue), design (unpushed cue tokens), engineering (core `localOnlyOids` + refresh cascade) |

## What

In the History (and Graph) view, make it visible at a glance which commits are
still **local-only** — not yet on any remote — versus already **pushed**. Scanning
history, the user can immediately see "what haven't I shared yet".

## Why

The branch bar's ahead count says *how many* commits are unpushed, but not *which*
ones. When reordering, squashing, or deciding what to push, seeing the boundary
between shared and unshared history directly on the rows removes guesswork and the
risk of rewriting already-pushed commits.

## Notes

- A commit is **local-only** when it is reachable from HEAD but from no
  remote-tracking ref (`refs/remotes/*`). Computed in `core/` with a revwalk that
  pushes HEAD and hides every remote tip — cheap (O(ahead)) and it keeps the
  commit walk uncoupled from remote state.
- Never signal by colour alone (D19): the cue combines a shape/glyph (a trailing
  `↑` badge on the row + a hollow graph dot) with a dim treatment (pushed commits
  in `text.secondary`, local-only at full `text.primary`).
- Must refresh whenever pushed-ness changes: it rides the History refresh cascade
  and re-emits after fetch/pull/push.

---

Graduated:

- Designed in: [`spec/product`](../../spec/product/product.md#history-tab),
  [`spec/design`](../../spec/design/design.md#git-state-colours),
  [`spec/engineering`](../../spec/engineering/engineering.md#local-only-vs-pushed-commits).
- Plan: [`plans/2026-07-20-plan39-avatars-and-local-remote.md`](../../plans/2026-07-20-plan39-avatars-and-local-remote.md).
- Cue treatment + core computation → **D53** in [`decisions.md`](../../decisions.md).
