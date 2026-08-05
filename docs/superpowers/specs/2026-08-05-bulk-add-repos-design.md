# Bulk-add repositories — folder scan and repository sources

| | |
|--|--|
| **Date** | 2026-08-05 |
| **Realises** | [`wishlist/bulk-add-projects.md`](../../wishlist/bulk-add-projects.md) |
| **Touches** | core (folder scan, source model + persistence), ui (scan ViewModel, add-from-folder dialog, Project Options), design (dialog + list styling) |

## Goal

Add many existing repositories in one action, and keep a folder registered as a
**repository source** so repos that appear in it later join the project on their
own.

Two user needs, one flow:

1. *"Add everything under `~/projects`."* — one-shot scan and pick.
2. *"`~/projects` is where my repos live; keep it in sync."* — the same scan,
   plus the folder is remembered and rescanned.

The manifest-driven [`repo` tool](../../wishlist/repo-manifest-tool.md) stays a
separate wish. A source is a *discovery* mechanism over local disk; a manifest is
a *declaration* that also clones. They do not overlap and this design does not
pre-empt one for the other.

## Decisions

| Question | Decision |
|--|--|
| New repo found by a source | **Auto-added.** A repo the user removes from the project is recorded per-source as ignored, so a rescan never re-adds it. |
| Rescan trigger | **Project activation** (app start / project switch) **plus a manual action.** No filesystem watcher. |
| Scan depth | **Per-source `maxDepth`, default 2.** Descent stops at a repo. |
| Entry point | **One dialog with a "keep as source" toggle.** Unticked = one-shot import. |
| Target project | The active project, as single-add today. |

## Architecture

```
core/reposcan.hpp        scanForRepos(root, {maxDepth}) -> Expected<vector<string>>
core/projectstore.hpp    RepoSource; Project::sources; add/remove/ignore/clear
        │
ui/projectcontroller     scanFolder(), addRepos(), rescanSources()   (QtConcurrent off-thread)
        │
ui/qml/AddFromFolderDialog.qml       pick → scan → checklist → add [+ register]
ui/qml/ProjectOptionsDialog.qml      Sources section: rescan / clear ignored / remove
```

Dependencies stay downward. `core/` gains no Qt; the scan is `std::filesystem` +
libgit2 validation only.

## 1. Core — folder scan

New `core/include/gittide/reposcan.hpp` / `core/src/reposcan.cpp`:

```cpp
namespace gittide {

/// Tuning for scanForRepos.
struct ScanOptions
{
    int maxDepth = 2; ///< directory levels below root to search; 1 = direct children
};

/// Find the git repositories under `root`.
///
/// `maxDepth` counts directory levels below `root` (1 = its immediate
/// subdirectories). Descent stops at a repository — a repository's interior is
/// never searched, so submodules and nested repos are not swept up alongside
/// their parent. Directories whose name starts with '.' are skipped, as are
/// directories that cannot be read (a permission error is not a scan failure).
///
/// @returns repository paths as generic UTF-8, sorted and deduplicated; an
/// empty vector when the tree holds no repositories. Fails only when `root`
/// does not exist or is not a directory.
Expected<std::vector<std::string>> scanForRepos(const std::filesystem::path& root, ScanOptions opt = {});

} // namespace gittide
```

Validation reuses `GitRepo::open`, which is backed by `git_repository_open`
(no upward discovery), so an ordinary subdirectory cannot masquerade as its
parent repository. Bare repositories and worktrees validate as a side effect.
Paths are produced with `generic_u8string()`.

## 2. Core — repository sources

`core/include/gittide/projectstore.hpp`:

```cpp
/// A folder that is rescanned for repositories to add to a project.
struct RepoSource
{
    std::string path;                 ///< absolute, generic UTF-8
    int maxDepth = 2;                 ///< see ScanOptions::maxDepth
    std::vector<std::string> ignored; ///< repo paths this source must never add again
};

struct Project
{
    // …existing fields…
    std::vector<RepoSource> sources;
};
```

Store API (each mechanical; callers still call `save()`):

- `Expected<void> addSource(const std::string& projectId, RepoSource src)` —
  errors on unknown project or a duplicate source path.
- `Expected<void> removeSource(const std::string& projectId, const std::string& path)` —
  repositories the source already added **stay** in the project.
- `void ignoreInSources(const std::string& projectId, const std::string& repoPath)` —
  appends `repoPath` to `ignored` of every source whose path is a directory
  prefix of it. No-op when no source contains the path.
