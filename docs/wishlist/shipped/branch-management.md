# Branch management — create / switch / delete

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `done` |
| **Touches** | product (a branch picker + branch actions), engineering (core: branch list/create/checkout/delete on `GitRepo`), design (branch picker UI, checkout-safety affordances) |

## What

Let the user work with a repo's **local branches** from inside GitTide, without
dropping to a terminal. Concretely, per repo:

- **See** the current branch and the list of local branches (the History graph
  already paints lanes; this adds a branch *picker* and an explicit "you are on
  `X`" indicator).
- **Switch** (checkout) to another local branch.
- **Create** a new branch from the current `HEAD` (or from a selected commit in
  the History graph) and optionally switch to it.
- **Delete** a local branch (with the usual "not fully merged" guard).
- **Rename** a local branch.

Scope is deliberately **local-only**: no fetch/push, no tracking-branch setup, no
merge. Remote branches, upstream tracking, and merge/rebase are their own later
wishes that build on this.

## Why

History is read-only today: the user can *see* the branch/merge graph but can't
*act* on it — switching branches, the single most common git verb after
commit, forces them out to a shell. Branch create/switch/delete is the smallest
useful step from "GitTide shows my repo" toward "GitTide drives my repo," it's
already named in the product spec's post-MVP list, and it's **entirely local** —
no network tier, no credentials, no transport — so it fits the current
no-SSH/HTTPS milestone (Decision D13) and carries low risk. It also unblocks the
features that need a branch to exist first: merge, conflict resolution, and
eventually push.

## Notes

- **Layering.** Branch enumeration and mutation are pure git operations →
  **`core/`** on `GitRepo`, libgit2 `git_branch_*` / `git_checkout_tree` +
  `git_repository_set_head`, returning `Expected<T>` like the rest of core. No Qt;
  surfaced through the ViewModel / `AsyncRepo` bridge. Honour the invariants:
  paths via `generic_u8string()`, never build git command strings.
- **Checkout safety — needs design.** What happens to uncommitted work on
  checkout? libgit2's default checkout will refuse when local changes would be
  overwritten; GitTide must **never clobber user work silently**. Decide the
  policy: block with a clear message, offer stash-and-switch (depends on a future
  stash wish), or only allow checkout when the tree is clean. Mirror the
  spirit of discard's existing guards.
- **Async.** Checkout can touch many files (working-tree rewrite) → it's a
  potentially slow, cancellable operation off the UI thread, like clone. Switching
  branch invalidates status + history + diff → it triggers the same refresh
  cascade as "switch project," scoped to the one repo.
- **UI surface — open question.** Where does the branch picker live? A combo/menu
  in the repo header (GitHub-Desktop puts a "Current Branch" dropdown in the top
  bar), versus actions hung off the History graph's context menu (create branch
  *here*, checkout *this*). Likely both: a header picker for switch/create, and a
  commit-context "New branch from here." Design in `spec/design`.
- **Multi-repo angle.** GitTide's differentiator is the Project. A natural
  follow-on (not this wish) is *bulk* branch ops — "what branch is each repo on,"
  "switch all repos to `main`" — which the Dashboard could surface. Resist for the
  first cut (YAGNI); land single-repo branch ops first.
- **Detached HEAD & edge cases.** Checking out a commit (not a branch) from the
  graph yields detached HEAD — decide whether to support it or always require a
  branch. Deleting the current branch, deleting an unmerged branch, and name
  validation (`git_branch_name_is_valid`) all need handling.
- **Scope creep to resist.** Remote/tracking branches, merge, rebase, and
  branch-from-remote are explicitly *out* — separate wishes. Keep the first cut to
  local list + create + switch + delete + rename.

---

**Graduated 2026-06-18.**

- Designed in: [`spec/product` §Branches](../../spec/product/product.md#branches)
  (branch bar + actions, safe-switch flow),
  [`spec/engineering` §Branch operations](../../spec/engineering/engineering.md#branch-operations--the-refresh-cascade)
  (core branch ops on `GitRepo`, safe-switch invariant, refresh cascade),
  [`spec/design`](../../spec/design/design.md#components) (branch bar + dialogs +
  detached affordance).
- Checkout-safety policy resolved as **stash-and-switch** → [D21](../../decisions.md).
- Realised by: [Plan 8 — Branch management](../../plans/2026-06-18-plan8-branch-management.md).
