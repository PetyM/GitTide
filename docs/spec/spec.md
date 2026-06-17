# GitTide — Specification

GitTide is a cross-platform (macOS / Windows / Linux) desktop git client built
around a first-class **Project** concept layered over many repositories — group
repos into a project, switch fast, see an aggregated status dashboard, stage at
file/hunk/line granularity, and read history as a commit graph.

This directory is the **living specification**: the durable description of what
GitTide is and how it is built — the design we commit to, and (for shipped
features) the current truth. It is organised by domain, not by chronology, and
it is the centre of a three-stage lifecycle:

```
../wishlist/ (wanted)   →   spec/ (designed)   →   ../plans/ (built)
```

A rough idea starts as a [wish](../wishlist/index.md). When taken on, it is
**designed here first** — the spec is where the design lands. A
[plan](../plans/index.md) then *realises* that spec section task-by-task. So the
spec leads implementation and outlives it: it states the intended design up
front and remains the source of truth after the plan is done.

The process that moves work through this lifecycle is in
[`../workflow.md`](../workflow.md); the rationale and rejected alternatives behind
major choices are logged in [`../decisions.md`](../decisions.md).

## How the spec is organised

| Section | Question it answers | Start here |
|---------|--------------------|------------|
| [`product/`](product/product.md) | What does GitTide do? Screens, flows, the Project/Repo model, scope. | `product/product.md` |
| [`engineering/`](engineering/engineering.md) | How is it built? Layering, dependency rules, async model, build/test, cross-cutting invariants. | `engineering/engineering.md` |
| [`design/`](design/design.md) | What does it look like? Visual tokens, theming, components, branding. | `design/design.md` |

Each section's top-level `*.md` is the overview and index for that domain; it
links out to further markdown files and subfolders as a topic grows. Start at
the overview, follow links down.

## The maintenance contract

The spec is the source of truth for **cross-cutting design** — the things that
span many files and that you cannot reconstruct by reading any single one
(layering, invariants, the threading model, user flows, the visual system, and
the *why* behind them).

It is **not** an API reference. Symbol-level facts — what a class is for, what a
function's parameters mean, the contract of an individual type — live in
**Doxygen comments next to the code**, which is where they stay correct. The
spec points to the code; it never restates per-method documentation.

So, when you change something:

- **Behaviour or architecture changed** (a new flow, a new layer boundary, a
  changed invariant, a new design token) → update the relevant section here in
  the same change.
- **A single symbol's contract changed** → update its Doxygen comment in the
  code; the spec usually needs nothing.
- **The code and this spec disagree** → the code is ground truth. Fix the spec,
  and note the drift so it does not recur. (This is the tiebreaker for
  *unintended drift* — new design is still authored here first; the spec leads,
  per the lifecycle above.)

Write in the present tense and describe the current design — not the history of
how it got here. History belongs in `../plans/`.

## Global invariants (index)

The non-negotiables, defined in full in [`engineering/`](engineering/engineering.md):

- **Layering, downward-only.** `app → ui → core → libgit2`. No upward deps.
- **No Qt in `core/`.** Core is pure C++23 and speaks `std` types
  (`std::string` UTF-8, `std::filesystem::path`, `std::expected`). Qt appears
  only at the ViewModel boundary.
- **libgit2 and nlohmann/json are PRIVATE to `core/`** — no public header leaks
  them.
- **One owner per `GitRepo`.** Not thread-safe; concurrency is achieved by
  giving each worker its own repo instance, never by sharing one.
- **Errors are values.** `std::expected<T, GitError>`; no exceptions across
  layer boundaries.
- **Paths via `generic_u8string()`**, never `.string()` (see the path-handling
  rule in `engineering/`).
- **Colour comes from a token**, never a hex literal in a widget (see
  `design/`).
