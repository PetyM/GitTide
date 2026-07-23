# Design

The visual design system: the single source of truth for colour, type, spacing,
shape, theming, and component styling. Every widget reads a **token**; no widget
hard-codes a colour. Implementation lives in `ui/src/theme*.cpp` — see the wiring
notes at the end.

The brand is a set of nested "echo" waves rising from a root commit — cyan on
near-black, a white HEAD. The source art lives in
[`../../assets/`](../../assets/) (`gittide-icon.svg`, `gittide-icon-light.svg`,
`gittide-mark.svg`, `gittide-logo-final.html`).

## Tokens

Tokens are the **only** legal source of colour. Both themes define every token; a
widget reads a token, never a literal hex. `ThemeManager` resolves the active
theme's table into a Qt **`QPalette`** plus a small accent stylesheet (§ Theming).

### Surfaces, borders, text

| Token            | Dark      | Light     | Use |
|------------------|-----------|-----------|-----|
| `surface.base`   | `#1C1C1E` | `#F5F5F5` | Window background |
| `surface.raised` | `#262628` | `#FFFFFF` | Sidebar, cards, dialogs, tab body |
| `surface.overlay`| `#333336` | `#EAEAEA` | Menus, tooltips, popovers |
| `border`         | `#3D3D40` | `#E0E0E0` | Dividers, control outlines |
| `text.primary`   | `#E4E4E6` | `#212121` | Headings, primary content |
| `text.secondary` | `#A6A6AB` | `#5F5F5F` | Labels, secondary content |
| `text.muted`     | `#757579` | `#9E9E9E` | Hints, disabled, captions |
| `shadow`         | `#66000000` | `#24000000` | Overlay drop-shadow (translucent) |

Neutrals are Material Grey — a neutral (unbiased) grey ground.

### Accent (brand)

| Token          | Dark      | Light     | Use |
|----------------|-----------|-----------|-----|
| `accent`       | `#42A5F5` | `#1976D2` | Primary action, brand wave, selection, focus |
| `accent.hover` | `#64B5F6` | `#1565C0` | Hover/active of accent elements |
| `head`         | `#E3F2FD` | `#1976D2` | HEAD / root commit node (matches the icon) |

**One accent only.** Material Blue is the brand (Blue 400 on dark, Blue 700 on
light); do not introduce a second hue for emphasis.

### Git state colours

Used for status lists, the diff gutter, dashboard badges, and graph decorations.
Identical across themes (tuned for contrast on both surfaces).

| Token             | Hex       | Meaning |
|-------------------|-----------|---------|
| `state.added`     | `#3FB950` | Added / staged |
| `state.modified`  | `#D29922` | Modified |
| `state.deleted`   | `#F85149` | Deleted |
| `state.untracked` | `#6E7681` | Untracked |
| `state.conflict`  | `#DB6D28` | Conflict (badge / merge banner) |
| `state.incoming`  | `#388BFD` | Merge "Incoming (theirs)" conflict band |

State is **never** signalled by colour alone — always pair it with an icon or a
letter (A / M / D / C). The inline merge-conflict bands reuse this git-state
palette — *Current (ours)* tints from `state.added` (green), *Incoming (theirs)*
from `state.incoming` (blue) — at low alpha as backgrounds, each paired with its
text label. These are state signalling like the diff gutter and lane colours, not
a second accent hue (the one-accent rule, D17, governs emphasis/action colour).

## Typography

- **Family:** `system-ui` first, then `-apple-system`, `Segoe UI`, `Noto Sans`,
  `sans-serif`.
- **Monospace:** `ui-monospace`, then `SF Mono`, `Cascadia Code`, `Consolas`,
  `monospace`. Used for diffs, OIDs, and file paths.
- **Scale (px):** `22` window / empty-state headline · `16` section heading ·
  `13` body / control text · `11` caption / badge.
- **Weights:** `700` headline · `600` label / button · `400` body.

## Spacing & shape

- **Grid:** 4px base. Allowed gaps / padding: `4 · 8 · 12 · 16 · 24`. Nothing
  off-grid.
