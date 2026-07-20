# Code syntax highlighting in diffs

| | |
|--|--|
| **Added** | 2026-06-21 |
| **Status** | `done` |
| **Shipped** | 2026-06-30 |
| **Designed in** | [spec/product/2026-06-30-diff-syntax-highlighting-design.md](../../spec/product/2026-06-30-diff-syntax-highlighting-design.md) · decisions D45 (KSyntaxHighlighting + bundled themes), D46 (rich-text-per-line) |
| **Plan** | [plans/2026-06-30-plan30-diff-syntax-highlighting.md](../../plans/2026-06-30-plan30-diff-syntax-highlighting.md) |
| **Touches** | product (richer diff readability), design (token colours for syntax on top of add/remove backgrounds), engineering (ui-side highlighter feeding `DiffLinesModel`/`DiffView` — **not** core) |

## What

Render each diff line's code with **language-aware syntax colouring** — keywords,
strings, comments, numbers, types — instead of flat monospace text. The
add/remove gutter colouring stays; syntax colour layers *on top* of it, so a
green added line still reads as added but its code is also highlighted.

- Highlighting applies in both diff modes: the editable working-changes diff and
  the read-only history diff.
- Language is detected from the file's extension (the diff already knows the path).
- Unknown/unsupported languages fall back to today's plain monospace rendering —
  never worse than now.

## Why

Reading code is far easier when it's coloured. Right now every diff line is
undifferentiated monospace text; the only colour is the add/remove background.
Every comparable client (GitHub Desktop, GitKraken, VS Code) highlights diff
code, and it materially speeds up scanning a change — you spot the string literal,
the changed identifier, the comment, at a glance. It's a high-perceived-quality,
self-contained polish item with no impact on the git engine.

## Notes

- **Layering — this is a `ui/` concern, not `core/`.** Core's diff types
  (`DiffLine`/`DiffHunk`/`DiffResult` in `core/include/gittide/diff.hpp`) carry
  the line *text* and origin; that's all core owes. Tokenising that text into
  coloured spans is a Qt/UI job — keep it out of core (no Qt in core; core stays
  deterministic/offline for tests). The natural seam is between `DiffLinesModel`
  (`ui/include/gittide/ui/difflinesmodel.hpp`) and `DiffView.qml`
  (`ui/qml/DiffView.qml`), which today render `lineText` as a plain string.
- **How to colour — open question.** Two shapes:
  - **Rich-text per line** — produce HTML/`QTextDocument` markup with colour
    spans and bind it to a `Text { textFormat: Text.RichText }`. Simplest to drop
    into the existing per-line `ListView`, but rich text per row has a cost on
    large diffs (the view is virtualized, so only visible rows pay it).
  - **Token model** — expose spans (start, length, token-class) as a role and let
    the delegate paint them. More work, more control, better for huge diffs.
  Start with rich-text-per-line and measure; switch only if virtualized rendering
  stutters.
- **Highlighter source.** Options: a small hand-rolled lexer for a few common
  languages (C/C++, JS, Python, JSON, Markdown) — minimal dependency, limited
  coverage; or a library (e.g. KSyntaxHighlighting, or a bundled grammar set) —
  broad coverage, heavier dependency to vet against the build. Decide based on how
  many languages we want day one vs. dependency appetite — log in
  [`decisions.md`](../../decisions.md).
- **Colours come from theme tokens, not hex literals** (per the invariant). Syntax
  classes (keyword/string/comment/number/type/…) need their own token set in the
  design system, defined for both light and dark themes, sitting *above* the
  `state.added`/`state.deleted` backgrounds with enough contrast on each.
- **Performance.** Highlight lazily — only for the lines the virtualized
  `DiffView` actually renders — and cache per line so scrolling doesn't re-tokenise.
  A multi-thousand-line diff must stay smooth. Intra-line (word-level) diff
  highlighting is a *separate* concern and out of scope here.
- **Scope creep to resist.** No editing/IntelliSense, no full-file semantic
  analysis, no per-user theme editing. Lexical (regex/grammar) highlighting only,
  driven by file extension, with a clean plain-text fallback.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (highlighted diff), spec/design (syntax token set + layering over add/remove), spec/engineering (ui highlighter + DiffLinesModel/DiffView seam) · plan: plans/<file>
- Highlighter approach (hand-rolled lexer vs library) and rich-text-vs-token-model → log in decisions.md
-->
