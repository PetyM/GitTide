# Repository sources in the sidebar — group nodes, and dialog polish

| | |
|--|--|
| **Date** | 2026-08-05 |
| **Follows** | [`2026-08-05-bulk-add-repos-design.md`](2026-08-05-bulk-add-repos-design.md) (shipped) |
| **Touches** | ui (repo list model, sidebar, add-from-folder dialog), core (depth default), design (source row, stepper) |

## Goal

A registered folder is currently invisible outside the Project Options dialog:
its repositories appear in the sidebar as an undifferentiated flat list, and the
folder itself appears nowhere. This change makes the source a **visible grouping
node** in the repository list, with its repositories nested under it.

Three smaller corrections ride along, all reported from real use:

1. The depth `SpinBox` in the add-from-folder dialog renders wrong — its
   `contentItem` and `background` are themed, but the stock Basic up/down
   indicators are not, and they overlap the custom background.
2. The default scan depth should be **1**, not 2.
3. Registering the folder as a source should be **on by default**, not opt-in.

## Decisions

| Question | Decision |
|--|--|
| What a group is | **A view concern only.** `projects.json` is untouched — repositories stay a flat list; the sidebar derives the grouping from each source's path. |
| Which group a repo lands in | The **deepest** source whose folder contains it, by the same directory-boundary rule `isUnder` uses, plus an exact self-match so a source that *is* a repository holds itself. |
| Repos under no source | Ordinary top-level rows, **after** the groups. A project with no sources looks exactly as it does today. |
| The source row | Collapsible; click toggles rather than opening anything. Right-click gives *Rescan now* / *Clear ignored* / *Remove source*. |
| Initial expansion | Source groups start **expanded**; repositories and submodules keep today's collapsed default. |
| Empty / missing source | Still shown. A source whose folder has vanished carries an "folder not found" badge, as in Project Options. |

Deriving the grouping rather than storing it is what keeps this change small:
removal, the per-source ignore list, `lastActiveRepo`, duplicate rejection and
the rescan pass all continue to operate on a flat repository list and need no
changes.

## 1. Model — group nodes

`RepoListModel::Node` gains a kind:

```cpp
enum class Kind
{
    Repo,   ///< a repository (or, as a child, a submodule)
    Source, ///< a registered folder; its children are the repos beneath it
};
```

and three roles: `IsSourceRole`, `RepoCountRole`, `AvailableRole`.

The builder takes both lists:

```cpp
/// Rebuild the top-level rows. Source folders become collapsible group nodes,
/// in store order, each holding the repositories that live beneath it;
/// repositories under no source follow as ordinary top-level rows. Does no git
/// I/O — see the existing note on hydration.
void setRepos(const std::vector<gittide::RepoRef>& repos,
              const std::vector<gittide::RepoSource>& sources = {});
```

The default argument keeps the existing call sites and tests compiling; passing
no sources yields exactly today's flat list.

Grouping rule, in order:

1. One group node per `RepoSource`, in store order, first among the roots.
   `AvailableRole` is false when the folder no longer exists (`std::error_code`
   overload — no throwing filesystem call).
2. Each repository is assigned to the **deepest** source whose folder contains
   it. "Contains" is the directory-boundary test (`/home/u/proj` does not
   contain `/home/u/projects/api`) **or** an exact path match, so a source
   registered on a folder that is itself a repository holds that repository
   rather than orphaning it.
3. Repositories matching no source become top-level rows, after the groups, in
   project order.

A group with no children is still emitted — seeing an empty registered folder is
the point of the feature.

## 2. The row-addressing invariant this breaks

Today every root row *is* a repository, so `m_roots[row]` and `repos[row]` are
the same thing, and two call sites rely on it:

- the poll pass — `setSyncCounts(row, …)` and `setRepoHead(row, …)`
  (`ui/src/projectcontroller.cpp:97,121`)
- the fleet fetch — `setFetchState(row, …)`, `setSyncCounts(row, …)`, and the
  display name read back via `data(index(row, 0))` (`:741,744,752,783,798`)

Insert group nodes and those indices diverge silently: one repository's spinner,
branch line and ahead/behind counts land on another's row. Nothing crashes and
no test currently fails — which is exactly why this is the load-bearing part of
the change.

