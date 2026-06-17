# Plan N — <Short title>

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | YYYY-MM-DD |
| **Status** | `planned` \| `in-progress` \| `done` |
| **Spec** | links into [`../spec/`](../spec/spec.md) this plan realises, e.g. `spec/engineering/engineering.md §Async` |
| **Depends on** | prior plans this builds on (or `—`) |

**Goal:** One or two sentences — the user-visible or structural outcome.

**Architecture:** Where this lands in the layering, the key new types, and the
one or two decisions that shape the rest. Keep it short; detail lives in the
tasks.

**Tech stack:** Language/library specifics this plan leans on.

## Global constraints

- Invariants this plan must not break (link to `spec/engineering/engineering.md`).
- New `core/` sources → `core/CMakeLists.txt`; new `ui/` sources →
  `ui/CMakeLists.txt`; new tests → the matching list in `tests/CMakeLists.txt`.
- Anything that must keep passing (named tests, object names, public contracts).

---

## Task 1: <name>

**Files:** Create / Modify / Test — list the exact paths.

**Interfaces:** the signatures this task produces (so later tasks can depend on
them without reading the body).

- [ ] **Step 1: Write the failing test** — what it asserts, and why it fails now.
- [ ] **Step 2: Make it pass** — the smallest implementation.
- [ ] **Step 3: Refactor / verify** — clean up; run the suite.

## Task 2: <name>

…repeat…

---

## Outcome

> Fill in when the plan reaches `done`. The durable result — what shipped and
> **where it now lives** in the code and the living spec. This is the bridge a
> future reader follows from "this plan" to "the current truth":
>
> - Shipped: <summary>.
> - Spec updated: <which `spec/` sections now describe this>.
> - Code: <the main files/types that resulted>.
