# Product

What GitTide does, from the user's point of view: the concepts, the screens, the
flows, and what is in scope.

## The core idea

GitTide is a desktop git client built around a first-class **Project**. A Project
groups several repositories so you can switch between contexts fast and see them
together. This is the differentiator versus GitHub Desktop and similar clients,
which are organised one-repo-at-a-time.

- **Project** — a named grouping of repositories plus a little UI state (which
  repo was last active). Projects are the top-level unit the user navigates.
- **Repo** — a reference to a git repository on disk (by absolute path, with an
  optional display alias). A repo is only ever referenced, never copied.

The product promise is **speed**: the UI never blocks, repositories are worked in
parallel, and large histories/diffs render incrementally.

## Scope

**Shipped (MVP):**

- Projects: create, group repositories, switch the active project, persist state.
- Add a repository three ways: add an existing local repo, initialise a new one,
  or clone from a URL (with progress + cancel).
- Per-repo working state: status, **inline change selection** at **file, hunk,
  and line** granularity (checkboxes — no separate staging area), discard, and
  commit.
- Diff viewer (file / hunk / intra-line), shared between working changes and
  history.
- Per-repo commit **history with a graph** (branches, merges).
- **Multi-window** (hybrid): each window has the in-window project switcher, and
  any project can also be opened in a new top-level window. Window/session state
  persists for restore.
- **Submodules**: a repo's submodules appear as expandable children in the repo
  tree; opening one navigates into it as its own repo.

**Not yet (post-MVP):**

