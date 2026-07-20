# Wishlist

Features wanted but not yet designed — the **start** of the lifecycle:

```
wishlist/ (wanted)   →   spec/ (designed)   →   plans/ (built)
```

One feature per file, copied from [`TEMPLATE.md`](TEMPLATE.md). Name the file
after the feature in kebab-case (e.g. `stash-management.md`). A wish is a seed,
not a design: capture the *what* and *why* quickly; the real design happens when
it graduates into a spec section and a plan, at which point its **Status** flips
and it links out to them.

## Open wishes

| Wish | Added | Status | Touches |
|------|-------|--------|---------|
| [Deployment — native installers (apt / brew / .exe)](deployment-packaging.md) | 2026-06-22 | `idea` | engineering, product |
| [Bulk-add existing repositories](bulk-add-projects.md) | 2026-06-18 | `idea` | product, engineering, design |
| [Rebase](rebase.md) | 2026-06-17 | `partly shipped` | product, engineering, design |
| [History editing — amend, squash, reword](history-editing.md) | 2026-06-17 | `partly shipped` | product, engineering, design |
| [Network sync — fetch / pull / push](network-sync.md) | 2026-06-17 | `partly shipped` | product, engineering, design |
| [Author avatars in commit history](author-avatars.md) | 2026-06-17 | `partly shipped` | product, design, engineering |
| [API docs via Doxygen (CMake-driven)](doxygen-api-docs.md) | 2026-06-17 | `idea` | engineering |
| [`repo` tool — manifest-driven multi-repo](repo-manifest-tool.md) | 2026-06-17 | `idea` | product, engineering, design |

<!-- Add a row per wish. When a wish reaches `done` or `dropped`, move it down
to the Shipped section so the open list stays scannable. -->

## Shipped

Graduated wishes, kept for the record — each links out to its spec sections and
plan from the bottom of its file.

| Wish | Added | Shipped | Touches |
|------|-------|---------|---------|
| [Branch management — create / switch / delete](shipped/branch-management.md) | 2026-06-17 | 2026-06-18 | product, engineering, design |
| [Merge — with conflict resolution](shipped/merge.md) | 2026-06-17 | 2026-06-22 | product, engineering, design |
| [GitHub-Desktop-style UI refactor](shipped/github-desktop-ui-refactor.md) | 2026-06-17 | 2026-06-18 | product, design, engineering |
| [Logging — categories with per-category and global levels](shipped/logging.md) | 2026-06-21 | 2026-06-21 | engineering, product |
| [App menu + Options dialog](shipped/app-menu.md) | 2026-06-22 | 2026-06-23 | product, design, engineering |
| [Context menus — right-click actions on every entity](shipped/context-menus.md) | 2026-06-21 | 2026-06-23 | product, design, engineering |
| [Keyboard controls — navigate and act without the mouse](shipped/keyboard-controls.md) | 2026-06-21 | 2026-06-23 | product, design, engineering |
| [Stash management — stackable](shipped/stash-management.md) | 2026-06-17 | 2026-06-30 | product, engineering, design |
| [Code syntax highlighting in diffs](shipped/diff-syntax-highlighting.md) | 2026-06-21 | 2026-06-30 | product, design, engineering |
