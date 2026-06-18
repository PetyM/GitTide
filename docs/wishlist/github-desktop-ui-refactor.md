# GitHub-Desktop-style UI refactor

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `done` |
| **Touches** | product (screens & flows), design (visual system), engineering (ui layer, possible framework change) |

## What

Reshape the main UI to feel like GitHub Desktop — cleaner, more modern, fairly
minimalist, but keeping every essential element. Concretely:

- **Drop the staging area.** No more staged/unstaged file lists. Instead, files
  carry **checkboxes**, and individual changes inside the diff (hunks / lines)
  carry checkboxes too. What's checked is what gets committed — staging becomes an
  implicit, inline selection rather than a separate place files live.
- **Merge History into the diff view.** History stops being its own tab; it shares
  a view with the diff (select a commit → its diff shows in the same panel that
  shows working-changes diffs).
- **Remove the Dashboard entirely.**
- **Consider moving off QWidgets** — to QML, or another GUI framework — if that
  better serves a clean, modern look. Open question, not a decision.

The end state: a clean modern GUI, minimalist, with all the substantive controls
still present.

## Why

The current three-surface layout (Changes tab with an explicit staging area,
separate History tab, separate Dashboard) is more ceremony than a focused git
client needs. GitHub Desktop's inline-checkbox model is faster and more legible:
you see your changes and tick exactly what to commit, in one place, without the
mental model of a staging "area". Folding history into the diff view removes a
tab and unifies "look at a diff" into a single surface. Dropping the dashboard
removes a screen that isn't pulling its weight. Net: fewer surfaces, less chrome,
a more obviously modern app.

## Notes

- This contradicts several things in the current [product spec](../spec/product/product.md):
  the **Changes tab** staging lists, the separate **History tab**, and the
  **Dashboard** are all described there. Graduating this wish means rewriting
  those sections (and likely the "Screens & navigation" overview).
- **Checkbox semantics — open question.** With no staging area, what does a tri-state
  file checkbox mean when only some hunks are checked? How is the commit built from
  the checked set — stage-on-commit under the hood, or commit a constructed tree?
  Core already supports file/hunk/line granularity (`stage/unstage/discard`); the
  question is the UI contract on top of it, and whether the underlying git index
  flow stays the same.
- **QML vs QWidgets — the big fork.** This is a significant architectural choice
  that rejects a real alternative, so when it graduates it belongs in
  [`decisions.md`](../decisions.md). Weigh against current invariants: Qt lives
  only at the ViewModel boundary, `core/` is Qt-free. QML would change how the
  `ui/` layer and the `AsyncRepo` bridge are wired, and how tests
  (QtTest/headless) run. A non-Qt framework would be an even larger blast radius.
  Could also be done as two wishes: the UX refactor (independent of toolkit) and
  the toolkit migration.
- The **Project** concept (multi-repo grouping) is GitTide's differentiator vs
  GitHub Desktop and must survive the refactor — the project switcher and repo
  tree stay even as the main area is reshaped.
- Pairs with the visual theme work already in the spec/design — reuse the existing
  theme tokens; colour still comes from a token, never a hex literal.

---

## Graduated

Designed into the living spec (2026-06-18):

- **Product** — [`product.md`](../spec/product/product.md): Screens & navigation
  (collapsible Project sidebar + list column + shared diff panel), the rewritten
  **Changes** tab (default-checked checkboxes, no staging area, commit from the
  checked set, discard via context menu), the rewritten **History** tab (commit
  list + commit-files list feeding the shared read-only diff), Dashboard removed.
- **Design** — [`design.md`](../spec/design/design.md): Fusion base style with a
  token-driven `QPalette` + accent stylesheet; `changedFilesList` and diff
  line-checkbox component contracts; sidebar collapse.
- **Engineering** — [`engineering.md`](../spec/engineering/engineering.md):
  "Inline selection, commit, and the history diff" — stage-on-commit flow and the
  new core endpoints (reset index to `HEAD`; list a commit's files; diff a file in
  a commit).
- **Decisions** — [`decisions.md`](../decisions.md): D22 (UI model), D23
  (stage-on-commit), D24 (QWidgets reaffirmed + Fusion), D25 (palette over QSS).

Toolkit: the QML fork was **rejected** — stays QWidgets (D24).

Plan: see [`plans/`](../plans/index.md).