- Bulk **pull**-all across a project. (Bulk **fetch**-all is now designed — see
  [Fleet fetch](#fleet-fetch).)
- Branch **merge** and conflict-resolution UI. (Local branch *create / switch /
  delete / rename* is now designed — see [Branches](#branches).)
- An aggregated project-wide timeline graph (all repos on one axis).
- Remove / rename of projects or repos and repo reordering.

> New ideas for this list start as a [wish](../../wishlist/index.md), get
> designed into this spec, then realised by a [plan](../../plans/index.md).

## Screens & navigation

The window is GitHub-Desktop-like, with three zones left-to-right:

- **Project/repo sidebar (collapsible).** Top: the **project switcher** (a combo;
  its dropdown includes a "New project…" item). Below: the **repo tree** of the
  active project, with an add-repo toolbar (three buttons: add existing / init /
  clone) at the bottom. A toggle collapses the whole sidebar to a slim rail to
  reclaim width; expanding restores it. This zone is GitTide's multi-repo
  differentiator and is always present (collapsed or not).
- **List column.** A **branch bar** across the top shows the current branch and
  hosts branch actions (see [Branches](#branches)). Below it, two sub-tabs —
  **Changes** and **History** — select what the list shows and what feeds the
  diff panel.
- **Diff panel.** One shared **diff view** on the right, driven by whatever is
  selected in the list column: a working file (editable, with checkboxes) or a
  file from a historical commit (read-only).

When there is nothing to show, the main area shows a **branded empty state**
instead (see [design](../design/design.md)):

- No project exists → a centered **Create Project** call-to-action.
- A project with no repos → the three centered add-repo actions.

### Changes tab

Select changes and commit — **no separate staging area**. The list column shows
one list of changed files, each with a **checkbox**, defaulting to **checked**
(GitHub-Desktop style). Selecting a file shows its diff in the shared panel;
inside the diff, individual lines carry checkboxes too, so a file's checkbox is
tri-state (all / none / some lines checked). Where two or more changed lines
(added/removed, mixed origins allowed) sit consecutively with no context line
between them, a single **block checkbox** appears on its own bare row above the
run — a tri-state shortcut that checks or unchecks the whole change at once.
Toggling it sets every line in the block; toggling individual lines underneath
keeps the block box in sync (checked / unchecked / partial). Lone single-line
changes get no block row. Block rows appear only in the editable Changes diff,
never in read-only history. The commit message box and Commit
button are pinned at the **bottom of the list** (enabled when a message is
present and at least one change is checked).

Committing builds the commit from exactly the **checked** set — checked whole
files and checked lines within partially-checked files. Staging is an invisible
implementation detail (the git index is rebuilt from the checked set at commit
time), not a place files live. **Discard** is available from a right-click
context menu on a file or a line selection.

### History tab

Read history as a commit graph, then inspect any commit's diff in the same panel.
The History tab is implemented in QML. The list column shows a virtualized
**commit list** (graph lane column · initials avatar · summary · author · date)
where each row's graph column paints branch/merge lanes in a multi-colour lane
palette and marks the HEAD commit with a white dot. Selecting a row opens the
three-part **commit detail** flow: (1) the **changed-files list** of that commit
(read-only, no checkboxes) fills the detail pane; (2) picking a file shows its
**diff** read-only (no per-line checkboxes); and (3) a **Checkout** button detaches
HEAD at the selected commit. A 2px `accent` left border and a row-wide highlight
mark the selected history row.

### Syncing

Sync a branch with its remote without leaving GitTide. The branch bar shows
**ahead / behind** counts for the current branch's upstream. Four actions are
available: **Fetch** (download new commits, update remote-tracking refs),
**Pull** (fetch + reconcile), **Push** (upload local commits), and **Publish**
(push + set upstream for a branch that has none). Pull strategy — fast-forward-only
or rebase — is persisted in the repo's git config (`pull.rebase`) and toggled
from the branch bar. A rebase that hits conflicts aborts with an error; conflict
resolution is done via CLI for now. Authentication: SSH remotes use the system
ssh-agent; HTTPS remotes accept a personal access token entered in a
`CredentialDialog`. Tokens are kept in a session map (cleared on quit) — secure
keychain storage is deferred.

Remaining deferred: merge-strategy pull, rebase / merge conflict UI,
SSH keyfile + passphrase, multi-remote management, pull-all.

#### Fleet fetch

The multi-repo payoff: **fetch every repository in the active project at once**,
in parallel, without opening each one. One action on the project header fetches
the whole fleet; the sidebar then shows, per repo, a live spinner that resolves
to an outcome — **up-to-date**, **updated** (with a refreshed *behind* badge so
incoming commits are visible at a glance), or **failed** (glyph with the error on
hover). A repo flagged *missing* is skipped. One repo's failure never stops the
others; the project header shows an aggregate summary (e.g. "12 fetched, 1
failed"). Fetch only moves remote-tracking refs — no working tree changes — so it
is safe to run across the fleet while a repo is open.

Credentials follow the per-repo flow: the run uses the session's cached
credentials and ssh-agent; the first repo to hit an auth failure raises a single
`CredentialDialog`, and the entered token is cached and reused for the rest of the
fleet. First cut is **fetch only** — fleet pull-all stays deferred (it mutates
working trees and pulls in merge/rebase conflict handling).

### Branches

Work with a repo's **local** branches without dropping to a terminal. Scope now
covers local branches plus single-remote sync (see [Syncing](#syncing)); branch
merge and conflict-resolution UI are separate later wishes.

- **See.** A branch bar above the tabs names the current branch, or shows a
  `detached @ <short-oid>` state when `HEAD` is on a bare commit (paired with an
  icon, never colour alone).
- **Switch.** The branch bar's dropdown lists local branches; picking one checks
  it out. Checkout is **safe-by-default**: with a dirty working tree, GitTide
  stashes the changes, switches, then re-applies the stash onto the target so the
  work "follows" the user. If re-applying conflicts, it stops, keeps the stash,
  and reports — it never clobbers uncommitted work silently. (This uses git's
  stash internally; it is *not* the user-facing stash feature.)
- **Check out a remote branch.** The dropdown's **Remote** section lists
  remote-tracking refs (e.g. `origin/feature`). Picking one is DWIM, à la GitHub
  Desktop: if a local branch of the same trailing name already exists it is simply
  switched to; otherwise GitTide creates a local branch from the remote ref, sets
  its upstream to that ref (so ahead/behind and pull/push resolve immediately),
  and switches onto it — through the same safe-by-default stash path as a local
  switch.
- **Create.** From the branch bar (new branch from current `HEAD`) or from a
  commit's context menu in the History graph ("New branch from here"), optionally
  switching to it.
- **Checkout a commit.** The History graph's context menu can check out a bare
  commit, yielding a detached `HEAD`; switching to any branch re-attaches.
- **Delete / rename.** From the branch bar. Deleting the current branch is
  blocked; deleting an unmerged branch requires an explicit "delete anyway"
  confirmation. Names are validated before the operation.

The flow is per-repo: a successful switch/checkout refreshes that repo's status,
history, and branch list together (the same cascade as switching project, scoped
to one repo).

## Key flows

- **Switch project** → load its repos → redraw the repo tree. Only the active
  repo is fully loaded; others are lazy.
- **Select → commit** → status refreshes with every change checked by default →
  uncheck what to leave out (whole files, or individual lines in the diff) →
  write a message → commit, which rebuilds the index from the checked set and
  commits it (author/committer come from the repo's git config) → status and
  history refresh.
- **Inspect history** → History tab → pick a commit → its changed files list →
  pick a file → its diff shows read-only in the shared panel.
- **Add a repository** → pick one of the three modes; clone shows a determinate
  progress modal with a working Cancel.
- **Switch branch** → pick a branch (or "New branch from here" on a commit) →
  safe-checkout (stash + re-apply if dirty) → the repo's status, history, and
  branch bar refresh together.

## Windowing

The Qt Quick app runs as a **single window** today: one `QQmlApplicationEngine`
loading `Main.qml`, over process-wide shared services (the project registry, the
thread pool). Multi-window — multiple windows with **per-window** active-project
state over the same shared registry, plus session restore — is a planned
extension, not yet implemented in the QML shell.

## Data & persistence (what is stored, and where)

Two separate files, so window juggling never rewrites the project registry:

- **Project registry** (`projects.json`) — the source of truth for projects and
  their repos: ids, names, repo path+alias lists, and `activeProject` as a
  last-focused hint. Repos are references to disk; a deleted directory is marked
  "missing", never a crash.
- **Window session** (`session.json`) — which projects had windows open and their
  geometry, for restore on next launch.

Both live in the OS-appropriate config dir. The persistence mechanics (atomic
writes, path encoding, corrupt-file recovery) are an engineering concern — see
[`engineering`](../engineering/engineering.md).

## Submodules

A submodule **is** a git repo at a working-directory path. GitTide discovers them
live (never persisting them — git's state is the single source of truth) and
shows them as children under their parent repo in the tree, recursively to
arbitrary depth (expanded by default). Each submodule row carries at-a-glance
state: its pinned short commit OID and a clean / dirty / uninitialised indicator.
The parent repo shows a submodule as a single changed entry (a pointer move), not
its recursed internals.

Selecting an initialised submodule **opens it as a first-class repo**: its
working directory is a real git repo, so it becomes the active repo and reuses
the same Changes / History machinery (and lights up in the tree like any other
active repo). An uninitialised submodule has nothing on disk and is not openable.
Checkout actions on the parent's submodule pointer remain deferred.