**Every root-addressing call moves to path addressing.** `setSyncCountsByPath`
and `setRepoHeadByPath` already exist; this adds `setFetchStateByPath`, and the
fleet fetch takes its display name from the `RepoRef` it already holds instead of
reading it back out of the model. After this no caller addresses a root by
position, so the grouping cannot misroute a row — by construction, not by
convention.

The row-indexed setters that become unused are removed rather than left as
traps; the model tests that exercise them move to the by-path variants.

## 3. Sidebar

`Sidebar.qml`'s `TreeViewDelegate` branches on `model.isSource`:

- **Source row** — single line, 30px (the height an uninitialised submodule row
  already uses). Folder basename at 13px `theme.textPrimary`; repository count
  right-aligned in 11px `theme.textMuted`; full path on hover. When
  `available` is false the second slot reads *"folder not found"* in
  `theme.stateDeleted`, matching Project Options.
- **`activate()`** toggles expansion instead of calling `repoVm.open` — a source
  has nothing to open. Click and keyboard activation (Enter/Space) behave the
  same. The existing chevron and its `MouseArea` are unchanged.
- **Expansion** — expansion is a view state, so the sidebar drives it: on
  `modelReset` it expands every root row whose `isSource` is true. Repositories
  and submodules keep the current collapsed default.

New `ui/qml/SourceContextMenu.qml`, built like `RepoContextMenu.qml`:

```
Rescan now
Clear ignored
─────────────
Remove source        (destructive)
```

wired to `projectController.rescanSources()`, `clearIgnoredForSource(path)` and
`removeSource(path)` — all three already exist. The sidebar's right-click
handling picks this menu for a source row and the existing repo menu otherwise.

## 4. Add-from-folder dialog

**The stepper.** `up.indicator` and `down.indicator` get themed: bordered cells
carrying `−` and `+` in `theme.textSecondary`, `theme.accent` on hover, muted
when the control is disabled, sharing one 6px-radius `theme.surfaceBase` surface
with the value between them. The result is the control the rest of the dialog
already implies, instead of stock indicators painted over a custom background.

**Depth default 1.** The dialog opens at 1, and `ScanOptions::maxDepth` and
`RepoSource::maxDepth` become 1 to match — so a source loaded from a file
written before the key existed behaves like a freshly registered one, and there
is a single answer to "what is the default depth" across the codebase.

**Keep-as-source on by default.** `openDialog()` sets `keepSource.checked = true`
instead of clearing it. Two consequences, both wanted: the confirm button is
enabled as soon as a folder is chosen (source-only registration is now the
default path, not an edge case), and the deferred finding that source-only
registration completes with no visible feedback resolves itself — registering a
source now adds a visible row to the sidebar.

## Error handling

| Case | Behaviour |
|--|--|
| Source folder deleted | Group still shown, marked unavailable; its already-added repos keep their own rows beneath it and their own missing state |
| Two sources, one inside the other | The repository joins the deeper one; the shallower group simply does not list it |
| Source registered on a repository | That repository is the group's sole child |
| Repository removed from the project | Disappears from its group; the group remains, and the existing per-source ignore list keeps it from returning |
| Source removed | Group disappears; its repositories return to the top level (they were never owned by it) |

## Testing

**`tests/ui/test_repo_list_model.cpp`** — grouping by deepest source; ungrouped
repositories placed after the groups; an empty source still emitted; a source
that is itself a repository holding that repository; source order preserved;
`available` false for a missing folder.

**Addressing regression** — with a group node present, a path-addressed fetch or
poll update lands on the intended row and leaves its sibling untouched. This is
the test that fails today if the row-index calls are left in place.

**`tests/ui/test_project_controller.cpp`** — `refreshRepoModel` passes the active
project's sources to the model.

**QML** — a source row reports its repository count and toggles on activate
rather than opening a repository; the source context menu exists; the depth
control starts at 1; keep-as-source starts checked.

## Out of scope (YAGNI)

Persisting per-source expansion state; drag-and-drop between groups; nesting
groups inside groups; a per-source rescan (the controller rescans all sources of
the active project, and `rescanSources()` has no per-source variant); sorting or
filtering within a group.
