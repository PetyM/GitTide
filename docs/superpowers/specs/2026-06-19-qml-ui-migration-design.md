# QML UI Migration — Design Spec

**Date:** 2026-06-19
**Status:** Design approved, pre-implementation
**Topic:** Replace the QWidgets presentation layer with Qt Quick / QML, keeping `core/` and the ViewModel layer intact. Electron + React is the agreed fallback if QML output does not satisfy.

> This is a *future-state* design. It deliberately does **not** overwrite
> [`docs/spec/design/design.md`](../../spec/design/design.md), which documents the
> current QWidgets implementation (code is ground truth). When the QML layer ships,
> `docs/spec/` is updated as part of that build.

---

## 1. Motivation

The QWidgets UI did not reach the visual quality of the HTML/CSS mockups produced
during earlier brainstorming. Root cause is medium, not skill: QWidgets is a
retained-mode native toolkit whose styling (QSS — a thin CSS subset) has a low
ceiling and high cost for modern polish (gradients, shadows, smooth layout,
colored diffs). Concrete pains: overflowing overlays, no colored diffs, layout
fights.

**Chosen direction: Qt Quick / QML.** QML styling is close to CSS (gradients,
shadows, animation, GPU rendering, easy theme tokens), so it reproduces the mockup
quality, while keeping the pure-C++23 `core/` untouched and avoiding an IPC bridge.

**Fallback:** if QML output is unsatisfactory, switch to Electron + React/Tailwind
with `nodegit` (libgit2 bindings), accepting the loss of `core/` and a heavier
binary.

## 2. Architecture impact

The layering survives almost entirely. Only the **view layer** is rewritten.

| Layer | Fate |
|-------|------|
| `core/` (C++23, git engine, JSON) | **Unchanged.** No Qt, same invariants. |
| `ui/` controllers — `ProjectController`, `RepoController` | **Kept.** Become QML-accessible `QObject`s (context properties / `QML_ELEMENT`). |
| `ui/` models — `RepoListModel`, `HistoryModel`, `ProjectListModel`, `ChangedFilesList` | **Kept.** Already `QAbstractItemModel` → consumed directly by QML `ListView`/`TreeView`. |
| `ui/` `AsyncRepo` (QtConcurrent + QCoro) | **Kept.** Async bridge unchanged. |
| `ui/` QWidget views — `mainwindow`, `diffview`, `changesview`, `branchbar`, `projectsidebar`, `*dialogs`, `graphdelegate`, `historyview` | **Replaced by QML.** |
| `ui/` theming — `theme`, `thememanager`, `themestyle` (QPalette + QSS) | **Replaced** by a QML theme singleton exposing tokens as properties. |
| `app/` `WindowManager` | **Adapted** to host a `QQmlApplicationEngine` instead of constructing QWidget windows. |

Items needing real engineering attention:
- **Commit graph** — was a `QStyledItemDelegate.paint()`. In QML becomes a `Canvas`
  / `QQuickPaintedItem` / Qt Quick Shapes item drawing lanes + nodes from the
  existing `GraphBuilder` lane data (which stays in `core/`).
- **Testing** — logic lives in controllers/models (already headless Qt Test, no
  widgets) → keep those tests. View behaviour → `QtQuickTest` (`qmltestrunner`).
  The "headless UI runner" invariant changes shape.

## 3. Visual system

The token system from the existing design spec is **retained as-is** — same dark
and light tables, same accent, same git-state colors, same spacing/radius grid.
QML reads tokens from a theme singleton; no literal hex in components.

### Tokens (unchanged from current spec)

Surfaces/text/accent/state tokens, typography (system-ui / mono, scale 22·16·13·11,
weights 700·600·400), spacing (4px grid: 4·8·12·16·24), radius (6 controls · 10
cards/rows · 18 dialogs). See [`docs/spec/design/design.md`](../../spec/design/design.md)
for the full tables — they carry over verbatim.

### New / changed visual decisions (this round)

1. **Commit graph uses a multi-color lane palette** — the **one documented
   exception** to "one accent only". Lane palette:
   `lane0 #22D3EE` (cyan) · `lane1 #A371F7` (violet) · `lane2 #3FB950` (green) ·
   `lane3 #D29922` (amber) · `lane4 #F778BA` (pink). **HEAD node stays white**
   (`#FFFFFF` dark / accent light). UI chrome elsewhere keeps the single accent.