- `Expected<void> clearIgnored(const std::string& projectId, const std::string& sourcePath)`.

`removeRepo` stays mechanical; the ViewModel calls `ignoreInSources` after it.

### Persistence

`projects.json` gains a per-project `"sources"` array:

```json
{
  "id": "…", "name": "Work", "repos": [ … ],
  "sources": [ { "path": "/home/u/projects", "maxDepth": 2, "ignored": ["/home/u/projects/scratch"] } ]
}
```

The key is additive, so **`kVersion` stays 1**. `from_json` treats a missing
`"sources"` as empty and skips malformed entries the way it already skips
malformed repos — an existing file loads unchanged.

## 3. ViewModel — `ProjectController`

```cpp
/// Scan `path` off-thread; results arrive on scanFinished / scanFailed.
Q_INVOKABLE QCoro::Task<void> scanFolder(QString path, int maxDepth);

/// Add `paths` in one batch. When `sourcePath` is non-empty the folder is also
/// registered as a source with `maxDepth`, seeded with `unchecked` as ignored.
Q_INVOKABLE void addRepos(const QStringList& paths, const QStringList& unchecked,
                          const QString& sourcePath, int maxDepth);

/// Rescan every source of the active project and add what is new.
Q_INVOKABLE QCoro::Task<void> rescanSources();

signals:
    /// One entry per candidate: { path, name, alreadyAdded }.
    void scanFinished(const QVariantList& candidates);
    void scanFailed(const QString& message);
    /// `failures` holds one "name: message" line per repo that failed to add.
    void reposAdded(int added, const QStringList& failures);
    void sourcesRescanned(int added, int unavailableSources);
```

Scans run under `co_await QtConcurrent::run(...)`, the pattern already used at
`ui/src/projectcontroller.cpp:315` for non-`AsyncRepo` core work.

**Batch add.** Validate and `addRepo` each path, collecting failures; a failure
never aborts the batch. Then **one** `saveStore()` and **one**
`refreshRepoModel()` + `hydrateRepoModel()` for the whole batch, then
`reposAdded(added, failures)`. With no active project the batch is refused with
the existing "No active project" guard.

**Registration.** A non-empty `sourcePath` adds the source with
`ignored = unchecked` — unticking a repo at registration means *never add this*,
not merely *skip it now*.

**Rescan.** `activate()` kicks `rescanSources()`; the Project Options action calls
it directly. Per source: scan, drop paths already in the project or listed in
`ignored`, add the remainder. One save and one model refresh per pass, not per
source. A source whose folder is gone counts toward `unavailableSources` and is
**never auto-removed**.

**Removal feeds the ignore list.** After `removeRepo` succeeds, the controller
calls `ignoreInSources` and saves. This is what makes auto-add safe: removal is
permanent.

A repository that disappears from disk keeps today's *missing* row. A source
never deletes rows.

## 4. UI and visual style

Every new surface is built from the existing primitives, so it is
indistinguishable from the dialogs already shipped — no bespoke chrome, no hex
literals, colour only from `theme` tokens ([design](../../spec/design/design.md)).

### `AddFromFolderDialog.qml` (new)

- `AppDialog` root — themed header with title *"Add repositories from folder"*,
  `OverlayCard` background, `padding: 20`, `width: 520`, `objectName:
  "addFromFolderDialog"`. Content in a `DialogColumn` (`spacing: 12`), footer in
  `DialogButtons`.
- **Folder row** — muted 11px `Label` caption over a `RowLayout` of an eliding
  path `Label` (`theme.textPrimary`, or `theme.textMuted` + "No folder chosen")
  and an `AppButton { variant: "secondary"; text: "Choose…" }` opening a
  `FolderDialog`, exactly as `CloneRepoDialog.qml` does for its destination.
- **Depth** — `SpinBox` (1–5, default 2) with the caption *"Scan depth"* and the
  same `Rectangle` background treatment as the dialog's text fields
  (`radius: 6`, `theme.surfaceBase`, `theme.border`, `theme.accent` on focus).
- **Result list** — `ListView` with `Layout.preferredHeight: 240`, `clip: true`
  and an `AppScrollBar`, following `BranchPickerDialog.qml`. Delegate is an
  `ItemDelegate` with a rounded (`radius: 4`) background — `theme.surfaceRaised`
  on hover, transparent otherwise — holding an `AppCheckBox`, the repo folder
  name at 13px `theme.textPrimary`, and the full path beneath at 11px
  `theme.textMuted`, `elide: Text.ElideMiddle`. An already-added row is
  `enabled: false` with its text at `theme.textMuted` and the trailing hint
  *"already added"*.