- **Radius:** `6` controls (buttons, inputs, combo) · `10` cards / list rows /
  badges · `18` dialogs and the wordmark lockup. Echoes the icon's rounded
  square.
- **Density:** comfortable. Tree rows ≥ 28px tall; hit targets ≥ 24px.

## Theming

- **Pure Qt Quick/QML.** There is no QWidgets / Fusion / QSS / `QPalette` path —
  the UI is QML and reads colour straight from theme tokens exposed as bindable
  properties. Adding a theme = adding one token column, nothing else.
- **`ThemeManager`** (`gittide::ui`) owns the active mode and resolves the active
  `Theme` (token table). **`QmlTheme`** wraps it and republishes every token as a
  bindable `Q_PROPERTY` (`QColor`s, `shadow`, `laneColors`, `iconSource`); a
  `themeChanged` → `changed()` hop refreshes every QML binding on a live switch.
  QML widgets bind `theme.<token>` directly — never a hard-coded hex.
- **Mode + toggle.** Mode is `System | Dark | Light` (`QmlTheme.mode`, an int
  mirroring `ThemeManager::Mode`; writable from QML). The sidebar header carries
  a toggle button (`objectName: themeToggle`) that calls `theme.cycleMode()` to
  rotate System → Dark → Light → System; its glyph reflects the mode
  (☾ dark / ☀ light / ◐ system). The override is **session-only** — settings
  persistence is deferred.
- **OS-driven default:** read `QStyleHints::colorScheme()`; subscribe to
  `colorSchemeChanged` and re-emit live while in `System` mode (the brand's
  primary look is dark, so Unknown/Dark → dark). Default is `System`.
- **Icon swap:** dark uses `gittide-icon.svg`, light uses
  `gittide-icon-light.svg`, surfaced as `theme.iconSource` (a `qrc:/…` URL) and
  followed by the sidebar wordmark / empty-state art.

## Components

- **Action buttons (`AppButton`).** All plain action buttons use `AppButton` (not
  raw Basic `Button`). Three variants: `primary` — filled `accent` / hover
  `accentHover`, text `surfaceBase`; `secondary` — `border` outline, transparent
  fill / hover `surfaceOverlay`, text `textPrimary`; `danger` — filled
  `stateDeleted` / hover darker, text `surfaceBase`. `compact: true` cuts height
  (30 → 22) and padding (14 → 8) for inline affordances such as the submodule
  Init pill. Disabled: `surfaceOverlay` fill, `textMuted` text. Radius 6.
  Ghost links (empty-state secondaries) remain bespoke — they are not plain action
  buttons.
- **Action dropdowns (`AppComboBox`).** All plain dropdowns use `AppComboBox`
  (not raw Basic `ComboBox`). Field: `surfaceBase` fill + `border` outline → `accent`
  ring on focus/press, radius 6, `textPrimary` label. Popup card: `surfaceRaised` +
  `border`, radius 6; delegate rows highlight to `surfaceOverlay`; scroll via
  `AppScrollBar`.
- **Specialized controls (bespoke, not `AppButton`/`AppComboBox`).** `MainTab`
  (accent-underline tab), `WindowButton` (title-bar chrome), `AppRadioButton` /
  `AppCheckBox` (form toggles), and `EmptyState.Cta` (large empty-state CTA) each
  keep their own styling — they are distinct from plain action controls.
