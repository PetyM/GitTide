# Plans

The chronological record of *how* GitTide was built — one document per
implementation cycle, executed task-by-task and test-first. Each plan
**realises a section of the [spec](../spec/spec.md)**: the spec holds the design,
the plan records how it was implemented, and its **Outcome** section links back
to the spec that owns it. To read "what is true now", start at the spec; to read
"how we got here", read these.

New plans follow [`TEMPLATE.md`](TEMPLATE.md).

## Status

| Plan | Date | Status | Realises (spec) |
|------|------|--------|-----------------|
| [Core foundation](2026-06-16-core-foundation.md) | 2026-06-16 | done | engineering · core |
| [UI shell](2026-06-16-ui-shell.md) | 2026-06-16 | done | engineering · product |
| [Plan 3a — Core git ops](2026-06-17-plan3a-core-git-ops.md) | 2026-06-17 | done | engineering · core |
| [Plan 3b — Async + Changes tab](2026-06-17-plan3b-async-changes-ui.md) | 2026-06-17 | done | engineering · product |
| [Plan 4a — init/clone + store mutations](2026-06-17-plan4a-core-init-clone-mutations.md) | 2026-06-17 | done | engineering · core |
| [Plan 4b — Project/Repo management UI](2026-06-17-plan4b-ui-project-repo-management.md) | 2026-06-17 | done | product |
| [Plan 5a — Graph types + log](2026-06-17-plan5a-core-graph-log.md) | 2026-06-17 | done | engineering · core |
| [Plan 5b — History/graph view](2026-06-17-plan5b-ui-history-graph-view.md) | 2026-06-17 | done | product |
| [Plan 6 — Rebrand → GitTide](2026-06-17-plan6-rebrand-gittide.md) | 2026-06-17 | done | — |
| [Plan 7 — Visual theme](2026-06-17-plan7-visual-theme.md) | 2026-06-17 | done | design |
| [Code-style conformance](2026-06-17-code-style-conformance.md) | 2026-06-17 | done | engineering · code-style |
| [Plan 8 — Branch management](2026-06-18-plan8-branch-management.md) | 2026-06-18 | done | product · engineering · design |
| [Plan 9a — Core commit-selection + history diff](2026-06-18-plan9a-core-commit-selection-history-diff.md) | 2026-06-18 | done | engineering · core |
| [Plan 9b — GitHub-Desktop UI refactor](2026-06-18-plan9b-ui-github-desktop-refactor.md) | 2026-06-18 | done | product · design · engineering |
| [Plan 10 — Logging facility](2026-06-21-plan10-logging.md) | 2026-06-21 | done | engineering |
| [Plan 11 — Fleet fetch-all](2026-06-22-plan11-fleet-fetch-all.md) | 2026-06-22 | done | product · engineering |
| [Plan 12 — UI polish + sync transfer progress](2026-06-22-plan12-ui-polish-sync-progress.md) | 2026-06-22 | done | design · engineering · product |
| [Plan 13 — Diff block checkbox](2026-06-22-plan13-diff-block-checkbox.md) | 2026-06-22 | done | product |
| [Plan 14a — Core merge engine](2026-06-22-plan14a-core-merge-engine.md) | 2026-06-22 | done | engineering · core |
| [Plan 14b — UI merge + inline conflict](2026-06-22-plan14b-ui-merge-conflict.md) | 2026-06-22 | done | product · design · engineering |
| [Plan 15 — App menu, frameless title bar](2026-06-23-plan15-app-menu.md) | 2026-06-23 | done | design · product · engineering |
| [Plan 16 — Context menus](2026-06-23-plan16-context-menus.md) | 2026-06-23 | done | product · design |
| [Plan 17 — Keyboard controls](2026-06-23-plan17-keyboard-controls.md) | 2026-06-23 | planned | product · design |

> Migration note: these plans were authored under the earlier
> `docs/superpowers/` layout and carried over into this structure. The "Realises
> (spec)" column is the bridge from each plan to the living spec section that now
> owns its durable design.
