# QML Plan 5 — Submodule Tree in Sidebar — Design Spec

**Date:** 2026-06-19
**Status:** Design approved, pre-implementation
**Topic:** Render git submodules recursively (≥3 levels) in the QML sidebar repo
tree, with a pinned short OID and a clean/dirty/uninitialized status dot per
submodule — realising §3.3 of the
[QML UI migration design](2026-06-19-qml-ui-migration-design.md).

> Future-state design. It does **not** overwrite the current
> [`docs/spec/design/design.md`](../../spec/design/design.md) (code is ground
> truth); the living spec is updated as part of the build.

---

## 1. Motivation

Migration design §3.3 specifies that submodules render in the repo tree at the
same row size as repositories, distinguished by indentation + a vertical guide
rail + elbow connector, the `❖` glyph in accent (~70% alpha), a short pinned
OID, and a status dot (green = clean, amber = dirty), at **arbitrary nesting
depth (≥3 levels shown)**, with top-level repos separated by a divider.

Current state falls short of all of that:

- `core/` exposes only `GitRepo::submodules()` — a **flat, direct-only** list of
  paths, with **no OID and no status**, and **no tests** (one caller).
- `RepoListModel` is **hard-capped at depth 1** (`Row` → `SubRow`; the code
  comments "Depth is max 1" and child-of-child returns `{}`), opens each repo
  inline in `setRepos`, and exposes only `repoPath` + `missing`.
- `Sidebar.qml` renders every node identically (no submodule styling, glyph,
  OID, status dot, guide rail, elbow, or divider).

## 2. Architecture impact

Layering is preserved: **core owns the git engine**, the model is a dumb mapper,
QML is presentation. This *fixes an existing smell* — today the `ui/` model
opens `GitRepo` instances and walks submodules itself; that git logic moves back
into `core/`.

| Layer | Change |
|-------|--------|
| `core/` | **New** `SubmoduleStatus` enum, `SubmoduleNode` struct, `GitRepo::submoduleTree()` (recursive, with OID + status). **Retire** flat `submodules()`. |
| `ui/` `RepoListModel` | Replaced internal shape: `Row`/`SubRow` → one recursive `Node`; arbitrary-depth `index`/`parent`; new roles. Maps `submoduleTree()` output. |
| `ui/qml/Sidebar.qml` | Delegate branches on `isSubmodule`; adds glyph/OID/status-dot/guide-rail/elbow/divider per the mockup. |
| `tests/support/TempRepo` | **New** `addSubmodule()` helper (first submodule test infra in the repo). |

No change to `core/` invariants: no Qt in core, libgit2/json stay PRIVATE,
`Expected<T>` errors-as-values, paths via `generic_u8string()`, one owner per
`GitRepo`.

## 3. Core API

```cpp
namespace gittide {

enum class SubmoduleStatus
{
    Clean,          // initialised, working tree matches pinned commit, no local changes
    Dirty,          // initialised, but modified (working-tree change or checked-out ≠ pinned)
    Uninitialized,  // listed in .gitmodules but not checked out (no working dir)
};

struct SubmoduleNode
{
    std::filesystem::path      path;      // absolute working-dir path
    std::string                name;      // .gitmodules name (UTF-8)
    std::string                shortOid;  // pinned gitlink commit, 7 hex chars; "" if Uninitialized
    SubmoduleStatus            status;
    std::vector<SubmoduleNode> children;  // recursive; empty if Uninitialized or leaf
};

// Recursively enumerates submodules of this repo (depth-first), opening each
// initialised submodule as its own repository to descend. Uninitialised
// submodules are reported without children.
Expected<std::vector<SubmoduleNode>> submoduleTree() const;

} // namespace gittide
```

**Per-node derivation (direct children, via `git_submodule_foreach`):**

- `name` ← `git_submodule_name`; `path` ← `workdir() / fromGitPath(git_submodule_path)`.
- `shortOid` ← first 7 hex of `git_submodule_head_id` (the commit the
  superproject pins). Empty when the pin is unavailable (uninitialised).
- `status` ← `git_submodule_status(...)`:
  - no `GIT_SUBMODULE_STATUS_IN_WD` bit ⇒ `Uninitialized`.
  - else if `GIT_SUBMODULE_STATUS_IS_WD_DIRTY(status)` (working-tree dirty:
    `WD_INDEX_MODIFIED | WD_WD_MODIFIED | WD_UNTRACKED`) **or** the index/HEAD
    differs from the pin (`INDEX_MODIFIED | WD_MODIFIED` — checked-out ≠ pinned)
    ⇒ `Dirty`.
  - else ⇒ `Clean`.
