# Keyboard controls — navigate and act without the mouse

| | |
|--|--|
| **Added** | 2026-06-21 |
| **Status** | `idea` |
| **Touches** | product (keyboard-driven flows across the shell), design (focus model, focus-ring affordance, shortcut discoverability), engineering (ui: QML `Keys`/`FocusScope`/`Shortcut` wiring on the existing models, **not** core) |

## What

Drive GitTide from the keyboard, so the common loop — review changes, stage,
write a message, commit — never needs the mouse. Concretely:

- **Arrow keys navigate lists.** Up/Down moves the selection through the
  **changed-files** list (the working-changes pane) and through the **history**
  commit list. The selected row drives the diff pane, exactly as a click does
  today.
- **Enter confirms / commits.** Enter activates the focused element: in the
  commit-message field it performs the commit (the primary action); in a list it
  opens/activates the selected item. The exact Enter semantics per pane need
  design (see Notes).
- **Space stages/unstages** the selected changed file (toggle), the natural
  companion to arrow navigation through the file list.
- **A discoverable, consistent shortcut set** for the high-frequency verbs —
  commit, refresh, focus-the-message-box, switch between the changed-files list
  and the history list — rather than ad-hoc per-view bindings.

Scope is **keyboard navigation + activation of flows that already exist**: no new
git capability, just a keyboard path to what the mouse can already do.

## Why

Today the shell is mouse-driven: selecting a changed file, picking a commit in
history, and committing all require pointing and clicking. For a developer tool
this is the single biggest day-to-day friction — the whole review-and-commit loop
is a keyboard loop in every comparable client (GitHub Desktop, GitKraken, magit,
the terminal). Arrow-to-navigate + Enter-to-commit turns a multi-click ritual into
a few keystrokes, makes the app feel fast and native, and is a prerequisite for
real accessibility. It's self-contained UI polish with no impact on the git
engine.

## Notes

- **Layering — this is a `ui/` concern, not `core/`.** Everything here is QML
  focus + key handling on top of the existing ViewModels/models
  (`RepoViewModel`, the changed-files and history models). No core changes; no Qt
  creeps into core.
- **Focus model needs design.** The shell has several focusable regions
  (changed-files list, history list, diff pane, commit-message field). Define one
  coherent focus model: a clear "active pane," a visible focus ring/selection
  highlight (a **design token**, not a hex literal), and a key to move focus
  between panes (Tab, or a chord). QML `FocusScope`/`activeFocus` is the
  mechanism; the *policy* (what's focusable, the tab order, what's focused on
  load) is the design work. Decide where focus lands when a project opens and after
  a commit.
- **Enter semantics — open question.** Enter must mean the obvious thing in each
  context: commit when the message box is focused; activate the selected row in a
  list. Avoid surprising commits — e.g. Enter in the message field commits, but
  newline-in-message needs an escape hatch (Shift+Enter for newline,
  Cmd/Ctrl+Enter to commit, as GitHub Desktop does). Pin this down in the product
  spec.
- **Cross-platform modifiers.** Use Qt's `StandardKey`/portable shortcut
  declarations so Cmd-on-macOS vs Ctrl-elsewhere is handled correctly; don't
  hard-code one platform's modifier.
- **Discoverability.** Shortcuts only help if they're findable — surface them
  (tooltips with the key hint, a menu, and/or a `?` shortcut overlay). Worth a
  small design pass so the binding set is documented in one place rather than
  scattered.
- **Scope creep to resist.** No full command palette, no vim-style modal editing,
  no user-rebindable keymap for the first cut — a fixed, well-chosen default set.
  Those are natural *follow-on* wishes once the focus model exists.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (keyboard flows + Enter semantics), spec/design (focus model + focus-ring token + shortcut discoverability), spec/engineering (ui focus/Keys wiring) · plan: plans/<file>
- Enter-to-commit vs newline escape hatch, and the focus model → log in decisions.md
-->