2. **Selecting a history row highlights the whole row including its graph cell** —
   the graph is drawn *over* transparent row backgrounds, so the selection fill
   shows behind the lanes; accent left border from x=0.
3. **Submodules render in the repo tree at the same row size as repositories**,
   distinguished by: indentation + a vertical guide rail + elbow connector,
   the `❖` glyph in accent (≈70% alpha), a short pinned OID, and a status dot
   (green=clean, amber=dirty). **Arbitrary nesting depth** (≥3 levels shown).
   Top-level repos separated by a divider so "next project" reads clearly.
4. **Tri-state master checkboxes** sit in headers, aligned over the column they
   control: one in the Changed-files header (replacing the old "Select all" link),
   one in the diff header (over the per-line checkbox column).
5. **Gravatar avatars** next to commit summary and in the history list. Initials
   fallback now; **future**: fetch avatars from GitHub/GitLab APIs when available.
6. **Branch bar** — prominent accent-tinted branch button (two-line: name +
   "Current branch"); Fetch/Pull/Push grouped into one segmented control with an
   ahead-count badge on Push.
7. **Branch dropdown** grouped into **Local / Worktrees / Remote (origin)** with a
   filter field; worktree entries show their path, remote-only entries dimmed with
   a `☁` glyph; sentinel rows New/Rename/Delete below a separator.
8. **"Add repository"** — plain label, no `+`, no dropdown chevron.
9. **Overlays never overflow** — menus/popovers have clamped width + max-height +
   internal scroll + real elevation (shadow + 1px ring); dialogs are radius-18
   cards with accent focus rings.
10. **New-branch dialog** includes a **"Create from" base-ref picker** (default:
    current branch).
11. **Delete-branch is confirmation-by-click, not type-to-confirm** — a warning
    bar for unmerged branches + a double-click-armed danger button + a
    **"Don't ask for confirmation again"** checkbox that disables the second
    confirmation thereafter.
12. **Empty states** — brand-centered cards (icon, 22px headline, secondary
    subtext) with stable CTA object names: `createProjectCta`, `addExistingCta`,
    `cloneCta`, `initRepoCta`, plus **`manifestProjectCta`**. The three add/clone/
    manifest actions share the same ghost-button styling.

## 4. New product capability — Create project from manifest

A project can be built from a **manifest file** listing its repositories (clone /
register many repos at once). Fits the GitTide model (project = group of repos).

- **Format: Google `repo`-style manifest XML** (AOSP `repo` tool — `<remote>` /
  `<project>` elements).
- **Engineering note:** parsing XML adds a **new dependency to `core/`** (today
  only libgit2 + nlohmann/json are private to core). An XML parser (e.g. pugixml,
  FetchContent) must be added under the same PRIVATE-linkage rule. This is a
  distinct feature, **not required for the first QML milestone** — the
  `manifestProjectCta` may be present-but-deferred initially.

## 5. Mockups (source of truth for the visual)

Saved alongside this spec, openable in any browser:

- [`mockups/gittide-qml-mainwindow.html`](mockups/gittide-qml-mainwindow.html) —
  full main window (dark). Working **Changes / History** tabs. Sidebar with nested
  submodules; branch bar; tri-state header checkboxes; colored diff with per-line
  checkboxes; History tab with multi-color commit graph, gravatar list, read-only
  files + diff.
- [`mockups/gittide-qml-overlays.html`](mockups/gittide-qml-overlays.html) —
  multi-color graph reference; branch dropdown (Local/Worktrees/Remote); project
  switcher; new-branch dialog with base-ref picker; delete confirm (no typing);
  clone progress modal.
- [`mockups/gittide-qml-empty-and-light.html`](mockups/gittide-qml-empty-and-light.html) —
  empty states (welcome, no-repos) and the **light theme** main window (token swap).

> The mockups are HTML/CSS approximations of the target. They define layout,
> hierarchy, color, spacing and interaction intent — not pixel-exact QML.

## 6. Open questions / deferred

- Manifest feature scheduling (likely a later milestone; XML dep decision at build).
- GitHub/GitLab avatar API integration (future enhancement; gravatar + initials now).
- Exact QML graph-rendering primitive (Canvas vs QQuickPaintedItem vs Shapes) —
  decided during implementation against `GraphBuilder` output.
- QML test strategy details (`QtQuickTest` scope vs controller/model coverage).
