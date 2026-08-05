# Diff text selection and copy

| | |
|--|--|
| **Date** | 2026-08-05 |
| **Realises** | user request: "select and copy from the diff" |
| **Touches** | ui (new `DiffSelection` type, `DiffView.qml`, `CommitDetail.qml`, shared code-row component, theme tokens), design (selection colour tokens) |

## Goal

Let the user select diff text freely — character-accurate, across rows, like a
text editor — and copy it. Copy yields pasteable source by default, with an
explicit variant that keeps the `+`/`-` diff markers.

Selection must not cost the diff pane anything it already does: per-line staging
checkboxes, block checkboxes, the line-number gutter, conflict-resolution
buttons, and syntax highlighting all stay.

## Decisions

| Question | Decision |
|--|--|
| Granularity | **Free character selection across rows.** Not row-level selection. |
| Selectable columns | **Code column only.** Checkbox, line-number gutter and `+`/`−` sign columns are never selectable. |
| Copy content | **Code text only.** A second command copies the same selection prefixed with diff markers. |
| Where selection state lives | **A C++ `DiffSelection` object**, not in delegates. `ListView` destroys off-screen delegates; selection spans rows that may not exist as items. |
| How text is rendered | **Read-only `TextEdit` per row.** Only `TextEdit` offers `positionAt(x, y)` (pixel → character index) and native selection painting via `select(a, b)`. |
| Rejected: one big `TextEdit` | A single document for the whole diff would give selection for free but destroys per-line checkboxes, the gutter, block rows and conflict headers — the pane's primary function. |
| Rejected: monospace column math | Mapping x → column by fixed advance width is simple but wrong for tabs, CJK width, and whatever the platform resolves `"monospace"` to. |
| Scope | **Both diff surfaces**: `DiffView.qml` (working tree + stash preview) and `CommitDetail.qml` (commit diff). |

## Architecture

```
ui/include/gittide/ui/diffselection.hpp   DiffSelection : QObject
ui/src/diffselection.cpp
ui/src/qmlcontext.cpp                     qmlRegisterType<DiffSelection>("GitTide", 1, 0, "DiffSelection")
ui/qml/DiffCodeText.qml                   shared code-column item (TextEdit + selection wiring + hit area)
ui/qml/DiffView.qml                       uses DiffCodeText, owns a DiffSelection
ui/qml/CommitDetail.qml                   uses DiffCodeText, owns a DiffSelection
```

`DiffSelection` is instantiable from QML (precedent: `GraphColumn`). Each diff
list owns one; the two lists select independently.

### `DiffSelection`

State is a pair of (row, column) positions in *model* coordinates — row is a
model index, column a character offset into that row's `TextRole` string.

| Member | Purpose |
|--|--|
| `anchorRow`, `anchorCol`, `cursorRow`, `cursorCol` | raw drag endpoints, unnormalised |
| `hasSelection` | true when anchor ≠ cursor |
| `model` | the `QAbstractItemModel` being selected in; setting it clears |
| `begin(row, col)` | set anchor and cursor |
| `extendTo(row, col)` | move cursor (drag, Shift+click) |
| `selectWord(row, col)` | expand to the `[A-Za-z0-9_]` run around `col` |
| `selectLine(row)` | whole row |
| `selectAll()` | first row col 0 → last row end |
| `clear()` | drop selection |
| `startInRow(row)`, `endInRow(row)` | per-row highlight range, `-1` when the row is outside the selection |
| `copyText(bool withMarkers)` | assemble the selected text |

Rows are normalised internally, so a backwards drag (bottom-up, or right-to-left
within one row) yields the same result as the forward one.

`copyText` reads `TextRole` and `KindRole` from the model — never from the
rendered items. This keeps copy correct for rows that are scrolled out of view,
and immune to any difference between a row's rich-text document and its source
string.

Copy assembly:

- rows joined with `\n`, trailing `\n` appended
- first and last row contribute substrings; middle rows contribute in full
- synthetic `block` rows contribute nothing (not even an empty line)
- `hunk` header and conflict-marker rows contribute their text verbatim
- with markers: each contributing row is prefixed `+` (added), `-` (removed) or
  ` ` (everything else) followed by a space. ASCII `-`, not the display `−`.
  A partially selected first/last row still gets its prefix. `hunk` rows keep
  their raw `@@ … @@` text with no prefix.

### `DiffCodeText.qml`

Replaces the code-column `Label` in both delegates. Today those two delegates
are near-identical copies; this extraction removes the duplication.

