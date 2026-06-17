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
- Per-repo working state: status, stage/unstage/discard at **file, hunk, and
  line** granularity, and commit.
- Diff viewer (file / hunk / intra-line).
- Per-repo commit **history with a graph** (branches, merges).
- Project **dashboard**: read-only aggregated status across the project's repos.
- **Multi-window** (hybrid): each window has the in-window project switcher, and
  any project can also be opened in a new top-level window. Window/session state
  persists for restore.
- **Submodules**: a repo's submodules appear as expandable children in the repo
  tree; opening one navigates into it as its own repo.

**Not yet (post-MVP):**

- Network operations beyond clone: push / pull / fetch, and bulk "fetch/pull
  all" across a project.
- Branch management: create / switch / delete, merge, conflict-resolution UI.
- An aggregated project-wide timeline graph (all repos on one axis).
- Remove / rename of projects or repos and repo reordering.

> New ideas for this list start as a [wish](../../wishlist/index.md), get
> designed into this spec, then realised by a [plan](../../plans/index.md).

## Screens & navigation

The window uses **Layout A** — a two-level sidebar, GitHub-Desktop-like:

- **Left sidebar.** Top: the **project switcher** (a combo; its dropdown includes
  a "New project…" item). Below: the **repo tree** of the active project, with an
  add-repo toolbar (three buttons: add existing / init / clone) at the bottom.
- **Main area.** Tabs: **Changes** and **History**.

When there is nothing to show, the main area shows a **branded empty state**
instead (see [design](../design/design.md)):

- No project exists → a centered **Create Project** call-to-action.
- A project with no repos → the three centered add-repo actions.

### Changes tab

Stage work and commit. Left: staged and unstaged file lists from the repo's
status. Right: the **diff view** for the selected file. Bottom: a commit message
box and Commit button (enabled when a message is present and something is
staged). Selecting a hunk or a line range in the diff stages / unstages /
discards exactly that selection — partial staging is a first-class flow.

### History tab

Read history as a commit graph. A table lists commits (graph · summary · author ·
date); the graph column paints the branch/merge lanes. Rendering is virtualized,
so a very large history scrolls smoothly.

### Dashboard

A read-only aggregated view of the active project: every repo's status computed
in parallel, off the UI thread, so adding a repo with a slow status never stalls
the others.

## Key flows

- **Switch project** → load its repos → redraw the repo tree → kick off the
  parallel dashboard status. Only the active repo is fully loaded; others are
  lazy.
- **Stage → commit** → stage a file/hunk/line → status refreshes → write a
  message → commit (author/committer come from the repo's git config) → history
  refreshes.
- **Add a repository** → pick one of the three modes; clone shows a determinate
  progress modal with a working Cancel.

## Multi-window

A process-wide `WindowManager` owns multiple windows over **shared** services
(the project registry, the thread pool). The "active project" is **per-window**
state, not a global — opening "in a new window" creates another window bound to a
project. Optionally, opening a project that is already open can raise the existing
window instead of creating a duplicate (a setting).

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
shows them as children under their parent repo in the tree. Selecting a submodule
opens it as a first-class repo, reusing the same Changes / History machinery. The
parent repo shows a submodule as a single changed entry (a pointer move), not its
recursed internals.
