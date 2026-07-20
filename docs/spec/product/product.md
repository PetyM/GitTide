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
- Per-repo commit **History** list (virtualized; multi-select; drag-to-reorder/squash) and a dedicated **Graph tab** with the full all-refs git graph (local + remote branches + tags, with branch/tag chips).
- **Multi-window** (hybrid): each window has the in-window project switcher, and
  any project can also be opened in a new top-level window. Window/session state
  persists for restore.
- **Submodules**: a repo's submodules appear as expandable children in the repo
  tree; opening one navigates into it as its own repo.

**Not yet (post-MVP):**

- Bulk **pull**-all across a project. (Bulk **fetch**-all is now designed — see
  [Fleet fetch](#fleet-fetch).)
- Branch **merge** with conflict resolution is now designed — see
  [Merge](#merge). (Rebase, merge strategies, and octopus merges stay deferred.)
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
  hosts branch actions (see [Branches](#branches)). Below it, three sub-tabs —
  **Changes**, **History**, and **Graph** — select what the list shows and what
  feeds the diff panel.
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

Read HEAD-branch commit history and inspect any commit's diff in the same panel.
The list column shows a virtualized **commit list** (avatar · summary · author ·
date) — no graph lane column. Each row's **avatar** shows the author's real image
(Gravatar, resolved from the commit email) over an instant initials placeholder, so
a large history is scannable by face without rows ever jumping; loading avatars
from the network is a setting, on by default. **Local-only commits** — those not
yet on any remote — are marked so the user sees at a glance what hasn't been shared:
a trailing **`↑` badge** on the row, and pushed commits are **dimmed** while
local-only ones stay full strength (in the Graph tab the same commits get a
**hollow** dot). The cue updates live as commits are made, fetched, or pushed.
Selecting a row opens the three-part
**commit detail** flow: (1) the **changed-files list** of that commit (read-only,
no checkboxes) fills the detail pane; (2) picking a file shows its **diff**
read-only (no per-line checkboxes); and (3) a **Checkout** button detaches HEAD
at the selected commit. A 2px `accent` left border and a row-wide highlight mark
the selected history row. **Multi-select** (Shift-click for a contiguous range,
Ctrl-click to toggle) shows the **combined diff** of a contiguous range — the net
changes across those commits — in the same detail pane; the tip commit can be
**reworded** from its context menu. See [history-editing](history-editing.md).

**Drag-to-reorder / squash** operates on the reorderable run (contiguous
single-parent commits from HEAD): press-and-hold any row for 250 ms to arm the
drag (the whole row is the drag source — no grip icon needed); the three-band drop
zone (top / middle / bottom third) disambiguates reorder from squash. Selection
(single click, Shift-range, Ctrl-toggle) is handled by `TapHandler`s that
cooperate with the `DragHandler`, fixing the earlier `MouseArea` grab-steal bug.

### Graph tab

A read-only **full git graph** of all refs — local branches, remote-tracking
branches, and tags — rendered in the same lane-based `GraphColumn` painter used
by the old History graph, filling the **full pane width** (no inline commit
detail panel). Each commit row shows branch/tag name chips for every ref tip
that points at it. Selecting a row highlights it and updates the shared
selection state; **double-clicking a row switches to the History tab**, which
shows that commit's diff in its own commit-detail panel via the same shared
selection — if the commit isn't reachable from HEAD, History's own commit list
simply shows no highlighted row (the diff still displays correctly). A
right-click context menu offers the cross-branch-safe subset of actions: copy
SHA, new branch from here, checkout commit, merge. History-editing items
(Reword, Undo, Squash, Edit history) are hidden via the
`CommitContextMenu.allowHistoryEditing` property set to `false` in
`GraphPane.qml`. **Ctrl+3** switches to this tab; `repoVm.refreshGraph()` is
called on first switch and on live-refresh triggers while the tab is active. See
[context-menus](context-menus.md) and [keyboard-controls](keyboard-controls.md).

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
multi-remote management, pull-all. (SSH keyfile + passphrase and secure token
storage are in progress — see [Identity & credentials](#identity--credentials).)

### Identity & credentials

GitTide manages **who you commit as** and (progressively) **how you
authenticate**, from one place, so neither has to be configured on the command
line.

- **Named identities.** A user keeps a small catalogue of identities (a display
  name + email each) and assigns one at three levels, most specific winning:
  a **per-repo override**, a **per-project default** (applies to every repo in a
  project), and a **global default**. GitTide resolves the effective identity and
  **writes it into git config** — the global one into `~/.gitconfig`, a per-repo /
  per-project one into that repo's local `.git/config` — so every commit, merge,
  and rebase (and the CLI) sees it. This is applied when an assignment changes and
  when a repo becomes active. A local identity **you** set by hand (via the CLI)
  is recognised as such and **never overwritten** — GitTide only touches identity
  it owns (marked with a `gittide.identity` key). Managed from the **Options →
  Manage identities…** dialog: add identities, pick the global one, and set the
  open repo's override.
- **Secrets stay in the OS keychain.** HTTPS tokens and SSH-key passphrases live in
  the platform keychain (macOS Keychain / libsecret / Windows Credential Store) —
  **never** written to GitTide's own files. The non-secret metadata (identity
  names/emails, SSH key file paths, per-host accounts) lives in `credentials.json`;
  the secret is looked up from the keychain only when a network op needs it, so
  fetch/pull/push/clone authenticate without re-prompting across sessions. Entering
  a token in the auth prompt saves it to the keychain for that host. On a machine
  with no keyring, GitTide falls back to the per-session prompt.
- **Forge accounts.** Per-host accounts (github.com / gitlab.com / self-hosted)
  hold the login and a token used for HTTPS git auth; adding a token validates it
  against the host API to confirm it and pre-fill an identity.

All of the above is reachable from one **Credentials** dialog (Options → *Manage
identities…*): manage identities and their global / per-project / per-repo
assignment, add forge host accounts (token validated + saved to the keychain), and
register SSH keys (passphrase to the keychain). Remaining out of scope: forge
features beyond token validation (PRs/issues), and an SSH agent/keyfile picker
inside the sync auth prompt. See [network-sync](network-sync.md) and Plans 36–38.

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

### Merge

Integrate one **local** branch into the current branch from inside GitTide —
entirely local, no network. Scope is a single non-octopus merge: fast-forward
when possible, otherwise a real merge commit; resolve conflicts inline; commit or
abort. Merge strategies, rebase (see the separate rebase wish), and remote-branch
merge are out of scope.

- **Start.** *Merge into \<current\>* is offered two ways: as an item in the
  branch dropdown (per local branch) and from a commit/branch-tip context menu in
  the History graph. A dirty working tree is handled safe-by-default — GitTide
  auto-stashes before the merge and re-applies afterwards, the same
  never-clobber contract as checkout ([Branches](#branches)); a re-apply that
  would land on conflicts is deferred until the merge is committed, and a
  re-apply conflict stops and keeps the stash.
- **Outcomes.** *Up-to-date* → nothing to do. *Fast-forward* → HEAD advances, no
  merge commit. *Clean merge* → a merge commit is created automatically.
  *Conflicts* → the repo enters a **merge-in-progress** state and the user
  resolves (below).
- **Merge-in-progress is a first-class, self-evident state.** Whenever the repo
  is mid-merge a banner names the merge (*"Merging \<branch\> into \<current\>"*),
  lists the conflicted files, and always offers **Abort** plus **Commit merge**
  (enabled once nothing is unresolved). This state is read from the repository
  itself on every refresh — not remembered in the app — so it is shown correctly
  even after a restart or a merge begun from the CLI, and **Abort is always
  reachable**. GitTide must never leave the user in a merge it cannot describe or
  exit. **Abort** returns the repo to its exact pre-merge state.
- **Inline conflict resolution.** A conflicted file opens in the shared diff
  panel with its conflict regions shown inline (VS Code style): the *Current
  (ours)* and *Incoming (theirs)* sides are tinted and labelled, and each region
  carries **Accept Current / Accept Incoming / Accept Both** actions; the body
  stays freely editable for a hand-merge. A file counts as **resolved** when no
  conflict markers remain — detected from its content, not a manual "mark
  resolved" toggle — which clears its badge and, once every file is clean, enables
  Commit merge. The merge commit's message defaults to
  `Merge branch '<x>' into <current>` and is editable.
- **Submodule conflicts — deinit and retry.** A merge whose conflicts are in
  submodule pointers (gitlinks) is offered a distinct escape in the banner:
  **Deinit submodules & retry**. Accepting aborts the merge, de-initialises the
  conflicted submodules (emptying their working dirs so the gitlinks merge as
  plain pointer moves with no nested-repo mess), and re-runs the merge; the
  affected submodules are re-initialised and updated to their pinned commits once
  the merge concludes (commit or abort). This is reactive — a plain merge is
  tried first, and the offer appears only when submodule pointers actually
  conflict.
- **Refresh cascade.** Starting, committing, aborting, or retrying a merge
  refreshes status, diff, history, branches, and sync-status together — the same
  cascade as a checkout.

### Stash

Park working changes on git's native **stack** and bring them back later — fully
local, no network. This is the *user-facing* stash feature, distinct from the
invisible auto-stash that safe-switch / merge / rebase use internally.

- **The stack is visible.** A collapsible **Stashes** panel lives in the
  [Changes tab](#changes-tab)'s left column, below the working-file list. Its
  header reads *Stashes (n)* and collapses to nothing when the stack is empty.
  Each row names the stash — its message and the branch it was taken from
  (`stash@{0}`, `{1}`, …, newest first).
- **Save stays one-click.** *Stash all changes* (title-bar / Repository menu)
  keeps its current behaviour: no message prompt, untracked files included,
  leaving a clean tree. Entries show git's default message (*WIP on \<branch>*).
- **Per-entry actions.** Any entry — not only the newest — can be **Apply** (run
  it onto the tree, keep it on the stack), **Pop** (apply + drop), or **Drop**
  (remove without applying). A **Clear** action on the panel header empties the
  whole stack. *Pop latest stash* in the menu stays as the newest-entry shortcut.
- **Preview before applying.** Selecting a stash row shows its diff in the shared
  right-hand diff panel, read-only, headed *Preview: stash@{n}* — the same viewer
  used for history. Deselecting returns the panel to the live working-tree diff.
- **Apply/pop conflicts never lose the stash.** If applying or popping would land
  on conflicts against the current tree, the operation stops, reports via the
  error banner, and **keeps the stash on the stack** — first cut does not route
  into inline conflict resolution (D44).
- **Refresh cascade.** Stash save, apply, pop, drop, and clear all rewrite the
  working tree and/or the stack → status, diff, and the stash list refresh
  together, the same cascade as discard / checkout.

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
- **Merge** → pick "Merge into \<current\>" (branch dropdown or history context
  menu) → fast-forward / clean merge commit / or conflicts → resolve inline
  (Accept Current/Incoming/Both, or edit) → Commit merge, or Abort to the
  pre-merge state → status, history, and branches refresh together.

## Windowing

The Qt Quick app runs as a **single window** today: one `QQmlApplicationEngine`
loading `Main.qml`, over process-wide shared services (the project registry, the
thread pool). Multi-window — multiple windows with **per-window** active-project
state over the same shared registry, plus session restore — is a planned
extension, not yet implemented in the QML shell.

## Data & persistence (what is stored, and where)

Two separate files, so window juggling never rewrites the project registry:

- **Project registry** (`projects.json`) — the source of truth for projects and
  their repos: ids, names, repo path+alias lists, `activeProject` as a
  last-focused hint, and each project's `lastActiveRepo` — the repo (or submodule)
  the user had open, persisted on open and reopened on next launch — and revealed
  in the sidebar tree (its ancestors expanded so a restored submodule shows as the
  active row, not hidden under a collapsed parent). A stale path whose folder is
  gone is ignored, falling back to the first repo. Repos are
  references to disk; a deleted directory is marked "missing", never a crash.
- **Window session** (`session.json`) — which projects had windows open and their
  geometry, for restore on next launch.
- **Credentials metadata** (`credentials.json`) — named identities, per-host and
  SSH-key references, and the global/project/repo identity assignments.
  **Secrets are never stored here** — HTTPS tokens and SSH passphrases live only in
  the OS keychain, keyed by host/key id. A corrupt file is recovered the same way
  as the project registry (backed up to `.corrupt`, replaced with an empty store).

These live in the OS-appropriate config dir. The persistence mechanics (atomic
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

Submodules can be **initialised, updated (to the pinned commit), and
deinitialised** from the GUI without dropping to a terminal:

- An inline **Init** button appears on uninitialised (greyed) rows in the sidebar.
- Right-clicking a submodule row opens a **submodule context menu**: *Initialize /
  Update submodule* (label adapts to current status), *Update all submodules*,
  *Deinitialize submodule*.
- Right-clicking a top-level repo row offers **Update all submodules** — one
  level only; to go deeper, initialise a submodule node and invoke the action on
  it (each initialised submodule node gets its own greyed children with the same
  affordances).

Operations run on any repo in the sidebar via a transient handle in
`ProjectController` — not only the active repo — which preserves the
one-owner-per-`GitRepo` invariant. A busy spinner on the row prevents
double-triggering while an op is in flight.

The sidebar tree refreshes after every GUI op and on external change: the active
repo's git-dir watch (`gitDirRefreshed → repoStructureChanged → refreshSubmodules`)
drives an immediate refresh; the 5-second fleet poll covers non-active repos and
no-ops via `applySubmodules` when nothing has changed.

Still deferred: `git submodule update --remote` (advance the pin to the
submodule's upstream), adding or removing submodules (`.gitmodules` editing),
forced full-depth recursive update in a single click, and `git submodule sync`.
