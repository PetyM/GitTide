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
theme's table into a Qt stylesheet (§ Theming).

### Surfaces, borders, text

| Token            | Dark      | Light     | Use |
|------------------|-----------|-----------|-----|
| `surface.base`   | `#0B1623` | `#EEF3F8` | Window background |
| `surface.raised` | `#11202F` | `#FFFFFF` | Sidebar, cards, dialogs, tab body |
| `surface.overlay`| `#16293B` | `#F4F8FB` | Menus, tooltips, popovers |
| `border`         | `#1E3245` | `#D4DFEA` | Dividers, control outlines |
| `text.primary`   | `#C9D1D9` | `#0B1623` | Headings, primary content |
| `text.secondary` | `#8B949E` | `#51606E` | Labels, secondary content |
| `text.muted`     | `#6E7681` | `#8595A4` | Hints, disabled, captions |

### Accent (brand)

| Token          | Dark      | Light     | Use |
|----------------|-----------|-----------|-----|
| `accent`       | `#22D3EE` | `#0891B2` | Primary action, brand wave, selection, focus |
| `accent.hover` | `#4DDFF2` | `#0AA5CC` | Hover/active of accent elements |
| `head`         | `#FFFFFF` | `#0891B2` | HEAD / root commit node (matches the icon) |

**One accent only.** Cyan is the brand; do not introduce a second hue for
emphasis.

### Git state colours

Used for status lists, the diff gutter, dashboard badges, and graph decorations.
Identical across themes (tuned for contrast on both surfaces).

| Token             | Hex       | Meaning |
|-------------------|-----------|---------|
| `state.added`     | `#3FB950` | Added / staged |
| `state.modified`  | `#D29922` | Modified |
| `state.deleted`   | `#F85149` | Deleted |
| `state.untracked` | `#6E7681` | Untracked |
| `state.conflict`  | `#DB6D28` | Conflict |

State is **never** signalled by colour alone — always pair it with an icon or a
letter (A / M / D / U / C).

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

- **`ThemeManager`** (`gittide::ui`) owns the active `Theme` (token table) and
  produces a Qt **QSS** string from it, applied app-wide via
  `qApp->setStyleSheet(...)`.
- **OS-driven default:** read `QStyleHints::colorScheme()`; subscribe to
  `colorSchemeChanged` and re-apply live. A manual override (dark / light /
  system) may force the mode; default is `system`.
- **Icon swap:** dark uses `gittide-icon.svg`, light uses
  `gittide-icon-light.svg`. The app/window icon and empty-state art follow the
  active theme.
- **No literals:** widgets carry object names / classes; QSS resolves colour from
  tokens. Adding a theme = adding one token column, nothing else.

## Components

- **Buttons.** Primary = filled `accent`, text = `surface.base` on dark; hover
  `accent.hover`. Secondary = `border` outline, transparent fill, `text.primary`.
  Ghost = no border, accent text (empty-state secondary links). Radius 6,
  padding `8×16`.
- **Project combo** (`projectSwitcher`). `surface.raised` with `border` outline;
  the "New project…" sentinel row is separated and shown in `text.secondary`.
- **Repo tree rows** (`repoList`). Row height ≥ 28; selected row =
  `surface.raised` + a 2px `accent` left border. A missing repo is `text.muted` +
  a warning icon, never red text alone. Radius 10 on hover highlight.
- **Tabs** (`mainTabs`). Flat; active tab marked by a 2px `accent` underline,
  inactive in `text.secondary`.
- **Diff gutter.** Added lines `state.added`, deleted `state.deleted` at low-alpha
  background with a full-strength sign in the gutter. Mono font.
- **Clone progress modal.** `surface.raised` card, `accent` progress bar, Cancel
  as a secondary button.
- **Empty-state cards.** Each empty page is a centered card (`surface.raised`,
  radius 18, max-width ~420px): brand icon, `22px` headline, `13px`
  `text.secondary` subtext, one primary CTA, secondary actions as ghost buttons.
  CTA object names are stable (`createProjectCta`, `addExistingCta`,
  `initRepoCta`, `cloneCta`) so tests keep finding them.

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
every state colour with a non-colour cue.

**Don't:** hard-code a hex in a widget; add a second accent hue; place text
off-grid or invent a new radius; signal status by colour alone; ship a component
that only works in one theme.

## Wiring

Token table and theme factories: `ui/src/theme.cpp` (`Theme`, `darkTheme()`,
`lightTheme()`). QSS generation: `ui/src/themestyle.cpp` (pure `Theme → QString`).
Application + OS-scheme resolution + live re-apply + icon: `ui/src/thememanager.cpp`.
Per-symbol contracts live in the headers under `ui/include/gittide/ui/`.