- **Project combo** (`projectSwitcher`). Styled as an `AppComboBox` **field** so it
  reads as a peer of the branch chip: `surface.base` fill, `border` outline →
  `accent` ring while its menu is open, radius 6, `▾` indicator, `text.primary`
  label. It stays a `Button`+`AppMenu` (the menu carries the New/Delete sentinels a
  plain combo can't) — the "New project…"/"Delete current project…" rows are
  separated and shown in `text.secondary`. Directly **below it** sits the
  full-width **Fetch all** button (secondary `AppButton`; label reads *Fetching…*
  and disables while a fleet fetch runs). While fetching, a **determinate progress
  bar** (`accent` fill on `surface.overlay`, `fetchAllProgress`) with a
  *done / total* repo caption sits directly beneath it — repos-settled over
  batch-size, not byte transfer. No bespoke icon. Per-repo outcome shows
  in the tree (the fetch-result glyph); a fleet fetch that ends with **non-auth
  failures** raises an error dialog (`fetchErrorDialog`) listing each failed repo
  as *name: message* — there is no passing status caption. Auth failures instead
  prompt for credentials and retry; the error dialog only appears once that retry
  settles.
- **Separators.** Two idioms. A **draggable** split uses a 3px handle that tints
  `border` → `accent` on hover — the repo tree ↔ changes/history/graph seam
  (sidebar min-width 200, pane min-width 360), the Changes files ↔ diff split, and
  the History files ↔ diff split all share it, so any resizable boundary is both
  visible and draggable. A **fixed** structural seam is a 1px `border` hairline
  (the branch-bar bottom edge, the top-level-repo dividers in the tree).
- **Repo tree rows** (`repoList`). Row height ≥ 28; selected row =
  `surface.raised` + a 2px `accent` left border. A missing repo is `text.muted` +
  a warning icon, never red text alone. Radius 10 on hover highlight. Top-level
  repositories are separated by a faint `border` divider above each repo after the
  first. Rows carry **no type glyph**; a leading **chevron** (`▸`/`▾`,
  `text.secondary`) toggles a row's subtree — shown only on rows that have
  children, with leaf rows reserving the same width so names stay aligned. Rows are
  **collapsed by default**; opening a repo (clicking its row) expands it, and the
  chevron toggles any subtree thereafter. **Submodules** render recursively
  (arbitrary depth): the pinned short OID shows in mono `text.muted`; a status dot
  reuses the git-state tokens — `state.modified` (dirty) or `state.added` @0.55α
  (clean) — paired with the OID text, never colour alone; an uninitialised
  submodule is dimmed (`text.muted`) with no OID or dot. Each level draws a
  `border` guide rail + elbow connector. (Like the history graph's multi-colour
  lanes, the status dot is a sanctioned reuse of git-state tokens for repository
  structure, not a new palette.)
- **Tabs** (`mainTabs`). The list column's Changes | History sub-tabs. Flat;
  active tab marked by a 2px `accent` underline, inactive in `text.secondary`.
- **Changed-files list** (`changedFilesList`). One row per changed file: a
  leading **tri-state checkbox** (checked / unchecked / partial), the path, and a
  trailing `state.*`-coloured **letter** (A / M / D / C) — state paired with a
  cue, never colour alone. A **new file is always `A`** whether staged or
  untracked: the checkbox already conveys staged-or-not, so a separate `U` letter
  would only duplicate it. The **letter, the file name, and a faint row
  background** are all tinted by status — `state.added` green for added *and*
  untracked (a new file reads as green, not the muted `state.untracked` grey),
  `state.deleted` red for deleted, else neutral; the row background uses the same
  hue at ~0.12α and the directory prefix stays `text.muted`. Selected row =
  `surface.raised` + 2px `accent` left border. The same widget renders a commit's
  files in **read-only** mode (no checkboxes) under the History tab.
  - **Selection is the user's.** A status refresh (the live watcher fires often)
    preserves each file's existing check state — it never re-checks what the user
    unchecked. New files/changes inherit the current intent: checked only when
    *everything* was already checked, otherwise unchecked, so a refresh never
    silently grows a partial selection. A fresh open (no prior selection) defaults
    to all-checked.
  - **Submodule pointer rows.** A changed submodule (gitlink) carries a **tri-state**
    checkbox, auto-derived from the submodule's state — unchecked = leave the pin;
    full check = included and the submodule is clean; **partial (dash)** = included
    but the submodule's working tree is *dirty* (uncommitted work). Either checked
    form commits the same thing: the submodule's **HEAD (last commit)** is recorded
    — git never pins the dirty content, so a superproject can't record a `-dirty`
    pointer. The partial state is a warning (hover tooltip) that committing leaves
    those uncommitted changes behind in the submodule. Submodule rows always stage
    whole-file, never per-line. Added lines `state.added`, deleted
  `state.deleted` at low-alpha background with a full-strength sign in the gutter;
  mono font. In working-changes (editable) mode each line carries a leading
  checkbox; in history (read-only) mode no checkboxes appear.
- **Merge banner** (`mergeBanner`). Shown above the Changes list whenever the repo
  is mid-merge (driven by `MergeState`, derived from disk). A `state.conflict`
  (orange) left accent + warning glyph, the text *"Merging \<branch\> into
  \<current\> — N conflicted files"* in `text.primary`, and trailing actions:
  **Abort merge** (destructive secondary) always present, **Commit merge**
  (primary, disabled until zero conflicts remain), and — only when the conflicts
  include submodule pointers — **Deinit submodules & retry** (secondary). Conflict
  is paired with the glyph + text, never colour alone.
- **Inline conflict view.** A conflicted file opens in the shared diff panel with
  its `<<<<<<< / ======= / >>>>>>>` regions rendered inline. The *Current (ours)*
  band tints from `state.added`, the *Incoming (theirs)* band from `state.incoming`
  (both low-alpha backgrounds), each with a labelled header row carrying per-region
  **Accept Current · Accept Incoming · Accept Both** ghost actions; the body stays
  editable for a hand-merge. Conflicted files in `changedFilesList` show the `C`
  letter in `state.conflict`. Bands are always labelled, so the two hues are never
  the sole signal.
- **Sidebar collapse.** A toggle collapses the project/repo sidebar to a slim
  rail and back; the collapsed rail keeps the active-repo affordance reachable.
- **Branch bar** (`branchBar`). A bar above the tabs. The current-branch chip
  (`branchChip`) is a **fixed-width**, `accent`-tinted button (branch name in
  `text.primary` over a "Current branch" caption, a `▾` chevron pinned right); a
  long name elides. A detached `HEAD` shows `detached @ <short-oid>` (mono OID) —
  state paired with a cue, never colour alone. Immediately to its right is the
  **sync cluster**: equal-width **Fetch / Pull / Push** buttons (secondary outline
  style). Pull and Push carry the behind / ahead count as an **inline `accent`
  pill** beside the label (not a corner badge), and appear only with an upstream;
  **Publish** replaces them when the branch has no upstream. While a fetch/pull/push
  runs, a **progress bar** (`accent` fill on `surface.overlay`) with a
  `received / total` caption shows beside the cluster — an indeterminate sweep
  until libgit2 reports object counts, then determinate — in place of a bare
  spinner. A trailing `⋯` opens the pull-strategy menu. Its
  dropdown is a menu on `surface.overlay`, grouped Local / Worktrees / Remote: the
  current branch is marked by an `accent` left border + check icon; remote-tracking
  rows carry a `☁` glyph and sit slightly dimmed (they are *not yet local*) yet are
  fully clickable — picking one checks the remote branch out (DWIM); a separator;
  then sentinel rows ("New branch…", "Rename current…", "Delete…") in
  `text.secondary`.
- **Elevation.** Every overlay (dialogs + the branch dropdown) floats on a shared
  `OverlayCard` background: a themed rounded card with a 1px `border` ring **and** a
  translucent `shadow` drop shadow (rendered via `MultiEffect`), so popovers and
  modals read as above the content, not painted flat onto it (design §9). The card
  colour/radius default to a dialog (`surface.raised` / 18) and are overridden by
  the dropdown (`surface.overlay` / 10).
- **Dialog chrome.** Every modal is an `AppDialog` — a shared base that replaces
  QtQuick.Controls.Basic's default `Dialog` header (an unthemed `palette.light`
  white bar with square corners and no close affordance) with a themed header:
  the `title` in `text.primary` on the left and a close **✕** button on the right
  (`text.muted` → `text.primary` with a `surface.overlay` hover chip). The header
  background is transparent so the `OverlayCard`'s rounded top corners show through
  instead of being capped by a square bar; clicking ✕ rejects the dialog. Set
  `closable: false` where a dialog must not be dismissed by a stray click (e.g. the
  clone-progress modal, dismissed only via Cancel, which aborts the clone).
  `AppDialog` also **centres in the window** (it parents to `Overlay.overlay`, not
  the item it is declared in, so a dialog nested in the diff pane still centres over
  the whole app) and **sizes to its content** (deriving `implicitHeight` from
  header + content + footer, since QtQuick's `Dialog` drops the content height from
  its implicit size once a `footer` is present — the card would otherwise be too
  short, overflowing its content and overlapping the footer).