- read-only `TextEdit`, `selectByMouse: false`, same font/colour/`textFormat`
  logic as the current `Label`
- `selectionColor: theme.selectionBg`, `selectedTextColor: theme.selectionText`
- binds `select(selection.startInRow(row), selection.endInRow(row))`, so a row
  recycled back into view repaints the correct highlight
- a `MouseArea` covering only this item — the checkbox, gutter and sign columns
  keep their current click behaviour

`TextEdit` has no `elide`. Long lines are hard-clipped without the `…` the
current `Label` shows. Accepted: copy still yields the full line, and horizontal
scrolling for long lines is a separate wish.

### Drag mechanics

On press the origin `MouseArea` grabs the mouse, so movement over other rows is
still delivered to it. Each move maps pointer position to a model position:

```
p    = mapToItem(list.contentItem, mouse.x, mouse.y)
row  = list.indexAt(p.x, p.y)
item = list.itemAtIndex(row)          // the row's DiffCodeText
col  = item.positionAt(mapped x, y)
```

Rows that are not code rows (conflict-start headers) are not drag targets;
dragging across them extends the selection through them by row index.

An autoscroll `Timer` runs while the pointer is past the viewport's top or
bottom edge, stepping `contentY` by an amount proportional to the overshoot.

## Interaction

| Input | Result |
|--|--|
| press–drag–release | select |
| click, no movement | clear |
| Shift+click | extend from current anchor |
| double-click | word |
| triple-click | whole row |
| right-click | context menu; selection unchanged |
| `Ctrl+A` | select the whole diff |
| `Ctrl+C` | copy code text |
| `Ctrl+Shift+C` | copy with diff markers |

`MouseArea` has no triple-click signal; it is detected with a click counter and
a 400 ms threshold in QML.

The diff list takes `activeFocus` on click and handles the shortcuts in
`Keys.onPressed`, so they apply only while the diff pane has focus.

Context menu (`AppMenu`): **Copy** · **Copy with Diff Markers** · **Select
All**. The two copy items are disabled when nothing is selected.

Selection clears on file switch, model reset, refresh, and entering or leaving
stash preview — `DiffSelection` connects to the model's reset signals rather
than relying on every call site to remember.

## Theme

Two new tokens in `Theme` / `QmlTheme`, light and dark:

| Token | Meaning |
|--|--|
| `selectionBg` | selection background behind selected code |
| `selectionText` | text colour inside the selection |

The invariant stands: no hex literal in a view.

## Testing

TDD — each test written failing first.

**`tests/ui/test_diff_selection.cpp`** (no window, pure logic)

- forward and backward drag normalise to the same range
- `startInRow`/`endInRow` for first, middle, last, and single-row selections;
  `-1` for rows outside
- `selectWord` at line start, line end, across punctuation, on an empty line
- `selectLine`, `selectAll`, `clear`
- selection clears on model reset
- `copyText` plain: substrings honoured, rows joined, trailing newline
- `copyText` with markers: `+`/`-`/` ` prefixes, ASCII hyphen, partial first/last
  row still prefixed
- `block` rows skipped; `hunk` rows verbatim and unprefixed

**`tests/ui/test_qml_diff_selection.cpp`** (headless `Main.qml`, pattern of
`test_qml_wheelscroller.cpp`)

- diff code rows exist with the expected object names in both surfaces
- driving `begin`/`extendTo` paints `selectionStart`/`selectionEnd` on visible
  rows
- `selectionColor` resolves from the theme token and changes with the theme
- a highlighted (rich-text) row selects at the same offsets as its `TextRole`
  string — guards the document-vs-source-offset risk below

New source `ui/src/diffselection.cpp` → `ui/CMakeLists.txt`; both test files →
the ui list in `tests/CMakeLists.txt`.

## Risks

1. **Rich text offsets.** Syntax-highlighted rows render HTML; `TextEdit`
   positions are document positions. If the highlighter's markup changes the
   plain-text length, painted highlight drifts from the source string. Copy is
   already immune (it reads the model), and a test pins the painting case.
2. **`TextEdit` cost.** Heavier than `Text`. Only visible rows are
   instantiated, but scroll smoothness on large diffs needs a look.
3. **Lost ellipsis** on clipped long lines, as above.

## Documentation to update on close

- `docs/spec/` — diff-view section gains the selection/copy flow; visual-system
  section gains the two selection tokens
- `docs/decisions.md` — why per-row `TextEdit` plus external selection state,
  and why the two rejected alternatives were rejected
- Doxygen on `DiffSelection` and `DiffCodeText.qml`
- a plan under `docs/plans/`
