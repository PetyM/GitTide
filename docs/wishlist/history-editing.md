# History editing — amend, squash, reword

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `idea` |
| **Touches** | product (history actions on commits), engineering (core: amend + commit-rewrite on `GitRepo`), design (amend affordance in Changes, history-rewrite actions + warnings) |

## What

Tidy up local history before it's shared. Building on the existing commit flow and
History graph:

- **Amend** the last commit — change its message and/or fold the currently staged
  changes into it (the classic "oops, one more file / fix the message").
- **Reword** a commit's message.
- **Squash / fixup** — combine a commit with its neighbour(s) into one, with a
  chosen or concatenated message.

Amend is the self-contained first step; reword/squash of older commits are a thin
layer over the [rebase](rebase.md) driver (interactive rebase is the general
engine behind squash/fixup/reword).

## Why

The first thing users reach for after committing is "fix that commit" — wrong
message, forgot a file, want to collapse three WIP commits into one before sharing.
Today GitTide can commit but not *edit* what it committed, forcing a terminal
trip. **Amend** in particular is high value and almost free (a single
`git_commit_amend`), and it pairs naturally with the existing Changes/commit UI.

## Notes

- **Amend is the cheap, independent slice — do it first.** `git_commit_amend`
  against `HEAD`: no rebase machinery needed. A checkbox/action in the Changes tab
  ("Amend last commit") that pre-fills the previous message and commits over `HEAD`.
  Ship this well before tackling squash of older commits.
- **Reword/squash of older commits = interactive rebase.** Editing anything but
  the tip means rewriting every descendant → it's [rebase](rebase.md) under the
  hood. Build amend now; gate reword/squash-of-older on the rebase driver +
  todo-list editor. This wish and the interactive part of `rebase.md` overlap —
  decide which owns the todo-list editor when both graduate (avoid duplicating it).
- **Layering.** `core/` on `GitRepo`: `git_commit_amend` for the tip; the rest via
  the rebase APIs. `Expected<T>`, no Qt in core.
- **Refresh cascade.** Amend/rewrite changes `HEAD` and history → status + history
  refresh.
- **Safety — rewriting shared history.** Any of these rewrites commit ids. Local
  is fine; once [network sync](network-sync.md) exists, warn loudly before
  rewriting already-pushed commits (they'd need a force-push). Keep an escape
  hatch — amend should be undoable via reflog conceptually; don't strand the user.
- **UI surface.** Amend lives in Changes (next to Commit). Reword/squash live on
  the History graph (context menu on a commit / a selected range), consistent with
  where [branch management](branch-management.md) hangs commit-context actions.
- **Submodules — handle with care (a known GitHub-Desktop pain point).** Rewriting
  history in a repo that has submodules is where clients get it wrong: GitHub
  Desktop is notorious for **clobbering submodule pointers** during amend/squash —
  silently moving a submodule to the wrong commit, staging spurious submodule
  pointer-move changes into the amended commit, or checking out submodule content
  the user never touched. A submodule shows in the parent as a single
  pointer-move entry (per GitTide's existing submodule model), and an amend that
  folds "all staged changes" must **not** drag in a submodule pointer the user
  didn't intend. The rule: a history edit must **preserve submodule pointers
  exactly** unless the user explicitly changed them, never auto-update or
  auto-checkout submodule content as a side effect, and never lose an intended
  pointer move. Worth an explicit test (a repo with a submodule, amend an unrelated
  file, assert the submodule entry is untouched) and likely a decisions.md note on
  the policy.
- **Scope creep to resist.** No full interactive-rebase todo editor in this wish's
  first cut — just amend + reword-tip. Squash/fixup of arbitrary ranges arrives
  with interactive rebase.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (amend in Changes, history-rewrite actions), spec/engineering (core amend + rewrite via rebase) · plan: plans/<file>
- Ownership of the interactive-rebase todo-list editor (here vs rebase.md) → log in decisions.md
-->