- **Recursion:** if `status != Uninitialized`, `GitRepo::open(path)` and append
  its `submoduleTree()` as `children`. Sequential — preserves one-owner-per-repo;
  child repos are opened and closed within the call. An open/walk failure on a
  child degrades that node to no children (it is not fatal to the whole tree).

`submodules()` is removed (no tests, single caller migrated to `submoduleTree()`).

**Threading:** `submoduleTree()` is synchronous, called from `setRepos` exactly
as the current inline walk is. Deep trees with many submodules could block the
UI thread; moving the walk onto `AsyncRepo` is **deferred** (noted in §6) and not
required for parity with current behaviour.

## 4. UI model — `RepoListModel`

Replace the two-struct shape with one recursive node:

```cpp
struct Node
{
    QString                            displayName;
    QString                            path;
    bool                               isSubmodule;
    bool                               missing;       // path does not exist on disk
    QString                            shortOid;       // submodules only
    SubmoduleStatus                    status;         // submodules only
    Node*                              parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
};
std::vector<std::unique_ptr<Node>> m_roots;
```

- **Stable storage:** `unique_ptr` nodes give stable addresses, so
  `createIndex(row, 0, node)` can stash `Node*` in `internalPointer()`. `parent()`
  returns the parent node's index via `node->parent`. This replaces the
  depth-1-only `pr + 1` integer scheme.
- **`setRepos`:** for each top-level repo, create a root `Node` (existing
  alias/path/missing logic), then if present `GitRepo::open` it and map
  `submoduleTree()` into child `Node`s recursively.
- **Roles** (`roleNames`): keep `repoPath`, `missing`; **add** `isSubmodule`
  (bool), `shortOid` (string), `status` (int — 0 Clean / 1 Dirty / 2
  Uninitialized). `Qt::DisplayRole` stays the display name.

## 5. QML — `Sidebar.qml`

Delegate (`TreeViewDelegate`, height 34) branches on `model.isSubmodule`:

- **Repository row** — `◧` glyph (`textSecondary`, → `accent` when current),
  name + path subtext, `⚠` (`stateModified`) when `model.missing`. Largely the
  current row.
- **Submodule row** — `❖` glyph (`accent`, opacity 0.7); name (elided, no path);
  mono **short OID** (`textMuted`, 11px) — hidden when `status === Uninitialized`;
  **status dot** 7px:
  - dirty → `stateModified` (amber), full alpha.
  - clean → `stateAdded` (green), opacity 0.55.
  - uninitialized → row dimmed (`textMuted`), dot hidden or dimmed.
- **Guide rail + elbow:** a 1px `border`-colored vertical rail per submodule
  depth and a horizontal elbow at the row's vertical mid. `TreeView` indentation
  supplies the x-offset; rails/elbow drawn in the delegate decoration/background.
- **Divider:** a 1px `border`-colored separator (`opacity 0.5`) between
  top-level repositories (the mockup's `.reposep`).
- Submodule subtrees **expanded by default** so nesting is visible on load.

All colours come from `theme` tokens; the only literal is the 0.7 / 0.55 alpha.
Visual reference: `mockups/gittide-qml-mainwindow.html` (`.repo`, `.sub`,
`.subtree`, `.reposep`).

## 6. Testing

- **`TempRepo::addSubmodule(name, childRepoPath)`** — new helper: libgit2
  `git_submodule_add_setup` → clone → `git_submodule_add_finalize`, then commit
  the gitlink + `.gitmodules`. First submodule test infrastructure in the repo;
  enables nesting by adding a submodule to a child repo.
- **Core test** (`test_submodule_tree` or extend an existing `GitRepo` test):
  build a 3-level nested tree; assert depth, `name`, `shortOid` (7 chars,
  matches pinned), and `status` for a `Clean`, a `Dirty`, and an `Uninitialized`
  node.
- **UI test** (extend `RepoListModel` tests): assert recursive `rowCount`/`index`
  past depth 1 and the new roles (`isSubmodule`, `shortOid`, `status`).
- **QML smoke:** `Sidebar`/`Main` loads with a submodule-bearing model without
  warnings (extend the existing shell test).

## 7. Deferred / open

- **Async** `submoduleTree()` on `AsyncRepo` (current behaviour is synchronous).
- **Submodule actions** (init / update / sync) from the tree — separate plan.
- **Live HEAD-vs-pinned drift** beyond the binary dirty flag.
- Status refresh on external change (file-watch) — out of scope; refresh is
  driven by the existing `setRepos` reload path.