- **Dialog body & footer primitives.** Two shared wrappers keep every dialog laid
  out correctly. `DialogColumn` is the content wrapper: a `ColumnLayout` assigned
  *directly* as a Popup's `contentItem` does not propagate its implicit height (the
  dialog then sizes too short), so dialog bodies wrap their vertical stack in
  `DialogColumn`, a plain Item that reports the column's implicit size. `DialogButtons`
  is the footer: a right-aligned button row inset from the card edges (a bare
  `RowLayout` footer sits flush against the border — `Layout.margins` there is a
  no-op because the footer's parent is not a Layout — and also fails to give the
  Dialog a stable footer height). Declare `AppButton`s as its children.
- **Branch dialogs.** New-branch / rename / delete-confirm follow the dialog
  pattern: `OverlayCard` (`surface.raised`, radius 18, `border` ring + shadow), 2px
  `accent` focus rings. New-branch = name input + a "switch to it" checkbox; an
  invalid name disables the primary button and shows the reason in `text.secondary`.
  Delete-confirm for an unmerged branch reveals a second-step "delete anyway (not
  fully merged)" action styled as a destructive secondary button.
- **Credential dialog.** HTTPS auth prompt follows the same `OverlayCard` pattern
  with `accent`-focus-ring fields (username + masked token) and a primary "Sign in"
  button disabled until both fields are filled.
