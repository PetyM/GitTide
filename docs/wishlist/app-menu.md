# App menu + Options dialog

| | |
|--|--|
| **Added** | 2026-06-22 |
| **Status** | `idea` |
| **Touches** | product, design, engineering — app window chrome, new Options dialog, app-level settings persistence |

## What

A compact application menu in the window's title bar, on the **left** — the way
VS Code and GitHub Desktop place it. Behind it sits an **Options** dialog: a
single place for settings that today are scattered across the app (pull
behaviour, theme). Menu entries: **Options**, **About**, **Theme** toggle.

While we are touching the window chrome, the app launches **maximised** by
default and gains a sensible **sizing policy** (a minimum window size so the
layout never collapses).

## Why

- Settings have no home. Pull behaviour hides in a `⋯` menu on the branch bar;
  theme cycling hides in the sidebar; neither is discoverable, and there is no
  obvious place to add the next setting. An Options dialog gives them one.
- A title-bar app menu is the conventional home for app-level actions (Options,
  About, Quit) and scales as the app grows, without stealing content space.
- Launching maximised with a minimum size makes first-run feel finished and
  stops the UI from being resized into an unusable state.

## Notes

Direction agreed while capturing this wish (real design happens in the spec):

- **Custom title bar.** Today the window uses native OS decorations
  (`ApplicationWindow`, no frameless hint). The left-aligned menu requires going
  **frameless** — we draw our own min / maximise / close on the right and the
  compact menu on the left. This is the biggest piece: window dragging,
  double-click-to-maximise, and per-OS behaviour (esp. Windows/macOS) all become
  our responsibility. "Maximised" here means a maximised window with our title
  bar visible — **not** exclusive fullscreen (which would hide the title bar).
- **Options dialog** follows the existing modal `Dialog` + `OverlayCard` pattern
  (cf. `NewProjectDialog.qml`). It consumes the existing per-view controls:
  - **Pull behaviour** → becomes a **global default app setting** (rebase /
    merge), applied to all repos; the per-repo branch-bar override goes away in
    favour of the one default.
  - **Theme** (System / Dark / Light) moves out of the sidebar into the menu /
    Options. As a side effect it should finally **persist** — today it resets to
    System on every restart.
- **New app-settings persistence.** There is no app-level settings store yet
  (only `projects.json` via `ProjectStore`, and per-repo git config). This wish
  needs one — likely a core `SettingsStore` (JSON under `~/.config/gittide/`,
  mirroring `ProjectStore`) holding the pull default, theme mode, and room to
  grow. Window geometry persistence is out of scope unless cheap — default
  maximised is enough.
- **Open questions for design:** exact menu interaction (click-to-open popup vs
  hamburger), where window-control glyphs/theming come from, and whether the
  global pull default needs a one-time migration from existing per-repo git
  config.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/<section> · plan: plans/<file> -->
