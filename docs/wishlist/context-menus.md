# Context menus — right-click actions on every entity

| | |
|--|--|
| **Added** | 2026-06-21 |
| **Status** | `idea` |
| **Touches** | product (a consistent right-click action surface across the shell), design (menu component, action grouping, destructive-action affordance), engineering (ui: QML `Menu` wiring on the existing lists/models, reusing core ops already exposed — **not** core) |

## What

Give **every actionable entity in the shell a right-click context menu** offering
the verbs that make sense for it, so the user reaches an action where they're
already looking instead of hunting for a toolbar button or a kebab menu. Concretely,
per entity type:

- **Changed files** (working-changes list) — stage/unstage, discard changes, open
  in editor / reveal in file manager, copy path, ignore (add to `.gitignore`).
- **Projects** (sidebar) — rename, remove project, add repository, settings.
- **Repositories** (sidebar) — open in terminal / file manager, remove from
  project, fetch/refresh, copy remote URL.
- **Submodules** — update/init, open the submodule as its own repo, sync.
- **Branches** (branch picker / list) — checkout, new branch from here, rename,
  delete, merge into current.
- **Commits** (history list) — copy SHA, create branch/tag here, checkout, revert,
  view full detail.

Scope is a **consistent right-click surface over actions that already exist (or are
trivial wrappers)** — not new git capability. Where a verb needs a feature that
isn't built yet (merge, revert, tags…), the menu item is the natural home for it
when that wish lands; this wish establishes the *pattern* and wires the
already-available verbs.

## Why

Today actions are scattered and partial: there's a right-click "remove from
project" on top-level repos and a kebab menu on projects, but most entities have
no context menu at all, and what exists is inconsistent. Right-click-for-actions is
the single most expected interaction in a desktop app — every comparable client
(GitHub Desktop, GitKraken, file managers) puts the relevant verbs one right-click
away. A consistent context-menu layer makes the whole app feel finished and
discoverable, surfaces capabilities the user currently can't find, and gives every
future feature (merge, stash, revert, tags) an obvious place to hang its action.

## Notes

- **Layering — this is a `ui/` concern, not `core/`.** Everything here is QML
  `Menu`/`MenuItem` popups bound to the existing models and ViewModels, calling
  core operations that are already exposed (or thin wrappers over them). No core
  changes; no Qt creeps into core.
- **Build on what exists.** `Sidebar.qml` already has the patterns —
  `repoContextMenu` (right-click → remove-from-project), `projectMenu`,
  `addRepoMenu`. The work is generalising this into a *consistent* approach across
  every list rather than one-off menus: a shared menu component / convention, the
  same right-click affordance everywhere, and the full action set per entity.
- **Consistency is the point — needs a small design pass.** One menu component and
  one set of conventions: action ordering/grouping (with `MenuSeparator`s),
  destructive actions visually distinguished and ideally at the bottom (a **design
  token** for the destructive colour, not a hex literal), keyboard access to the
  menu, and a uniform right-click affordance. Define this in `spec/design` so menus
  don't drift per view.
- **Disabled vs. hidden.** Decide per item whether an inapplicable action is hidden
  or shown-disabled (e.g. "unstage" on an unstaged file). A consistent rule keeps
  menus predictable.
- **Destructive guards.** Discard-changes, delete-branch, remove-repo etc. must
  carry the same confirmation/guard discipline the app already applies elsewhere —
  never silently destroy work. Mirror the existing discard guards.
- **Pairs with keyboard controls.** Context menus and [keyboard
  controls](keyboard-controls.md) are the two halves of "act without hunting for a
  button" — a menu should be openable from the keyboard too (the menu/context key
  on the focused row), so the two wishes share the focus model.
- **Scope creep to resist.** No fully user-configurable menus, no plugin/extension
  actions, and don't *build* the not-yet-existing verbs here (merge, revert, tags) —
  add their menu items when those wishes land. First cut: a consistent menu pattern
  wiring the verbs that already exist, across all entity types.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (per-entity action sets), spec/design (menu component + grouping + destructive affordance), spec/engineering (shared ui menu wiring over existing models) · plan: plans/<file>
- Disabled-vs-hidden rule and the shared menu component shape → log in decisions.md
-->
