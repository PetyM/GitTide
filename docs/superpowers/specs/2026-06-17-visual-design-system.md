# GitTide — Visual Design System

**Date:** 2026-06-17
**Status:** Approved design, pre-implementation
**Applies to:** UI layer (`gittide::ui`). Consumed by all current and future UI implementation plans.

## 1. Purpose

Give GitTide a deliberate, branded look instead of default-Qt-widget grey. This
document is the single source of truth for color, type, spacing, shape, theming,
and component styling. Every UI plan references these tokens; no widget hard-codes
a color.

The brand already exists (see `docs/gittide-icon.svg`, `docs/gittide-logo-final.html`):
nested "echo" waves rising from a root commit, cyan on near-black, white HEAD. This
spec extends that identity to the whole application.

## 2. Design tokens

Tokens are the **only** legal source of color. Both themes define every token; a
widget reads a token, never a literal hex. The `ThemeManager` (§5) resolves the
active theme's table into a Qt stylesheet.

### 2.1 Surfaces, borders, text

| Token            | Dark      | Light     | Use |
|------------------|-----------|-----------|-----|
| `surface.base`   | `#0B1623` | `#EEF3F8` | Window background |
| `surface.raised` | `#11202F` | `#FFFFFF` | Sidebar, cards, dialogs, tab body |
| `surface.overlay`| `#16293B` | `#F4F8FB` | Menus, tooltips, popovers |
| `border`         | `#1E3245` | `#D4DFEA` | Dividers, control outlines |
| `text.primary`   | `#C9D1D9` | `#0B1623` | Headings, primary content |
| `text.secondary` | `#8B949E` | `#51606E` | Labels, secondary content |
| `text.muted`     | `#6E7681` | `#8595A4` | Hints, disabled, captions |

### 2.2 Accent (brand)

| Token          | Dark      | Light     | Use |
|----------------|-----------|-----------|-----|
| `accent`       | `#22D3EE` | `#0891B2` | Primary action, brand wave, selection, focus |
| `accent.hover` | `#4DDFF2` | `#0AA5CC` | Hover/active of accent elements |
| `head`         | `#FFFFFF` | `#0891B2` | HEAD / root commit node (matches icon) |

One accent only. Cyan is the brand; do not introduce a second hue for emphasis.

### 2.3 Git state colors

Used for status lists, diff gutter, dashboard badges, graph decorations. Identical
across themes (tuned for contrast on both surfaces).

| Token             | Hex       | Meaning |
|-------------------|-----------|---------|
| `state.added`     | `#3FB950` | Added / staged |
| `state.modified`  | `#D29922` | Modified |
| `state.deleted`   | `#F85149` | Deleted |
| `state.untracked` | `#6E7681` | Untracked |
| `state.conflict`  | `#DB6D28` | Conflict |

State is **never** signaled by color alone (§7) — always pair with an icon or letter
(A / M / D / U / C).

## 3. Typography

- **Family:** `system-ui` first, then platform fallbacks (`-apple-system`, `Segoe UI`,
  `Noto Sans`, `sans-serif`).
- **Monospace:** `ui-monospace`, then `SF Mono`, `Cascadia Code`, `Consolas`,
  `monospace`. Used for diffs, OIDs, file paths.
- **Scale (px):** `22` window/empty-state headline · `16` section heading · `13` body /
  control text · `11` caption / badge.
- **Weights:** `700` headline · `600` label/button · `400` body.

## 4. Spacing & shape

- **Grid:** 4px base. Allowed gaps/padding: `4 · 8 · 12 · 16 · 24`. Nothing off-grid.
- **Radius:** `6` controls (buttons, inputs, combo) · `10` cards / list rows / badges ·
  `18` dialogs and the wordmark lockup. Echoes the icon's rounded square.
- **Density:** comfortable, not cramped. Tree rows ≥ 28px tall; touch-friendly hit
  targets ≥ 24px.

## 5. Theming mechanism

- **`ThemeManager`** (new, `gittide::ui`) owns the active `Theme` (token table) and
  produces a Qt **QSS stylesheet string** from it. Applied app-wide via
  `qApp->setStyleSheet(...)`.
- **OS-driven default:** read `QStyleHints::colorScheme()` (Qt ≥ 6.5). Light if the OS
  reports light, dark otherwise. Subscribe to `colorSchemeChanged` and re-apply live.
- **Manual override:** a setting may force dark/light/system; default `system`.
- **Icon swap:** dark theme uses `gittide-icon.svg`; light uses `gittide-icon-light.svg`.
  Window/app icon and empty-state art follow the active theme.
- **No literals:** widgets carry object names / classes; QSS resolves color from tokens.
  Adding a theme = adding one token column, nothing else.

## 6. Component specs

- **Buttons.** Primary = filled `accent`, `text` on dark = `surface.base`; hover
  `accent.hover`. Secondary = `border` outline, transparent fill, `text.primary`. Ghost
  = no border, accent text, used for empty-state secondary links. Radius 6, padding
  `8×16`.
- **Project combo** (`projectSwitcher`). `surface.raised`, `border` outline; the
  "New project…" sentinel row separated and shown in `text.secondary`.
- **Repo tree rows** (`repoList`). Row height ≥ 28; selected row = `surface.raised` +
  2px `accent` left border. Missing repo = `text.muted` + warning icon (never red text
  alone). Radius 10 on row hover highlight.
- **Tabs** (`mainTabs`). Flat; active tab marked by a 2px `accent` underline, inactive
  `text.secondary`.
- **Diff gutter** (ChangesView / DiffView). Added lines `state.added`, deleted
  `state.deleted` at low alpha background + full-strength sign in the gutter. Mono font.
- **Progress modal** (clone). `surface.raised` card, `accent` progress bar, Cancel as
  secondary button.
- **Empty-state cards.** Replace the current bare `QPushButton`s
  (`createProjectCta` / `addExistingCta` / `initRepoCta` / `cloneCta`). Each empty page
  becomes a centered card (`surface.raised`, radius 18, max-width ~420px): brand icon on
  top, `22px` headline, `13px` `text.secondary` subtext, one primary CTA, secondary
  actions as ghost buttons. Object names unchanged so existing tests still find the CTAs.

## 7. Accessibility

- Body text contrast ≥ 4.5:1 against its surface; tokens above are chosen to pass on
  both themes.
- **Focus ring:** 2px `accent` outline on keyboard focus, always visible.
- **Never color-only:** every git state pairs color with an icon or letter; selection
  pairs color with the left border.
- Respect OS reduced-motion: theme/icon transitions instant if reduce-motion set.

## 8. Design principles (the checklist plans must follow)

Do:
- Read color from a **token**, never a hex literal in a widget or QSS rule outside the
  generated theme.
- Stay on the **4px grid** and the radius set (6 / 10 / 18).
- Use the **one accent** for primary action, selection, and focus — nothing else.
- Brand every **empty state** (icon + headline + CTA card).
- Define **both** dark and light values whenever a new token is introduced.
- Pair every state color with a **non-color** cue.

Don't:
- Hard-code `#...` in a widget.
- Add a second accent hue.
- Put text off-grid or invent a new radius.
- Signal status by color alone.
- Ship a component that only works in one theme.

## 9. Relationship to other docs

- Master spec `2026-06-16-gitgui-design.md` §10 (UI Layout) stays structural; it points
  here for the visual language.
- Implementation lands in **Plan 7 — Visual Theme** (`ThemeManager`, token header, QSS,
  restyle). The **rebrand** (Plan 6) renames `gitgui`→`gittide` first so this layer is
  authored against final names.
- Future UI plans (history/graph view, etc.) consume these tokens directly.