- **Clone progress modal.** `OverlayCard`, themed `accent` progress bar with a
  percentage readout (`received / total objects (NN%)`), Cancel as a secondary
  button.
- **Menus** (`AppMenu` / `AppMenuItem`). All popup menus (add-repo, project
  switcher, repo context, pull-strategy) ride on a `surface.overlay` rounded card
  (radius 10, 1px `border` ring); items are `text.primary` with an `accent`-tint
  hover, disabled items `text.muted`. Checkable items keep a plain row so the tick
  survives.
- **Context menus.** `AppMenu` + `AppMenuItem` rows with `AppMenuSeparator` between groups. Destructive actions (discard, delete, remove) set `AppMenuItem { destructive: true }`, which renders text in `state.deleted` — same hover highlight, just the label colour changes. Each entity type has a dedicated QML component (`FileContextMenu`, `BranchContextMenu`, `CommitContextMenu`, `RepoContextMenu`); see [spec/product/context-menus.md](../product/context-menus.md) for the per-entity action tables and disabled/hidden rules.
- **Scrollbars.** Scrollable lists (history, changed files, diff, commit files,
  the sidebar repo tree) attach the shared `AppScrollBar` (`ScrollBar.vertical`).
  It is **visible whenever the content overflows** (`policy: AsNeeded`) and does
  not auto-fade — the handle only re-tints on hover/press (`border` → `text.muted`
  → `text.secondary`). The track is transparent; colour comes from tokens. The
  same lists add `WheelScroller`, which jumps `contentY` by the wheel delta on a
  **mouse** wheel (replacing the Flickable's sluggish decelerating flick);
  touchpads keep their native smooth pixel-delta scrolling.
- **Progress over spinners.** Any operation that can report quantitative progress
  shows a **determinate** bar (`accent` fill on `surface.overlay`) with a
  `received / total` (or percent) caption — fetch/pull/push in the branch bar,
  clone in its modal. A bare busy spinner / indeterminate sweep is a *fallback*,
  used only while counts are unknown or for work that genuinely can't report
  progress. Never park a spinner on an operation that exposes real progress.
- **Empty-state cards.** Each empty page is a centered card (`surface.raised`,
  radius 18, max-width ~420px): brand icon, `22px` headline, `13px`
  `text.secondary` subtext, one primary CTA, secondary actions as ghost buttons.
  CTA object names are stable (`createProjectCta`, `addExistingCta`,
  `initRepoCta`, `cloneCta`) so tests keep finding them.

### QML History view

The History tab is implemented in QML (Plan 4 — History Graph). The commit list is a virtualized `ListView`; each row owns a `GraphColumn` (`QQuickPaintedItem`, registered as `GitTide 1.0/GraphColumn`) that paints one `GraphRow`'s lane geometry — pass-through verticals, incoming line, outgoing edges, and the commit dot. Lanes are coloured by `laneColors[lane % laneColors.length]` (the multi-hue `theme.laneColors` list; the only sanctioned multi-colour exception). The HEAD commit's dot is drawn in `theme.head` (white) regardless of lane colour. A **local-only** (not-yet-pushed) commit's dot is drawn **hollow** — stroked outline in the lane/HEAD colour, transparent fill — a shape cue that never relies on colour alone. When a history row is selected, a 2px `accent` `Rectangle` at `x = 0` spans its full height (covering the graph cell), and the row background fills with `surfaceOverlay` — both sit behind the `GraphColumn`'s transparent background so the lane lines remain visible. **Avatars** (`Avatar.qml`, 24px disc) show the author's real image over the initials fallback: initials on an `accent`-tinted disc render instantly, and the network image (Gravatar, keyed on `md5(email)` via the `image://avatar/<hash>` provider) swaps in on `Image.Ready`, so rows never jump; an absent/failed/disabled avatar leaves the initials showing. In both the History and Graph rows, an **unpushed commit** carries a trailing **`↑` arrow in `accent`** (bold; tooltip *"Local only — not yet pushed"*) and its whole row is tinted with a faint `accent` wash (`accent` at ~8% alpha, under any selection highlight) so the not-yet-shared commits read as a distinct band; in History the summary also stays `text.primary` while pushed summaries drop to `text.secondary`. The arrow glyph and (in Graph) the hollow dot keep the cue from being colour-only. The right-hand detail pane (`CommitDetail`) is a read-only view: files in the selected commit, then the diff when a file is picked — the two are split by a **draggable vertical handle** (the 3px `border`→`accent`-hover handle shared with the Changes pane) so the changed-files list is clearly divided from the selected diff and each side is resizable; it also hosts a secondary **Checkout** button that detaches HEAD at the selected commit.

**Drag-to-reorder grip.** Rows that can be reordered — those in the reorderable run (the linear single-parent span from HEAD), and every row in the `RebaseTodoDialog` todo editor — carry a trailing **`⠿` grip** glyph in `text.muted`, brightening to `accent` while a drag is active. The grip is the affordance (an icon, not a colour-only state — D19) and a tooltip names it; dragging it reorders. In the history view a reorder is gated behind a confirmation because it rewrites history (D36).

## Accessibility

- Body-text contrast ≥ 4.5:1 against its surface (tokens above pass on both
  themes).
- **Focus ring:** a 2px `accent` outline on keyboard focus, always visible.
- **Never colour-only:** every git state pairs colour with a non-colour cue;
  selection pairs colour with the left border.
- Respect OS reduced-motion: theme / icon transitions are instant when
  reduce-motion is set.

## Principles (the checklist)

**Do:** read colour from a token; stay on the 4px grid and the radius set
(6 / 10 / 18); use the one accent for primary action, selection, and focus; brand
every empty state; define **both** dark and light values for any new token; pair
every state colour with a non-colour cue; **show determinate progress (counts /
percent) wherever the operation reports it**, falling back to a busy/indeterminate
indicator only when it can't.

**Don't:** hard-code a hex in a widget; add a second accent hue; place text
off-grid or invent a new radius; signal status by colour alone; ship a component
that only works in one theme; **park a bare busy spinner on an operation that can
report real progress.**

## Wiring

Token table and theme factories: `ui/src/theme.cpp` (`Theme`, `darkTheme()`,
`lightTheme()`). Mode ownership + OS-scheme resolution + live re-emit + icon
selection: `ui/src/thememanager.cpp`. Tokens → bindable QML properties (+ the
`mode`/`cycleMode` toggle API): `ui/src/qmltheme.cpp`. QML views bind
`theme.<token>` via the `theme` context property installed in
`ui/src/qmlcontext.cpp`. Per-symbol contracts live in the headers under
`ui/include/gittide/ui/`.
