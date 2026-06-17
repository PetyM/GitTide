# Workflow

How work moves through GitTide — for the AI agent and for humans. The artifacts
([`wishlist/`](wishlist/index.md), [`spec/`](spec/spec.md),
[`plans/`](plans/index.md)) are the nouns; this is the verbs. It is the
superpowers cadence (brainstorm → spec → plan → execute) adapted to a *living*
spec.

## The loop

```
wishlist/ (wanted)  →  spec/ (designed)  →  plans/ (built)  →  spec stays true
```

1. **Capture.** A new idea is a [wish](wishlist/index.md) — one file, *what* and
   *why*. Don't design it yet.
2. **Design.** When a wish is taken on, design it **in the spec first** — the
   relevant [`product`](spec/product/product.md) /
   [`engineering`](spec/engineering/engineering.md) /
   [`design`](spec/design/design.md) section. Resolve the open questions here. If
   a choice is significant or rejects real alternatives, log it in
   [`decisions.md`](decisions.md).
3. **Plan.** Decompose the spec change into a [plan](plans/index.md) (from
   [`TEMPLATE.md`](plans/TEMPLATE.md)), task-by-task and test-first. Set its
   `Status`, and link the spec section it *realises*.
4. **Build.** Execute the plan task by task, **TDD**: failing test → make it pass →
   refactor. Conform [code style](spec/engineering/code-style.md) on every file you
   touch; fix every warning.
5. **Close.** The plan is done when the spec reflects reality, tests are green, the
   plan's checkboxes are ticked, its **Outcome** is filled, and its `Status` flips
   to `done` in the index.

## When to ask the user vs. decide yourself

- **Ask** when the answer is genuinely the user's: product scope or behaviour,
  anything irreversible or outward-facing, or a fork where the choice changes the
  design and can't be inferred from the request, the code, or sensible defaults.
- **Decide** when it's a conventional default, verifiable in the code, or
  mechanical. Pick the obvious option, state it, and move on — don't stall on a
  choice with a clear default.

## Definition of Done

A task or plan is done when **all** of these hold:

- [ ] The test was written **first** and is green — Catch2 (core) and/or QtTest
      (ui), per [testing](spec/engineering/testing.md).
- [ ] No new compiler warnings and no Qt runtime warnings — a warning is a bug
      `(907)`.
- [ ] [Code style](spec/engineering/code-style.md) conformed for every file
      touched (clang-format + naming).
- [ ] The **spec** is updated to match the new behaviour/design — the living-spec
      contract in [`spec.md`](spec/spec.md).
- [ ] The plan's checkboxes are ticked; on the final task, its **Outcome** is
      filled and `Status` set to `done`.

## Commits & git

- **Conventional-commits style:** `type(scope): subject` — `feat` / `fix` /
  `refactor` / `docs` / `test` / `chore`; scope is the area (`core`, `ui`, `app`,
  `build`, `docs`). Imperative subject. Matches the existing history.
- **Atomic commits:** one logical change each. Keep a **rename separate from
  content edits** `(739)` (rename first via `git mv`, then edit) so git preserves
  file history — see [code style](spec/engineering/code-style.md).
- **Commit/push only when asked.** Don't push unprompted.
