# Diff syntax highlighting — design

| | |
|---|---|
| **Status** | `shipped` |
| **Date** | 2026-06-30 |
| **Wish** | [diff-syntax-highlighting](../../wishlist/diff-syntax-highlighting.md) |
| **Related** | [spec/design/design.md](../design/design.md) (visual system), [spec/engineering/engineering.md](../engineering/engineering.md) (layering) |

## Problem

Every diff line renders as flat monospace text. The only colour is the
add/remove gutter background and the full-line green/red applied to changed
lines. Reading code is much easier when keywords, strings, comments, numbers and
types are coloured — every comparable client (GitHub Desktop, GitKraken, VS Code)
does it. This is a high-perceived-quality, self-contained `ui/` polish item with
no impact on the git engine.

## Goals

- Language-aware syntax colouring on diff lines, in **both** diff modes (editable
  working-changes diff and read-only history diff).
- Language detected from the file's extension (the diff already knows the path).
- Unknown/unsupported languages fall back to **exactly today's** rendering — never
  worse than now.
- Add/remove signalling preserved: a green added line still reads as added.

## Non-goals

- No editing / IntelliSense / semantic analysis. Lexical highlighting only.
- No intra-line (word-level) diff highlighting — separate concern.
- No per-user theme editing.
- Conflict `ours`/`theirs` rows are **not** highlighted in v1 (separate
  `setConflictContent` path).

## Decisions

| Question | Decision |
|---|---|
| Highlighter source | **KSyntaxHighlighting** (KDE Frameworks, Qt-native, ~300 grammars + stateful line engine). Only turnkey grammar library for Qt; `QSyntaxHighlighter` is a rules-only base class tied to QWidgets/`QTextDocument`. |
| Render shape | **Rich-text per line.** Precompute HTML markup per line at `setDiff()`, expose as a model role, bind to `Text { textFormat: Text.RichText }`. Switch to a token-span model only if virtualized rendering stutters. |
| Colour source | **KSyntax bundled themes** (built-in light/dark), switched with the app's theme mode. *Not* derived from our design tokens — see D45 rationale. |
| Add/remove text colour | When highlighted, **drop** the full-line green/red text colour; the existing 0.12-alpha row background + `+`/`−` sign carry the add/remove signal. Matches GitHub/VS Code. |

## Architecture

`app → ui → core`. This change is entirely in `ui/`; `core/` and its diff types
are untouched. KSyntaxHighlighting is a `ui/`-private dependency.

### 1. `SyntaxHighlighter` helper (new, `ui/`)

A thin wrapper owning a shared `KSyntaxHighlighting::Repository` (expensive to
build — constructed once) and a subclass of `KSyntaxHighlighting::AbstractHighlighter`.

- Input: a `Definition` (resolved from the file path via
  `repository.definitionForFileName(path)`) and an ordered list of lines.
- The `AbstractHighlighter::applyFormat(offset, length, format)` override collects
  spans; the helper assembles one HTML string per line: HTML-escaped text wrapped
  in `<span style="color:#RRGGBB">` from each `Format`'s colour under the active
  `Theme`.
- Definition not found → returns empty strings (fallback signal).

### 2. Statefulness — per-hunk, per-side

KSyntax highlighting carries state line→line (e.g. a multi-line `/* */` block). A
diff interleaves removed/added/context lines and **skips** lines between hunks, so
naïve sequential highlighting is wrong. Approach:

- Per hunk, build **two streams**: the *old* side (context + removed lines, in
  order) and the *new* side (context + added lines, in order).
- Highlight each stream sequentially with its own KSyntax state; **reset state at
  each hunk boundary** (gaps between hunks can open/close constructs we can't see).
- Map the per-stream results back to their rows.

This is best-effort with the same limitation every diff viewer has (cross-hunk
gaps reset state). Context lines are highlighted from whichever stream; they are
identical text on both sides.

### 3. `DiffLinesModel` changes

- After building rows, `setDiff()` runs the highlight pass and stores the HTML on
  each **code** row (`context` / `added` / `removed`).
- New `HtmlRole` exposed as `lineHtml`. **Empty** for `hunk` / `block` / conflict
  rows and for unsupported languages.
- `TextRole` (`lineText`) stays — fallback and plain rows still use it.

### 4. `DiffView.qml` delegate

The code `Label` branches:

- `lineHtml` non-empty → `textFormat: Text.RichText`, `text: model.lineHtml`, no
  colour override (colours live in the HTML spans).
- else → **today's path**: plain `text: model.lineText` with the per-kind colour
  (green/red/muted). So unsupported languages render exactly as today.

`elide: Text.ElideRight` is retained for the plain-text fallback. Qt's `Text`
does **not** elide rich text, so the RichText branch carries `clip: true`
instead: highlighted lines hard-clip at the row width rather than overflowing
(no `…`). A minor behaviour difference from plain lines, accepted for v1.

### 5. Build

`FetchContent` for `extra-cmake-modules` (ECM) and `KSyntaxHighlighting` in
`cmake/Dependencies.cmake`, linked **PRIVATE** to the `ui` target only. **Risk to
vet at build time:** KSyntax pulls ECM and adds CI/Windows build weight. If
FetchContent proves painful, the fallback is a small hand-rolled lexer for a few
languages — but that is only learned at build time; this design assumes KSyntax
builds.

## Performance

Highlighting is precomputed synchronously inside `setDiff()` and cached on the
rows (no re-tokenising on scroll; the `ListView` is virtualized so only visible
rows pay HTML layout). Lexing is O(lines) and fast for typical diffs. Background
offload is **not** done in v1 — measure first; only move highlighting off the UI
thread if multi-thousand-line diffs stutter.

## Testing (TDD)

- **`SyntaxHighlighter`**: a known C++ snippet + the cpp definition produces HTML
  containing colour `<span>`s and correctly escapes `<`, `>`, `&`; an unknown
  extension yields empty strings.
- **`DiffLinesModel`**: `HtmlRole` is populated for code rows and empty for
  `hunk` / unsupported-language rows.
- **Headless QML smoke**: `DiffView` renders a RichText row without error.

Tests must degrade gracefully if KSyntax definitions are unavailable in the test
environment (assert the fallback path rather than failing).

## Decisions log (to add to `docs/decisions.md`)

- **D45 — Diff syntax highlighting uses KSyntaxHighlighting with its bundled
  themes**, not colours derived from the design-token set. *Why:* KSyntax is the
  only turnkey Qt grammar library; authoring + maintaining a full syntax-class
  token set (keyword/string/comment/number/type/…) for both themes is
  disproportionate to the payoff, and KSyntax themes are data-driven (not
  hex-literals-in-widgets), so the spirit of D18 (no hex in widgets) holds. The
  bundled light/dark themes switch with the app mode. *Revisit* if the bundled
  palette clashes badly with the app — a custom KSyntax theme JSON derived from
  our tokens is the upgrade path.
- **D46 — Rich-text-per-line over a token-span model** for rendering coloured
  diff lines. *Why:* simplest drop-in for the existing per-line `ListView`;
  virtualization bounds the cost. Switch to a token-span role + custom paint only
  if measured scrolling stutters.
