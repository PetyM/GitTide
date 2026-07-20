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
| [Plan 17 — Keyboard controls](2026-06-23-plan17-keyboard-controls.md) | 2026-06-23 | done | product · design |
| [Plan 18 — History range-diff & reword-tip](2026-06-24-plan18-history-range-reword.md) | 2026-06-24 | done | product · engineering · core |
| [Plan 19 — Plain rebase driver (Tier 1)](2026-06-24-plan19-rebase-driver.md) | 2026-06-24 | shipped | product · engineering · core |
| [Plan 20 — Interactive rebase engine (Tier 2)](2026-06-24-plan20-interactive-rebase.md) | 2026-06-24 | shipped | product · engineering · core |
| [Plan 21 — Live refresh (file watching + fleet poll)](2026-06-25-plan21-live-refresh.md) | 2026-06-25 | done | engineering |
| [Plan 22 — History-editing UX (squash · drag reorder · undo)](2026-06-25-plan22-history-editing-ux.md) | 2026-06-25 | done | product · design |
| [Plan 23 — Whole-row drag + drag-to-squash](2026-06-25-plan23-history-drag-squash.md) | 2026-06-25 | done | product · design |
| [Plan 24 — Submodule init / update from the GUI](2026-06-26-plan24-submodule-init-update.md) | 2026-06-26 | done | product · engineering · core |
| [Plan 25 — History redesign + full git-graph tab](2026-06-26-plan25-history-graph-tab.md) | 2026-06-26 | done | product · core · ui |
| [Plan 26 — Squash / drag UX polish](2026-06-26-plan26-squash-drag-ux.md) | 2026-06-26 | done | product · ui |
| [Plan 27 — Themed controls (AppButton + AppComboBox)](2026-06-26-plan27-themed-controls.md) | 2026-06-26 | done | design · ui |
| [Plan 28 — Multi-select squash skips the todo editor](2026-06-29-plan28-squash-skip-todo-editor.md) | 2026-06-29 | done | product · ui |
| [Plan 29 — Title-bar menu bar](2026-06-30-plan29-menu-bar.md) | 2026-06-30 | done | product · engineering · design |
| [Plan 31 — Stash viewing & management](2026-06-30-plan31-stash-management.md) | 2026-06-30 | done | product · engineering · core · ui |
| [Plan 32 — Graph tab full-width + double-click hand-off to History](2026-07-01-plan32-graph-fullwidth-doubleclick.md) | 2026-07-01 | done | product · ui |
| [Plan 33 — Repo tree entry redesign (branch + sync + dirty)](2026-07-20-plan33-repo-tree-entry.md) | 2026-07-20 | done | product · ui |
| [Plan 34 — Submodule detail rows + right-aligned sync](2026-07-20-plan34-submodule-detail.md) | 2026-07-20 | done | product · core · ui |
| [Plan 35 — macOS native chrome & system menu bar](2026-07-06-plan35-macos-native-chrome.md) | 2026-07-06 | done | product · ui |
| [Plan 36 — Credentials & identity management (Phase 1: identity)](2026-07-06-plan36-identity.md) | 2026-07-06 | done | product · engineering · core · ui |
| [Plan 37 — Credentials Phase 2: keychain secrets, SSH keys, host tokens](2026-07-06-plan37-keychain-secrets.md) | 2026-07-06 | done | engineering · core · ui |
| [Plan 38 — Credentials Phase 3: forge validation + central UI](2026-07-06-plan38-forge-central-ui.md) | 2026-07-06 | done | product · engineering · ui |

> Migration note: these plans were authored under the earlier
> `docs/superpowers/` layout and carried over into this structure. The "Realises
> (spec)" column is the bridge from each plan to the living spec section that now
> owns its durable design.