- **Header row above the list** — count (*"7 repositories found"*) plus
  *Select all* / *Select none* as `variant: "secondary"` `AppButton`s.
- **Source toggle** — `AppCheckBox` + 12px `Label`: *"Keep this folder as a
  source — add new repositories automatically"*.
- **States** — while scanning, the list area shows a centred 12px
  `theme.textMuted` *"Scanning…"* and the confirm button is disabled. An empty
  result replaces the list with *"No git repositories found in `<folder>`"* in
  the same muted style rather than an empty box.
- **Footer** — `DialogButtons` with `AppButton { variant: "secondary"; text:
  "Cancel" }` and `AppButton { variant: "primary"; text: "Add"; objectName:
  "addFromFolderConfirm" }`, enabled only when at least one row is checked.

### Entry points

A new item *"Add repositories from folder…"* sits beside today's *"Add Existing
Repository"* in `EmptyState.qml`, the sidebar menu and the app menu — same button
variants and ordering conventions, no new visual weight.

### `ProjectOptionsDialog.qml` — Sources section

A section reusing the dialog's existing heading style, listing one row per
source: the path (13px `theme.textPrimary`, `ElideMiddle`), a subline at 11px
`theme.textMuted` reading *"depth 2 · 3 ignored"*, and secondary `AppButton`s
*Rescan now* / *Clear ignored* / *Remove*. An unavailable source shows the
subline *"folder not found"* in `theme.stateDeleted` — the same token `AppButton`'s
`danger` variant uses. No sources → a muted *"No sources"* line.

### Feedback

Auto-added repos surface as a brief inline notice in the sidebar/project header
(*"2 repositories added from ~/projects"*), never a modal. Add failures from an
explicit batch reuse the existing error-dialog path used by `repoAddFailed`.

## Error handling

| Case | Behaviour |
|--|--|
| Root missing / not a directory | `scanForRepos` error → `scanFailed`, dialog shows the message inline |
| Unreadable subdirectory | Skipped silently; scan succeeds |
| No repositories found | Empty vector → dialog's empty-state line |
| One repo fails to add | Rest are still added; failures reported in `reposAdded` |
| Duplicate repo path | Already rejected by `addRepo`; surfaces as *already added*, never an error |
| Duplicate source path | `addSource` error, shown inline in the dialog |
| Source folder deleted | Counted as unavailable, source kept, badge in Project Options |
| No active project | Existing "No active project" guard |

## Testing (TDD — failing test first)

**`tests/test_repo_scan.cpp`** (new, `TempRepo`): depth 1 vs depth 2; descent
stops at a repository (a nested repo under a found repo is not returned);
dot-directories skipped; bare repository found; missing root → error; a tree with
no repositories → empty, not an error.

**`tests/test_project_store.cpp`**: add/remove source; duplicate source rejected;
JSON round-trip with sources; a document **without** `"sources"` loads with an
empty list; `ignoreInSources` matches by directory prefix only (`/a/bc` is not
inside `/a/b`); `clearIgnored`; `removeSource` leaves the repos in place.

**`tests/ui/test_project_controller.cpp`**: batch add persists once and emits one
`reposAdded`; one invalid path leaves the others added and is reported;
registering a source stores `maxDepth` and the unchecked paths as ignored;
`activate()` rescans and auto-adds a repo created after registration; a repo
removed from the project is not re-added by the next rescan; an unavailable
source does not stop the remaining sources in the pass.

New sources are registered in `core/CMakeLists.txt`, `ui/CMakeLists.txt` and the
matching lists in `tests/CMakeLists.txt`.

## Out of scope (YAGNI)

Filesystem watchers; per-repo alias editing in the bulk dialog; a project picker
in the dialog; global (cross-project) sources; auto-removing repos that vanish
from disk; anything manifest-related.

## Close-out

On ship, per [`docs/workflow.md`](../../workflow.md): fold the product flow, the
core scan/source engineering, and the dialog styling into the living
[`spec/`](../../spec/spec.md) sections; flip
[`bulk-add-projects.md`](../../wishlist/bulk-add-projects.md) to `done` and move
it to `wishlist/shipped/`.
