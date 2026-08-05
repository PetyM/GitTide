# Diff text selection and copy

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-08-05 |
| **Status** | `done` |
| **Spec** | [`specs/2026-08-05-diff-text-selection-design.md`](../specs/2026-08-05-diff-text-selection-design.md); on close, `spec/design`, `spec/product/keyboard-controls.md`, `spec/product/context-menus.md` |
| **Depends on** | — |

**Goal:** Let the user select diff text character-accurately across rows and copy it — plain code by default, `+`/`-` marker-prefixed on an explicit second command — in both the working-tree diff and the commit diff.

**Architecture:** A new `ui/` type `DiffSelection` (QObject, registered to QML) holds the selection as `(row, column)` pairs in *model* coordinates, because the `ListView` destroys off-screen delegates and a selection routinely spans rows that no longer exist as items. Three new QML files consume it: `DiffCodeText.qml` (a read-only `TextEdit` per row that paints its slice of the selection), `DiffSelectionOverlay.qml` (a `MouseArea` **sibling** of the list carrying drag, autoscroll, shortcuts and menu — a delegate-owned handler would lose its mouse grab the moment autoscroll recycles the row it started on), and `DiffContextMenu.qml`. `DiffView.qml` and `CommitDetail.qml` each declare one selection + one overlay.

**Tech stack:** C++23, Qt 6 Quick/QML (`TextEdit.positionAt` / `select`), QTest (ui suite).

## Global constraints

- **No Qt in `core/`** — this plan touches `ui/` only; `core/` is not modified.
- **Colour from a `theme` token**, never a hex literal in QML.
- **Errors are values**; no exceptions across layers.
- **Code style:** Allman braces via `.clang-format`; `m_` members; lowercase file names; KISS/DRY/SOLID/YAGNI. Doxygen for symbol-level facts — never restate them in the spec.
- **TDD:** failing test first, then the smallest implementation.
- New `ui/` sources → `ui/CMakeLists.txt`. New `ui/qml/*.qml` → `ui/qml/qml.qrc`. New tests → `gittide_ui_test_sources` in `tests/CMakeLists.txt` **and** an `#include` + `RUN(...)` line in `tests/ui/main.cpp` — both edits, or the test compiles and silently never runs.
- **Headless harness limit:** the offscreen platform never renders a frame, so **`ListView` delegates are never instantiated** in tests (see the comment above `history_delegate_has_tap_handler_not_mouse_area` in `tests/ui/test_qml_history.cpp`). No test may reach a diff row through a real list. Component-level instantiation (`test_qml_appcontrols.cpp` pattern) and a stub list object are the only ways in.
- **Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`. UI suite: `QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`. Per-class output is prefixed `[ui-test] running <Class>`. A single class: `QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests <slotName>` runs that slot in **every** class, so filter the log by the `[ui-test] running` line.
- **Must keep passing:** `TestDiffLinesModel`, `TestDiffLinesModelConflict`, `TestQmlShell`, `TestQmlTheme`, `TestTheme`.
- **Must not regress:** per-line staging checkboxes, the block checkbox, the conflict Accept buttons, and the line-number gutter all keep working in `DiffView.qml`.

---

## File structure

**New:**
- `ui/include/gittide/ui/diffselection.hpp` — `DiffSelection` declaration + Doxygen.
- `ui/src/diffselection.cpp` — its implementation.
- `ui/qml/DiffCodeText.qml` — one row's code column; presentational.
- `ui/qml/DiffSelectionOverlay.qml` — pointer/keyboard selection surface over a diff list.
- `ui/qml/DiffContextMenu.qml` — Copy · Copy with Diff Markers · Select All.
- `tests/ui/test_diff_selection.cpp` — `DiffSelection` logic.
- `tests/ui/test_qml_diff_selection.cpp` — the three QML components + both wirings.

**Modified:**
- `ui/include/gittide/ui/theme.hpp`, `ui/src/theme.cpp` — two selection tokens.
- `ui/include/gittide/ui/qmltheme.hpp`, `ui/src/qmltheme.cpp` — expose them.
- `ui/src/qmlcontext.cpp` — register `DiffSelection` as a QML type.
- `ui/CMakeLists.txt`, `ui/qml/qml.qrc`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`.
- `ui/qml/DiffView.qml`, `ui/qml/CommitDetail.qml` — use the new components.
- `tests/ui/test_theme.cpp`, `tests/ui/test_qml_theme.cpp` — token assertions.
- `docs/spec/design/design.md`, `docs/spec/product/keyboard-controls.md`, `docs/spec/product/context-menus.md`, `docs/decisions.md`.

---

## Task 1: Selection colour tokens

**Files:**
- Modify: `ui/include/gittide/ui/theme.hpp`, `ui/src/theme.cpp`, `ui/include/gittide/ui/qmltheme.hpp`, `ui/src/qmltheme.cpp`
- Modify: `docs/spec/design/design.md` (token table, near the `shadow` row at ~line 30)
- Test: `tests/ui/test_theme.cpp`, `tests/ui/test_qml_theme.cpp`

**Interfaces:**
- Produces: `Theme::selectionBg`, `Theme::selectionText` (both `QString`); `QmlTheme` properties `selectionBg`, `selectionText` (both `QColor`), usable from QML as `theme.selectionBg` / `theme.selectionText`.

- [ ] **Step 1: Write the failing tests**

In `tests/ui/test_theme.cpp`, add to `class TestTheme`:

```cpp
    void selection_tokens_are_translucent_accent()
    {
        // Selection paints *behind* syntax-highlighted code, so it must be
        // translucent — an opaque fill would flatten the highlighting away.
        QCOMPARE(darkTheme().selectionBg, QStringLiteral("#5942A5F5"));
        QCOMPARE(darkTheme().selectionText, QStringLiteral("#E4E4E6"));
        QCOMPARE(lightTheme().selectionBg, QStringLiteral("#401976D2"));
        QCOMPARE(lightTheme().selectionText, QStringLiteral("#212121"));
        QVERIFY(QColor(darkTheme().selectionBg).alpha() < 255);
        QVERIFY(QColor(lightTheme().selectionBg).alpha() < 255);
    }
```

In `tests/ui/test_qml_theme.cpp`, add to `class TestQmlTheme`:

```cpp
    void selection_tokens_are_exposed_to_qml()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QCOMPARE(theme.property("selectionBg").value<QColor>(), QColor("#5942A5F5"));
        QCOMPARE(theme.property("selectionText").value<QColor>(), QColor("#E4E4E6"));

        mgr.setMode(ThemeManager::Mode::Light);
        QCOMPARE(theme.property("selectionBg").value<QColor>(), QColor("#401976D2"));
        QCOMPARE(theme.property("selectionText").value<QColor>(), QColor("#212121"));
    }
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — `Theme` has no member `selectionBg`.

- [ ] **Step 3: Add the tokens**

`ui/include/gittide/ui/theme.hpp`, inside `struct Theme` after `focusBorder`:

```cpp
    // Text selection in the diff view. selectionBg is deliberately translucent so
    // syntax highlighting still reads through a selected run (#AARRGGBB).
    QString selectionBg, selectionText;
```

`ui/src/theme.cpp`, in `darkTheme()` after `.focusBorder`:

```cpp
        .selectionBg    = QStringLiteral("#5942A5F5"), // 35% accent
        .selectionText  = QStringLiteral("#E4E4E6"),   // = textPrimary
```

and in `lightTheme()`:

```cpp
        .selectionBg    = QStringLiteral("#401976D2"), // 25% accent
        .selectionText  = QStringLiteral("#212121"),   // = textPrimary
```

`ui/include/gittide/ui/qmltheme.hpp` — one `Q_PROPERTY` next to `focusBorder`'s:

```cpp
    Q_PROPERTY(QColor selectionBg READ selectionBg NOTIFY changed)
    Q_PROPERTY(QColor selectionText READ selectionText NOTIFY changed)
```

and in the public getter list:

```cpp
    QColor selectionBg() const;
    QColor selectionText() const;
```

`ui/src/qmltheme.cpp` — next to the other colour getters:

```cpp
QColor QmlTheme::selectionBg() const
{
    return QColor(theme().selectionBg);
}

QColor QmlTheme::selectionText() const
{
    return QColor(theme().selectionText);
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS, `TestTheme` and `TestQmlTheme` green.

- [ ] **Step 5: Document the tokens**

In `docs/spec/design/design.md`, add to the token table that holds the `shadow` row:

```markdown
| `selectionBg`    | `#5942A5F5` | `#401976D2` | Diff text selection fill — translucent so syntax colour reads through |
| `selectionText`  | `#E4E4E6`   | `#212121`   | Text inside a selection (= `text.primary`) |
```

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/theme.hpp ui/src/theme.cpp \
        ui/include/gittide/ui/qmltheme.hpp ui/src/qmltheme.cpp \
        tests/ui/test_theme.cpp tests/ui/test_qml_theme.cpp docs/spec/design/design.md
git commit -m "feat(theme): add selection colour tokens for diff text selection"
```

---

## Task 2: `DiffSelection` — state, ordering, per-row ranges

**Files:**
- Create: `ui/include/gittide/ui/diffselection.hpp`, `ui/src/diffselection.cpp`
- Modify: `ui/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`
- Test: `tests/ui/test_diff_selection.cpp`

**Interfaces:**
- Consumes: `DiffLinesModel` (existing) purely as a `QAbstractItemModel` with role names `lineText` and `lineKind`.
- Produces: `gittide::ui::DiffSelection` with properties `model` (`QAbstractItemModel*`, read/write), `hasSelection` (bool), `anchorRow` (int); invokables `begin(int,int)`, `extendTo(int,int)`, `selectAll()`, `clear()`, `startInRow(int) -> int`, `endInRow(int) -> int`, `rowLength(int) -> int`; signals `modelChanged()`, `selectionChanged()`. Tasks 3 and 4 add `copyText`, `selectWord`, `selectLine` to this same class.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_diff_selection.cpp`:

```cpp
// Tests for DiffSelection — the character-accurate, cross-row text selection
// behind the diff view. Pure logic over a DiffLinesModel; no window involved.

#include <QtTest>
#include <QSignalSpy>

#include "gittide/diff.hpp"
#include "gittide/ui/diffselection.hpp"
#include "gittide/ui/difflinesmodel.hpp"

using gittide::ui::DiffLinesModel;
using gittide::ui::DiffSelection;

namespace
{
// One hunk: "@@ -1,2 +1,2 @@" / "ctx one" / "added two" / "removed three".
// Rows: 0 = hunk header, 1 = context, 2 = added, 3 = removed.
gittide::DiffResult sampleDiff()
{
    gittide::DiffLine ctx;
    ctx.origin    = gittide::DiffLineOrigin::Context;
    ctx.oldLineno = 1;
    ctx.newLineno = 1;
    ctx.text      = "ctx one";

    gittide::DiffLine added;
    added.origin    = gittide::DiffLineOrigin::Added;
    added.oldLineno = -1;
    added.newLineno = 2;
    added.text      = "added two";

    gittide::DiffLine removed;
    removed.origin    = gittide::DiffLineOrigin::Removed;
    removed.oldLineno = 2;
    removed.newLineno = -1;
    removed.text      = "removed three";

    gittide::DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 2;
    h.newStart = 1;
    h.newLines = 2;
    h.lines    = {ctx, added, removed};

    gittide::DiffResult r;
    r.hunks = {h};
    return r;
}
} // namespace

class TestDiffSelection : public QObject
{
    Q_OBJECT
private slots:

    void no_selection_before_any_press()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        QVERIFY(!sel.property("hasSelection").toBool());
        QCOMPARE(sel.startInRow(1), -1);
        QCOMPARE(sel.endInRow(1), -1);
    }

    void a_press_without_a_drag_is_not_a_selection()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 3);
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void forward_drag_covers_partial_first_and_last_rows()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 4);   // "ctx |one"
        sel.extendTo(3, 7); // "removed| three"

        QVERIFY(sel.property("hasSelection").toBool());
        QCOMPARE(sel.startInRow(1), 4);
        QCOMPARE(sel.endInRow(1), 7);       // "ctx one".size()
        QCOMPARE(sel.startInRow(2), 0);     // whole middle row
        QCOMPARE(sel.endInRow(2), 9);       // "added two".size()
        QCOMPARE(sel.startInRow(3), 0);
        QCOMPARE(sel.endInRow(3), 7);
        QCOMPARE(sel.startInRow(0), -1);    // hunk header is outside
    }

    void backward_drag_normalises_to_the_same_range()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection forward;
        forward.setModel(&model);
        DiffSelection backward;
        backward.setModel(&model);

        forward.begin(1, 4);
        forward.extendTo(3, 7);
        backward.begin(3, 7);
        backward.extendTo(1, 4);

        for (int row = 0; row < 4; ++row)
        {
            QCOMPARE(backward.startInRow(row), forward.startInRow(row));
            QCOMPARE(backward.endInRow(row), forward.endInRow(row));
        }
    }

    void right_to_left_drag_inside_one_row_normalises()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(2, 7);
        sel.extendTo(2, 2);
        QCOMPARE(sel.startInRow(2), 2);
        QCOMPARE(sel.endInRow(2), 7);
    }

    void columns_are_clamped_to_the_row_length()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 0);
        sel.extendTo(1, 999);
        QCOMPARE(sel.endInRow(1), 7);
        QCOMPARE(sel.rowLength(1), 7);
    }

    void select_all_spans_first_column_to_last_row_end()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectAll();
        QVERIFY(sel.property("hasSelection").toBool());
        QCOMPARE(sel.startInRow(0), 0);
        QCOMPARE(sel.endInRow(3), 13); // "removed three".size()
    }

    void select_all_on_an_empty_model_selects_nothing()
    {
        DiffLinesModel model;
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectAll();
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void a_model_reset_clears_the_selection()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);
        sel.selectAll();
        QVERIFY(sel.property("hasSelection").toBool());

        model.setDiff(sampleDiff(), {}, false); // a new file lands in the same model
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void changing_the_model_clears_and_notifies()
    {
        DiffLinesModel first;
        first.setDiff(sampleDiff(), {}, false);
        DiffLinesModel second;
        second.setDiff(sampleDiff(), {}, false);

        DiffSelection sel;
        sel.setModel(&first);
        sel.selectAll();
        QSignalSpy modelSpy(&sel, SIGNAL(modelChanged()));

        sel.setModel(&second);
        QCOMPARE(modelSpy.count(), 1);
        QVERIFY(!sel.property("hasSelection").toBool());
    }

    void anchor_row_is_readable_for_drag_direction()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(2, 0);
        QCOMPARE(sel.property("anchorRow").toInt(), 2);
    }
};

#include "test_diff_selection.moc"
```

- [ ] **Step 2: Register the test, then run it to verify it fails**

`tests/CMakeLists.txt` — add to `gittide_ui_test_sources`, next to `test_diff_lines_model.cpp`:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_diff_selection.cpp
```

`tests/ui/main.cpp` — add the include next to `#include "test_diff_lines_model.cpp"`:

```cpp
#include "test_diff_selection.cpp"
```

and a run line next to the other `RUN(...)` calls in `main()`:

```cpp
    RUN(TestDiffSelection);
```

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`
Expected: FAIL to compile — `gittide/ui/diffselection.hpp` does not exist.

- [ ] **Step 3: Write the header**

Create `ui/include/gittide/ui/diffselection.hpp`:

```cpp
#pragma once
#include <QAbstractItemModel>
#include <QObject>
#include <QPointer>
#include <QString>

namespace gittide::ui {

/// Character-accurate text selection over a diff list model (DiffLinesModel).
///
/// The selection is two (row, column) positions in *model* coordinates: row is a
/// model index, column a character offset into that row's `lineText`. It lives
/// here rather than in the QML delegates because a ListView destroys off-screen
/// delegates, and a selection routinely spans rows that no longer exist as items.
///
/// The anchor is where the drag started and the cursor where it currently is;
/// either order is valid and every query normalises them, so a bottom-up or
/// right-to-left drag yields the same result as the forward one.
///
/// Reads the model through the role names `lineText` and `lineKind`, so it works
/// with any model that publishes them.
class DiffSelection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int anchorRow READ anchorRow NOTIFY selectionChanged)
public:
    explicit DiffSelection(QObject* parent = nullptr);

    QAbstractItemModel* model() const;

    /// Attach to @p model. Clears any selection and re-subscribes the clear-on-
    /// reset connections, so a file switch never leaves a stale selection behind.
    void setModel(QAbstractItemModel* model);

    /// True when anchor and cursor differ — a bare click selects nothing.
    bool hasSelection() const;

    /// Row the current drag started on; -1 when there is no anchor. QML reads it
    /// to decide which end of a row to extend to when a row has no code item.
    int anchorRow() const;

    /// Start a selection at (@p row, @p col): anchor and cursor both land there.
    Q_INVOKABLE void begin(int row, int col);

    /// Move the cursor to (@p row, @p col), keeping the anchor. Falls back to
    /// begin() when there is no anchor yet (a Shift+click with nothing selected).
    Q_INVOKABLE void extendTo(int row, int col);

    /// Select every row, from column 0 to the end of the last row. No-op on an
    /// empty model.
    Q_INVOKABLE void selectAll();

    /// Drop the selection.
    Q_INVOKABLE void clear();

    /// First selected column in @p row, or -1 when the row is outside the
    /// selection. Clamped to the row's length.
    Q_INVOKABLE int startInRow(int row) const;

    /// One past the last selected column in @p row, or -1 when the row is outside
    /// the selection. Clamped to the row's length.
    Q_INVOKABLE int endInRow(int row) const;

    /// Character count of @p row's text; 0 for an unknown row.
    Q_INVOKABLE int rowLength(int row) const;

signals:
    void modelChanged();
    void selectionChanged();

private:
    struct Pos
    {
        int row = -1;
        int col = 0;
    };

    Pos     orderedStart() const;
    Pos     orderedEnd() const;
    QString rowText(int row) const;
    QString rowKind(int row) const;
    int     roleOf(const QByteArray& name) const;

    QPointer<QAbstractItemModel> m_model;
    Pos                          m_anchor;
    Pos                          m_cursor;
};

} // namespace gittide::ui
```

- [ ] **Step 4: Write the implementation**

Create `ui/src/diffselection.cpp`:

```cpp
#include "gittide/ui/diffselection.hpp"

namespace gittide::ui {

DiffSelection::DiffSelection(QObject* parent)
    : QObject(parent)
{
}

QAbstractItemModel* DiffSelection::model() const
{
    return m_model;
}

void DiffSelection::setModel(QAbstractItemModel* model)
{
    if (m_model == model)
        return;
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model)
    {
        // Any structural change invalidates row indices — drop the selection
        // rather than let it point at rows that mean something else now.
        connect(m_model, &QAbstractItemModel::modelReset, this, &DiffSelection::clear);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &DiffSelection::clear);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &DiffSelection::clear);
        connect(m_model, &QAbstractItemModel::layoutChanged, this, &DiffSelection::clear);
    }
    clear();
    emit modelChanged();
}

bool DiffSelection::hasSelection() const
{
    if (m_anchor.row < 0 || m_cursor.row < 0)
        return false;
    return m_anchor.row != m_cursor.row || m_anchor.col != m_cursor.col;
}

int DiffSelection::anchorRow() const
{
    return m_anchor.row;
}

void DiffSelection::begin(int row, int col)
{
    m_anchor = Pos{row, col};
    m_cursor = m_anchor;
    emit selectionChanged();
}

void DiffSelection::extendTo(int row, int col)
{
    if (m_anchor.row < 0)
    {
        begin(row, col);
        return;
    }
    m_cursor = Pos{row, col};
    emit selectionChanged();
}

void DiffSelection::selectAll()
{
    const int rows = m_model ? m_model->rowCount() : 0;
    if (rows == 0)
    {
        clear();
        return;
    }
    m_anchor = Pos{0, 0};
    m_cursor = Pos{rows - 1, rowLength(rows - 1)};
    emit selectionChanged();
}

void DiffSelection::clear()
{
    m_anchor = Pos{};
    m_cursor = Pos{};
    emit selectionChanged();
}

int DiffSelection::startInRow(int row) const
{
    if (!hasSelection())
        return -1;
    const Pos s = orderedStart();
    const Pos e = orderedEnd();
    if (row < s.row || row > e.row)
        return -1;
    return row == s.row ? qBound(0, s.col, rowLength(row)) : 0;
}

int DiffSelection::endInRow(int row) const
{
    if (!hasSelection())
        return -1;
    const Pos s = orderedStart();
    const Pos e = orderedEnd();
    if (row < s.row || row > e.row)
        return -1;
    const int len = rowLength(row);
    return row == e.row ? qBound(0, e.col, len) : len;
}

int DiffSelection::rowLength(int row) const
{
    return static_cast<int>(rowText(row).size());
}

DiffSelection::Pos DiffSelection::orderedStart() const
{
    const bool cursorFirst = m_cursor.row < m_anchor.row ||
                             (m_cursor.row == m_anchor.row && m_cursor.col < m_anchor.col);
    return cursorFirst ? m_cursor : m_anchor;
}

DiffSelection::Pos DiffSelection::orderedEnd() const
{
    const bool cursorFirst = m_cursor.row < m_anchor.row ||
                             (m_cursor.row == m_anchor.row && m_cursor.col < m_anchor.col);
    return cursorFirst ? m_anchor : m_cursor;
}

int DiffSelection::roleOf(const QByteArray& name) const
{
    if (!m_model)
        return -1;
    const auto roles = m_model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}

QString DiffSelection::rowText(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return {};
    const int role = roleOf("lineText");
    if (role < 0)
        return {};
    return m_model->data(m_model->index(row, 0), role).toString();
}

QString DiffSelection::rowKind(int row) const
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return {};
    const int role = roleOf("lineKind");
    if (role < 0)
        return {};
    return m_model->data(m_model->index(row, 0), role).toString();
}

} // namespace gittide::ui
```

`rowKind()` is unused until Task 3 — it is declared here so Task 3 adds only `copyText`.

- [ ] **Step 5: Register the source and run the tests**

`ui/CMakeLists.txt` — add next to `difflinesmodel.cpp`:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/diffselection.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/diffselection.hpp
```

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS — `[ui-test] running TestDiffSelection` with no failures.

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/diffselection.hpp ui/src/diffselection.cpp \
        ui/CMakeLists.txt tests/CMakeLists.txt tests/ui/main.cpp tests/ui/test_diff_selection.cpp
git commit -m "feat(ui): add DiffSelection — cross-row diff text selection state"
```

---

## Task 3: `DiffSelection::copyText`

**Files:**
- Modify: `ui/include/gittide/ui/diffselection.hpp`, `ui/src/diffselection.cpp`
- Test: `tests/ui/test_diff_selection.cpp`

**Interfaces:**
- Produces: `Q_INVOKABLE QString copyText(bool withMarkers = false) const`.

- [ ] **Step 1: Write the failing tests**

Add to `class TestDiffSelection` in `tests/ui/test_diff_selection.cpp`:

```cpp
    void copy_text_joins_rows_and_honours_partial_ends()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 4);    // "ctx |one"
        sel.extendTo(3, 7); // "removed| three"

        QCOMPARE(sel.copyText(false), QStringLiteral("one\nadded two\nremoved\n"));
    }

    void copy_text_is_empty_without_a_selection()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        QCOMPARE(sel.copyText(false), QString());
    }

    void copy_with_markers_prefixes_every_row()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(1, 0);
        sel.extendTo(3, 13);

        // Context gets two spaces, added "+ ", removed "- " — ASCII hyphen, not
        // the "−" the view draws, so the result pastes as a patch-ish fragment.
        QCOMPARE(sel.copyText(true),
                 QStringLiteral("  ctx one\n+ added two\n- removed three\n"));
    }

    void a_partially_selected_row_still_gets_its_marker()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.begin(2, 6);
        sel.extendTo(3, 7);
        QCOMPARE(sel.copyText(true), QStringLiteral("+ two\n- removed\n"));
    }

    void hunk_headers_copy_verbatim_and_unprefixed()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectAll();
        const QString withMarkers = sel.copyText(true);
        QVERIFY2(withMarkers.startsWith(QStringLiteral("@@ -1,2 +1,2 @@\n")),
                 qPrintable(withMarkers));
    }

    void block_rows_contribute_nothing()
    {
        DiffLinesModel model;
        // blocks = true inserts a synthetic "block" row before the added/removed
        // run; it is a staging affordance, not diff content.
        model.setDiff(sampleDiff(), {}, false, true);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectAll();
        const QString copied = sel.copyText(false);
        QCOMPARE(copied, QStringLiteral("@@ -1,2 +1,2 @@\nctx one\nadded two\nremoved three\n"));
    }
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — `DiffSelection` has no member `copyText`.

- [ ] **Step 3: Implement**

Declaration in `ui/include/gittide/ui/diffselection.hpp`, after `rowLength`:

```cpp
    /// The selected text. With @p withMarkers each row is prefixed "+ ", "- " or
    /// "  " by its kind (ASCII hyphen, not the "−" the view draws), so the result
    /// reads as a patch fragment; hunk headers are always copied verbatim and
    /// unprefixed. Synthetic "block" rows contribute nothing. Rows are joined
    /// with "\n" and a trailing "\n" is appended. Empty without a selection.
    Q_INVOKABLE QString copyText(bool withMarkers = false) const;
```

Definition in `ui/src/diffselection.cpp`:

```cpp
QString DiffSelection::copyText(bool withMarkers) const
{
    if (!hasSelection())
        return {};

    const Pos s = orderedStart();
    const Pos e = orderedEnd();
    QString   out;
    for (int row = s.row; row <= e.row; ++row)
    {
        const QString kind = rowKind(row);
        if (kind == QLatin1String("block"))
            continue;
        const int from = startInRow(row);
        const int to   = endInRow(row);
        if (from < 0 || to < from)
            continue;

        QString slice = rowText(row).mid(from, to - from);
        if (withMarkers && kind != QLatin1String("hunk"))
        {
            const QChar sign = kind == QLatin1String("added")     ? QLatin1Char('+')
                               : kind == QLatin1String("removed") ? QLatin1Char('-')
                                                                  : QLatin1Char(' ');
            slice.prepend(QString(sign) + QLatin1Char(' '));
        }
        out += slice;
        out += QLatin1Char('\n');
    }
    return out;
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS.

If `block_rows_contribute_nothing` fails on the hunk-header text, print the actual string with `qPrintable(copied)` and align the expectation with `DiffLinesModel`'s `@@ -%1,%2 +%3,%4 @@` format — the model is ground truth.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gittide/ui/diffselection.hpp ui/src/diffselection.cpp tests/ui/test_diff_selection.cpp
git commit -m "feat(ui): copy selected diff text, plain or with diff markers"
```

---

## Task 4: Word and line selection

**Files:**
- Modify: `ui/include/gittide/ui/diffselection.hpp`, `ui/src/diffselection.cpp`
- Test: `tests/ui/test_diff_selection.cpp`

**Interfaces:**
- Produces: `Q_INVOKABLE void selectWord(int row, int col)`, `Q_INVOKABLE void selectLine(int row)`.

- [ ] **Step 1: Write the failing tests**

Add to `class TestDiffSelection`:

```cpp
    void select_word_expands_to_the_identifier_under_the_column()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectWord(2, 7); // inside "two" of "added two"
        QCOMPARE(sel.startInRow(2), 6);
        QCOMPARE(sel.endInRow(2), 9);
        QCOMPARE(sel.copyText(false), QStringLiteral("two\n"));
    }

    void select_word_at_the_start_of_a_row_stops_at_the_row_start()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectWord(1, 0); // "ctx one"
        QCOMPARE(sel.startInRow(1), 0);
        QCOMPARE(sel.endInRow(1), 3);
    }

    void select_word_on_a_separator_takes_the_single_character()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectWord(1, 3); // the space in "ctx one"
        QCOMPARE(sel.startInRow(1), 3);
        QCOMPARE(sel.endInRow(1), 4);
    }

    void select_word_past_the_end_takes_the_last_word()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectWord(1, 999);
        QCOMPARE(sel.startInRow(1), 4);
        QCOMPARE(sel.endInRow(1), 7);
    }

    void select_line_takes_the_whole_row()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false);
        DiffSelection sel;
        sel.setModel(&model);

        sel.selectLine(3);
        QCOMPARE(sel.startInRow(3), 0);
        QCOMPARE(sel.endInRow(3), 13);
        QCOMPARE(sel.copyText(false), QStringLiteral("removed three\n"));
    }

    void selecting_an_empty_row_selects_nothing()
    {
        DiffLinesModel model;
        model.setDiff(sampleDiff(), {}, false, true); // block rows carry empty text
        DiffSelection sel;
        sel.setModel(&model);

        // Row 2 is the synthetic block row inserted before the added/removed run.
        QCOMPARE(sel.rowLength(2), 0);
        sel.selectLine(2);
        QVERIFY(!sel.property("hasSelection").toBool());
        sel.selectWord(2, 0);
        QVERIFY(!sel.property("hasSelection").toBool());
    }
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — no member `selectWord`.

- [ ] **Step 3: Implement**

Declarations in `ui/include/gittide/ui/diffselection.hpp`, after `extendTo`:

```cpp
    /// Select the word around (@p row, @p col) — a run of letters, digits and
    /// underscores. On a separator character selects that single character; on an
    /// empty row selects nothing. @p col past the row end is treated as the last
    /// character, so a double-click in the blank area past a line takes its last
    /// word.
    Q_INVOKABLE void selectWord(int row, int col);

    /// Select all of @p row. Selects nothing on an empty row.
    Q_INVOKABLE void selectLine(int row);
```

In `ui/src/diffselection.cpp`, an anonymous-namespace helper above the class
definitions:

```cpp
namespace {
bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}
} // namespace
```

and the two methods:

```cpp
void DiffSelection::selectWord(int row, int col)
{
    const QString text = rowText(row);
    if (text.isEmpty())
    {
        begin(row, 0);
        return;
    }

    const int c     = qBound(0, col, static_cast<int>(text.size()) - 1);
    int       start = c;
    int       end   = c + 1;
    if (isWordChar(text.at(c)))
    {
        while (start > 0 && isWordChar(text.at(start - 1)))
            --start;
        end = c;
        while (end < text.size() && isWordChar(text.at(end)))
            ++end;
    }

    m_anchor = Pos{row, start};
    m_cursor = Pos{row, end};
    emit selectionChanged();
}

void DiffSelection::selectLine(int row)
{
    m_anchor = Pos{row, 0};
    m_cursor = Pos{row, rowLength(row)};
    emit selectionChanged();
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gittide/ui/diffselection.hpp ui/src/diffselection.cpp tests/ui/test_diff_selection.cpp
git commit -m "feat(ui): word and line selection in the diff"
```

---

## Task 5: `DiffCodeText.qml` — a row that paints its selection

**Files:**
- Create: `ui/qml/DiffCodeText.qml`
- Modify: `ui/qml/qml.qrc`, `ui/src/qmlcontext.cpp`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`
- Test: `tests/ui/test_qml_diff_selection.cpp`

**Interfaces:**
- Consumes: `DiffSelection` (Tasks 2–4), the `theme` context property.
- Produces: QML type `DiffCodeText` with properties `row` (int), `selection` (var), `plainText` (string), `html` (string); `objectName` fixed to `"diffCodeText"`; exposes `TextEdit`'s own `positionAt(x, y)`, `selectionStart`, `selectionEnd`. Also produces the QML registration `import GitTide` → `DiffSelection`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_qml_diff_selection.cpp`:

```cpp
// Tests for the diff selection QML layer: DiffCodeText.qml paints the slice of a
// DiffSelection that covers its row, DiffSelectionOverlay.qml turns pointer and
// key input into selection calls, and both diff surfaces declare the pair.
//
// The offscreen harness never renders a frame, so ListView delegates are never
// instantiated (see tests/ui/test_qml_history.cpp). Nothing here goes through a
// real list: components are instantiated directly, and the overlay is driven
// against a stub list object.

#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <memory>

#include "gittide/diff.hpp"
#include "gittide/ui/diffselection.hpp"
#include "gittide/ui/difflinesmodel.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::DiffLinesModel;
using gittide::ui::DiffSelection;
using gittide::ui::QmlTheme;
using gittide::ui::ThemeManager;

namespace
{
gittide::DiffResult twoLineDiff()
{
    gittide::DiffLine ctx;
    ctx.origin    = gittide::DiffLineOrigin::Context;
    ctx.oldLineno = 1;
    ctx.newLineno = 1;
    ctx.text      = "ctx one";

    gittide::DiffLine added;
    added.origin    = gittide::DiffLineOrigin::Added;
    added.oldLineno = -1;
    added.newLineno = 2;
    added.text      = "added two";

    gittide::DiffHunk h;
    h.oldStart = 1;
    h.oldLines = 1;
    h.newStart = 1;
    h.newLines = 2;
    h.lines    = {ctx, added};

    gittide::DiffResult r;
    r.hunks = {h};
    return r;
}
} // namespace

class TestQmlDiffSelection : public QObject
{
    Q_OBJECT
private slots:

    void code_text_paints_the_selection_slice_for_its_row()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffCodeText.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));

        obj->setProperty("row", 2);                       // the added line
        obj->setProperty("plainText", QStringLiteral("added two"));
        obj->setProperty("selection", QVariant::fromValue(&selection));

        // Nothing selected yet.
        QCOMPARE(obj->property("selectionStart").toInt(), obj->property("selectionEnd").toInt());

        selection.begin(2, 6);
        selection.extendTo(2, 9);
        QCOMPARE(obj->property("selectionStart").toInt(), 6);
        QCOMPARE(obj->property("selectionEnd").toInt(), 9);
        QCOMPARE(obj->property("selectedText").toString(), QStringLiteral("two"));

        selection.clear();
        QCOMPARE(obj->property("selectedText").toString(), QString());
    }

    void code_text_takes_its_selection_colours_from_the_theme()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffCodeText.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));

        QCOMPARE(obj->property("selectionColor").value<QColor>(), QColor("#5942A5F5"));
        mgr.setMode(ThemeManager::Mode::Light);
        QCOMPARE(obj->property("selectionColor").value<QColor>(), QColor("#401976D2"));
    }

    void a_highlighted_row_selects_at_the_same_offsets_as_its_source_text()
    {
        // Syntax highlighting renders HTML, and TextEdit positions are document
        // positions. Selecting by source-string offsets must still land on the
        // same characters, or the painted highlight drifts from what gets copied.
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffCodeText.qml")));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY2(obj != nullptr, qPrintable(comp.errorString()));

        obj->setProperty("row", 2);
        obj->setProperty("plainText", QStringLiteral("added two"));
        obj->setProperty("html", QStringLiteral("<span style=\"color:#ff0000;\">added</span> two"));
        obj->setProperty("selection", QVariant::fromValue(&selection));

        selection.begin(2, 6);
        selection.extendTo(2, 9);
        QCOMPARE(obj->property("selectedText").toString(), QStringLiteral("two"));
    }
};

#include "test_qml_diff_selection.moc"
```

Register it: add `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_qml_diff_selection.cpp` to
`gittide_ui_test_sources` in `tests/CMakeLists.txt`, and in `tests/ui/main.cpp`
add `#include "test_qml_diff_selection.cpp"` plus `RUN(TestQmlDiffSelection);`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — `qrc:/qml/DiffCodeText.qml` cannot be loaded (no such file).

- [ ] **Step 3: Write the component**

Create `ui/qml/DiffCodeText.qml`:

```qml
import QtQuick

// One diff row's code column. A read-only TextEdit rather than a Label because
// only TextEdit can map a pixel to a character index (positionAt) and paint a
// selection (select) — both of which the shared DiffSelection needs.
//
// Presentational only: pointer and key handling live in DiffSelectionOverlay,
// which sits outside the delegates and therefore survives autoscroll recycling.
//
// TextEdit has no elide, so a long line is hard-clipped without the "…" a Label
// would draw. Copy still yields the full line — it reads the model, not this item.
TextEdit {
    id: codeText
    objectName: "diffCodeText"

    // Model row this item renders. Selection ranges are looked up by it.
    property int row: -1
    // The DiffSelection this row participates in (null = never selected).
    property var selection: null
    // Syntax-highlighted HTML for the row; empty means render plainText.
    property string html: ""
    // The row's source text, as the model holds it.
    property string plainText: ""

    readonly property int selFrom: selection && selection.hasSelection ? selection.startInRow(row) : -1
    readonly property int selTo:   selection && selection.hasSelection ? selection.endInRow(row) : -1

    readOnly: true
    selectByMouse: false
    selectByKeyboard: false
    activeFocusOnPress: false
    clip: true
    font.family: "monospace"
    font.pixelSize: 12
    textFormat: html.length > 0 ? Text.RichText : Text.PlainText
    text: html.length > 0 ? html : plainText
    selectionColor: theme.selectionBg
    selectedTextColor: theme.selectionText

    function applySelection() {
        if (selFrom < 0 || selTo <= selFrom)
            codeText.deselect()
        else
            codeText.select(selFrom, selTo)
    }

    onSelFromChanged: applySelection()
    onSelToChanged: applySelection()
    onTextChanged: applySelection()
    Component.onCompleted: applySelection()
}
```

Add to `ui/qml/qml.qrc`, next to `<file>DiffView.qml</file>`:

```xml
    <file>DiffCodeText.qml</file>
```

- [ ] **Step 4: Register `DiffSelection` as a QML type**

`ui/src/qmlcontext.cpp` — add the include next to the others:

```cpp
#include "gittide/ui/diffselection.hpp"
```

and the registration inside `registerQmlTypes()`:

```cpp
    qmlRegisterType<DiffSelection>("GitTide", 1, 0, "DiffSelection");
```

- [ ] **Step 5: Run to verify the tests pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS — `[ui-test] running TestQmlDiffSelection` green.

If `a_highlighted_row_selects_at_the_same_offsets_as_its_source_text` fails, the
rich-text document's plain text differs from the source string (risk 1 in the
spec). Do **not** work around it in `copyText` — copy already reads the model.
Fix the painting instead, and record what you found in `docs/decisions.md`.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/DiffCodeText.qml ui/qml/qml.qrc ui/src/qmlcontext.cpp \
        tests/CMakeLists.txt tests/ui/main.cpp tests/ui/test_qml_diff_selection.cpp
git commit -m "feat(ui): DiffCodeText renders a diff row and paints its selection"
```

---

## Task 6: `DiffSelectionOverlay.qml` — drag and autoscroll

**Files:**
- Create: `ui/qml/DiffSelectionOverlay.qml`
- Modify: `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_diff_selection.cpp`

**Interfaces:**
- Consumes: `DiffSelection`, `DiffCodeText` (`objectName: "diffCodeText"`).
- Produces: QML type `DiffSelectionOverlay` with properties `list` (var), `selection` (var); signal `copyRequested(string text)`; functions `rowAt(y)`, `columnAt(row, x, y)`, `pressAt(x, y, modifiers)`, `moveTo(x, y)`, `endDrag()`. Tasks 7 and 8 extend this same file.

- [ ] **Step 1: Write the failing test**

Add to `class TestQmlDiffSelection` in `tests/ui/test_qml_diff_selection.cpp`
(and the `<QQuickItem>` include is already there):

```cpp
    // A stand-in for the diff ListView. The overlay only ever asks a list for
    // indexAt / itemAtIndex / contentY / contentHeight / height, so a stub with
    // those members exercises it fully — and unlike a real ListView it works
    // headless, where delegates are never instantiated.
    void overlay_drag_builds_a_selection_across_rows()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QQmlComponent comp(&engine);
        comp.setData(R"QML(
            import QtQuick

            // Rows are 20px tall; each row's code column starts at x = 100.
            Item {
                id: host
                width: 400
                height: 60

                property var rows: ["@@ -1,1 +1,2 @@", "ctx one", "added two"]

                Column {
                    id: fakeList
                    objectName: "fakeList"
                    property real contentY: 0
                    property real contentHeight: 60
                    width: 400
                    height: 60
                    function indexAt(x, y) {
                        const row = Math.floor(y / 20)
                        return (row < 0 || row > 2) ? -1 : row
                    }
                    function itemAtIndex(i) { return repeater.itemAt(i) }
                    Repeater {
                        id: repeater
                        model: 3
                        delegate: Item {
                            width: 400
                            height: 20
                            DiffCodeText {
                                x: 100
                                width: 300
                                height: 20
                                row: index
                                selection: testSelection
                                plainText: host.rows[index]
                            }
                        }
                    }
                }

                DiffSelectionOverlay {
                    objectName: "overlay"
                    anchors.fill: parent
                    list: fakeList
                    selection: testSelection
                }
            }
        )QML", QUrl(QStringLiteral("qrc:/qml/_test_diff_overlay_host.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root != nullptr, qPrintable(comp.errorString()));

        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        // Press inside row 1 ("ctx one") at its very start, drag into row 2.
        QMetaObject::invokeMethod(overlay, "pressAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 0));
        QMetaObject::invokeMethod(overlay, "moveTo", Q_ARG(QVariant, 400), Q_ARG(QVariant, 45));
        QMetaObject::invokeMethod(overlay, "endDrag");

        QVERIFY(selection.property("hasSelection").toBool());
        QCOMPARE(selection.startInRow(1), 0);
        QCOMPARE(selection.endInRow(2), 9); // dragged past the end of "added two"
    }

    void overlay_rejects_a_press_left_of_the_code_column()
    {
        // The checkbox, line-number gutter and sign columns must keep their own
        // click behaviour — a press there is not a text selection.
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QQmlComponent comp(&engine);
        comp.setData(R"QML(
            import QtQuick
            Item {
                width: 400
                height: 60
                Column {
                    id: fakeList
                    property real contentY: 0
                    property real contentHeight: 60
                    width: 400
                    height: 60
                    function indexAt(x, y) {
                        const row = Math.floor(y / 20)
                        return (row < 0 || row > 2) ? -1 : row
                    }
                    function itemAtIndex(i) { return repeater.itemAt(i) }
                    Repeater {
                        id: repeater
                        model: 3
                        delegate: Item {
                            width: 400
                            height: 20
                            DiffCodeText {
                                x: 100
                                width: 300
                                height: 20
                                row: index
                                selection: testSelection
                                plainText: "ctx one"
                            }
                        }
                    }
                }
                DiffSelectionOverlay {
                    objectName: "overlay"
                    anchors.fill: parent
                    list: fakeList
                    selection: testSelection
                }
            }
        )QML", QUrl(QStringLiteral("qrc:/qml/_test_diff_overlay_host.qml")));
        std::unique_ptr<QObject> root(comp.create());
        QVERIFY2(root != nullptr, qPrintable(comp.errorString()));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QVERIFY(overlay != nullptr);

        QVariant handled;
        QMetaObject::invokeMethod(overlay, "pressAt", Q_RETURN_ARG(QVariant, handled),
                                  Q_ARG(QVariant, 40), Q_ARG(QVariant, 25), Q_ARG(QVariant, 0));
        QVERIFY(!handled.toBool());
        QVERIFY(!selection.property("hasSelection").toBool());
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — `DiffSelectionOverlay is not a type`.

- [ ] **Step 3: Write the component**

Create `ui/qml/DiffSelectionOverlay.qml`:

```qml
import QtQuick

// Pointer and keyboard selection surface for a diff ListView. Declared as a
// SIBLING of the list, never inside a delegate: an autoscrolling drag scrolls the
// row it started on out of view, the ListView destroys that delegate, and a
// delegate-owned MouseArea would lose its mouse grab mid-drag.
//
// Talks to the list only through indexAt / itemAtIndex / contentY /
// contentHeight / height, so a stub object drives it in tests — where real
// ListView delegates are never instantiated.
MouseArea {
    id: overlay
    objectName: "diffSelectionOverlay"

    // The diff ListView this overlay selects in.
    property var list: null
    // The DiffSelection to drive.
    property var selection: null

    // Emitted with the text to put on the clipboard; the owning view routes it to
    // repoVm.copyToClipboard so clipboard access stays in one place.
    signal copyRequested(string text)

    property bool dragging: false
    property real lastX: 0
    property real lastY: 0

    acceptedButtons: Qt.LeftButton
    cursorShape: Qt.IBeamCursor

    // --- hit testing -------------------------------------------------------

    function findCode(item) {
        if (!item)
            return null
        if (item.objectName === "diffCodeText")
            return item
        for (var i = 0; i < item.children.length; ++i) {
            const found = findCode(item.children[i])
            if (found)
                return found
        }
        return null
    }

    function codeItemAtRow(row) {
        if (!list || row < 0)
            return null
        return findCode(list.itemAtIndex(row))
    }

    // Model row under overlay-local y. y outside the viewport is clamped to the
    // nearest edge row, which is what an autoscrolling drag wants.
    function rowAt(y) {
        if (!list)
            return -1
        const clamped = Math.max(0, Math.min(list.height - 1, y))
        return list.indexAt(1, clamped + list.contentY)
    }

    // Character offset under (x, y) in the row's code item, or -1 when the point
    // is left of the code column or the row has no code item (a conflict header).
    function columnAt(row, x, y) {
        const code = codeItemAtRow(row)
        if (!code)
            return -1
        const local = code.mapFromItem(overlay, x, y)
        if (local.x < 0)
            return -1
        return code.positionAt(local.x, local.y)
    }

    // --- selection driving -------------------------------------------------

    // Returns true when the press started a selection, false when it belongs to
    // whatever sits underneath (checkbox, gutter, conflict buttons).
    function pressAt(x, y, modifiers) {
        if (!selection)
            return false
        const row = rowAt(y)
        const col = columnAt(row, x, y)
        if (row < 0 || col < 0)
            return false

        lastX = x
        lastY = y
        if (modifiers & Qt.ShiftModifier)
            selection.extendTo(row, col)
        else
            selection.begin(row, col)
        dragging = true
        return true
    }

    function moveTo(x, y) {
        if (!dragging || !selection)
            return
        lastX = x
        lastY = y
        const row = rowAt(y)
        if (row < 0)
            return
        var col = columnAt(row, x, y)
        if (col < 0)
            // No code item on this row: extend through it — to the row end when
            // dragging downwards, to its start when dragging back up.
            col = row > selection.anchorRow ? selection.rowLength(row) : 0
        selection.extendTo(row, col)
        autoScroll.running = y < 0 || y > overlay.height
    }

    function endDrag() {
        dragging = false
        autoScroll.running = false
    }

    onPressed: function(mouse) {
        overlay.forceActiveFocus()
        mouse.accepted = pressAt(mouse.x, mouse.y, mouse.modifiers)
    }
    onPositionChanged: function(mouse) { moveTo(mouse.x, mouse.y) }
    onReleased: endDrag()

    Timer {
        id: autoScroll
        interval: 16
        repeat: true
        onTriggered: {
            if (!overlay.list)
                return
            const over = overlay.lastY < 0 ? overlay.lastY : (overlay.lastY - overlay.height)
            const step = Math.max(-60, Math.min(60, over))
            const max = Math.max(0, overlay.list.contentHeight - overlay.list.height)
            overlay.list.contentY = Math.max(0, Math.min(max, overlay.list.contentY + step))
            overlay.moveTo(overlay.lastX, overlay.lastY)
        }
    }
}
```

Add to `ui/qml/qml.qrc`:

```xml
    <file>DiffSelectionOverlay.qml</file>
```

- [ ] **Step 4: Run to verify the tests pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/DiffSelectionOverlay.qml ui/qml/qml.qrc tests/ui/test_qml_diff_selection.cpp
git commit -m "feat(ui): drag-select diff text across rows with autoscroll"
```

---

## Task 7: Overlay — click behaviour and keyboard shortcuts

**Files:**
- Modify: `ui/qml/DiffSelectionOverlay.qml`, `docs/spec/product/keyboard-controls.md`
- Test: `tests/ui/test_qml_diff_selection.cpp`

**Interfaces:**
- Produces: overlay functions `clickAt(x, y, count)` (count 1 = clear, 2 = word, 3 = line) and `handleKey(key, modifiers)`; the latter returns true when it consumed the key.

- [ ] **Step 1: Write the failing test**

Add to `class TestQmlDiffSelection`. First factor the host QML out of Task 6's
two tests into a private helper and have those two call it, so the block exists
once:

```cpp
private:
    // Builds the stub-list host from Task 6 and returns (root, overlay). The
    // engine, theme and selection must outlive the returned root.
    static QObject* buildOverlayHost(QQmlEngine& engine, QQmlComponent& comp)
    {
        comp.setData(R"QML(
            import QtQuick
            Item {
                id: host
                width: 400
                height: 60
                property var rows: ["@@ -1,1 +1,2 @@", "ctx one", "added two"]
                Column {
                    id: fakeList
                    property real contentY: 0
                    property real contentHeight: 60
                    width: 400
                    height: 60
                    function indexAt(x, y) {
                        const row = Math.floor(y / 20)
                        return (row < 0 || row > 2) ? -1 : row
                    }
                    function itemAtIndex(i) { return repeater.itemAt(i) }
                    Repeater {
                        id: repeater
                        model: 3
                        delegate: Item {
                            width: 400
                            height: 20
                            DiffCodeText {
                                x: 100
                                width: 300
                                height: 20
                                row: index
                                selection: testSelection
                                plainText: host.rows[index]
                            }
                        }
                    }
                }
                DiffSelectionOverlay {
                    objectName: "overlay"
                    anchors.fill: parent
                    list: fakeList
                    selection: testSelection
                }
            }
        )QML", QUrl(QStringLiteral("qrc:/qml/_test_diff_overlay_host.qml")));
        return comp.create();
    }

private slots:

    void a_plain_click_clears_the_selection()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QQmlComponent comp(&engine);
        std::unique_ptr<QObject> root(buildOverlayHost(engine, comp));
        QVERIFY2(root != nullptr, qPrintable(comp.errorString()));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));

        selection.selectAll();
        QMetaObject::invokeMethod(overlay, "clickAt", Q_ARG(QVariant, 150), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 1));
        QVERIFY(!selection.property("hasSelection").toBool());
    }

    void a_double_click_selects_the_word_and_a_triple_click_the_row()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QQmlComponent comp(&engine);
        std::unique_ptr<QObject> root(buildOverlayHost(engine, comp));
        QVERIFY2(root != nullptr, qPrintable(comp.errorString()));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));

        // Row 1 is "ctx one"; x = 100 is its first character.
        QMetaObject::invokeMethod(overlay, "clickAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 2));
        QCOMPARE(selection.copyText(false), QStringLiteral("ctx\n"));

        QMetaObject::invokeMethod(overlay, "clickAt", Q_ARG(QVariant, 100), Q_ARG(QVariant, 25),
                                  Q_ARG(QVariant, 3));
        QCOMPARE(selection.copyText(false), QStringLiteral("ctx one\n"));
    }

    void ctrl_a_selects_all_and_ctrl_c_copies()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QQmlComponent comp(&engine);
        std::unique_ptr<QObject> root(buildOverlayHost(engine, comp));
        QVERIFY2(root != nullptr, qPrintable(comp.errorString()));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QSignalSpy copySpy(overlay, SIGNAL(copyRequested(QString)));

        QVariant consumed;
        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_A)),
                                  Q_ARG(QVariant, int(Qt::ControlModifier)));
        QVERIFY(consumed.toBool());
        QVERIFY(selection.property("hasSelection").toBool());

        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_C)),
                                  Q_ARG(QVariant, int(Qt::ControlModifier)));
        QVERIFY(consumed.toBool());
        QCOMPARE(copySpy.count(), 1);
        QCOMPARE(copySpy.at(0).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\nctx one\nadded two\n"));

        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_C)),
                                  Q_ARG(QVariant, int(Qt::ControlModifier | Qt::ShiftModifier)));
        QCOMPARE(copySpy.count(), 2);
        QCOMPARE(copySpy.at(1).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\n  ctx one\n+ added two\n"));
    }

    void keys_without_control_are_left_alone()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QQmlComponent comp(&engine);
        std::unique_ptr<QObject> root(buildOverlayHost(engine, comp));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));

        QVariant consumed;
        QMetaObject::invokeMethod(overlay, "handleKey", Q_RETURN_ARG(QVariant, consumed),
                                  Q_ARG(QVariant, int(Qt::Key_A)), Q_ARG(QVariant, 0));
        QVERIFY(!consumed.toBool());
    }
```

`QSignalSpy` needs `#include <QSignalSpy>` at the top of the file.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — `clickAt` / `handleKey` are not functions on the overlay.

- [ ] **Step 3: Implement in `ui/qml/DiffSelectionOverlay.qml`**

Add the click and key functions after `endDrag()`:

```qml
    // count: 1 = plain click (clears), 2 = word, 3 = whole row.
    function clickAt(x, y, count) {
        if (!selection)
            return
        const row = rowAt(y)
        const col = columnAt(row, x, y)
        if (row < 0 || col < 0)
            return
        if (count >= 3)
            selection.selectLine(row)
        else if (count === 2)
            selection.selectWord(row, col)
        else
            selection.clear()
    }

    // Returns true when the key was consumed. Ctrl+C copies code text,
    // Ctrl+Shift+C the same selection with diff markers, Ctrl+A selects the
    // whole diff.
    function handleKey(key, modifiers) {
        if (!selection || !(modifiers & Qt.ControlModifier))
            return false
        if (key === Qt.Key_A) {
            selection.selectAll()
            return true
        }
        if (key === Qt.Key_C) {
            overlay.copyRequested(selection.copyText((modifiers & Qt.ShiftModifier) !== 0))
            return true
        }
        return false
    }
```

Wire them to real input. Replace `onReleased: endDrag()` with the click
bookkeeping, and add the key handler (a `MouseArea` carries `Keys` attached
properties, and `pressAt` already calls `forceActiveFocus()`):

```qml
    // Qt has no triple-click signal — count presses that land close together on
    // the same row, the way editors do.
    property int clickCount: 0
    property int clickRow: -1
    property double lastClickAt: 0
    property bool moved: false

    onReleased: function(mouse) {
        endDrag()
        if (moved) {
            moved = false
            return
        }
        const row = rowAt(mouse.y)
        const now = Date.now()
        clickCount = (now - lastClickAt < 400 && row === clickRow) ? clickCount + 1 : 1
        lastClickAt = now
        clickRow = row
        clickAt(mouse.x, mouse.y, clickCount)
    }

    Keys.onPressed: function(event) {
        event.accepted = handleKey(event.key, event.modifiers)
    }
```

and set `moved` inside `moveTo`, right after `lastY = y`:

```qml
        moved = true
```

`onPositionChanged` already calls `moveTo`, so a drag never reaches the
click path.

- [ ] **Step 4: Run to verify the tests pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS.

- [ ] **Step 5: Document the shortcuts**

In `docs/spec/product/keyboard-controls.md`, add the three bindings in the style
the file already uses, scoped to "diff pane focused":

```markdown
| `Ctrl+A` | Diff pane | Select the whole diff of the active file |
| `Ctrl+C` | Diff pane | Copy the selected diff text (code only) |
| `Ctrl+Shift+C` | Diff pane | Copy the selection with `+`/`-` diff markers |
```

Match the existing table's column layout — if the file uses a different shape
(a list, or a `Cmd` column for macOS), follow that instead; the file is ground
truth for its own format.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/DiffSelectionOverlay.qml tests/ui/test_qml_diff_selection.cpp \
        docs/spec/product/keyboard-controls.md
git commit -m "feat(ui): word/line click selection and copy shortcuts in the diff"
```

---

## Task 8: Diff context menu

**Files:**
- Create: `ui/qml/DiffContextMenu.qml`
- Modify: `ui/qml/DiffSelectionOverlay.qml`, `ui/qml/qml.qrc`, `docs/spec/product/context-menus.md`
- Test: `tests/ui/test_qml_diff_selection.cpp`

**Interfaces:**
- Produces: QML type `DiffContextMenu` (an `AppMenu`) with property `hasSelection` (bool) and signals `copy()`, `copyWithMarkers()`, `selectAll()`; `objectName: "diffContextMenu"`.

- [ ] **Step 1: Write the failing test**

Add to `class TestQmlDiffSelection`:

```cpp
    void context_menu_disables_copy_without_a_selection()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffContextMenu.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> menu(comp.create());
        QVERIFY2(menu != nullptr, qPrintable(comp.errorString()));

        QObject* copyItem = menu->findChild<QObject*>(QStringLiteral("diffMenuCopy"));
        QObject* markersItem = menu->findChild<QObject*>(QStringLiteral("diffMenuCopyMarkers"));
        QObject* selectAllItem = menu->findChild<QObject*>(QStringLiteral("diffMenuSelectAll"));
        QVERIFY(copyItem != nullptr);
        QVERIFY(markersItem != nullptr);
        QVERIFY(selectAllItem != nullptr);

        QVERIFY(!copyItem->property("enabled").toBool());
        QVERIFY(!markersItem->property("enabled").toBool());
        QVERIFY(selectAllItem->property("enabled").toBool());

        menu->setProperty("hasSelection", true);
        QVERIFY(copyItem->property("enabled").toBool());
        QVERIFY(markersItem->property("enabled").toBool());
    }

    void overlay_menu_actions_copy_and_select_all()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        DiffLinesModel model;
        model.setDiff(twoLineDiff(), {}, false);
        DiffSelection selection;
        selection.setModel(&model);
        engine.rootContext()->setContextProperty(QStringLiteral("testSelection"), &selection);

        QQmlComponent comp(&engine);
        std::unique_ptr<QObject> root(buildOverlayHost(engine, comp));
        QVERIFY2(root != nullptr, qPrintable(comp.errorString()));
        QObject* overlay = root->findChild<QObject*>(QStringLiteral("overlay"));
        QObject* menu = root->findChild<QObject*>(QStringLiteral("diffContextMenu"));
        QVERIFY(menu != nullptr);
        QSignalSpy copySpy(overlay, SIGNAL(copyRequested(QString)));

        QMetaObject::invokeMethod(menu, "selectAll");
        QVERIFY(selection.property("hasSelection").toBool());

        QMetaObject::invokeMethod(menu, "copy");
        QCOMPARE(copySpy.count(), 1);
        QCOMPARE(copySpy.at(0).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\nctx one\nadded two\n"));

        QMetaObject::invokeMethod(menu, "copyWithMarkers");
        QCOMPARE(copySpy.count(), 2);
        QCOMPARE(copySpy.at(1).at(0).toString(),
                 QStringLiteral("@@ -1,1 +1,2 @@\n  ctx one\n+ added two\n"));
    }
```

(Signals are invokable by name from `QMetaObject::invokeMethod`, so emitting
`copy()` this way exercises the overlay's handler exactly as a click would.)

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — `qrc:/qml/DiffContextMenu.qml` does not exist.

- [ ] **Step 3: Write the menu**

Create `ui/qml/DiffContextMenu.qml`, following `FileContextMenu.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic

// Right-click menu for the diff pane. Instantiate once per diff view, set
// hasSelection, then call popup(). The copy items are disabled — not hidden —
// when nothing is selected, so the menu keeps a stable shape.
AppMenu {
    id: menu
    objectName: "diffContextMenu"

    property bool hasSelection: false

    signal copy()
    signal copyWithMarkers()
    signal selectAll()

    AppMenuItem {
        objectName: "diffMenuCopy"
        text: qsTr("Copy")
        enabled: menu.hasSelection
        onTriggered: menu.copy()
    }
    AppMenuItem {
        objectName: "diffMenuCopyMarkers"
        text: qsTr("Copy with Diff Markers")
        enabled: menu.hasSelection
        onTriggered: menu.copyWithMarkers()
    }

    AppMenuSeparator {}

    AppMenuItem {
        objectName: "diffMenuSelectAll"
        text: qsTr("Select All")
        onTriggered: menu.selectAll()
    }
}
```

Add to `ui/qml/qml.qrc`:

```xml
    <file>DiffContextMenu.qml</file>
```

- [ ] **Step 4: Wire it into the overlay**

In `ui/qml/DiffSelectionOverlay.qml`, accept the right button:

```qml
    acceptedButtons: Qt.LeftButton | Qt.RightButton
```

replace `onPressed` with a version that routes the right button to the menu
without touching the selection:

```qml
    onPressed: function(mouse) {
        overlay.forceActiveFocus()
        if (mouse.button === Qt.RightButton) {
            // Right-click never changes the selection — it acts on it.
            contextMenu.popup()
            mouse.accepted = true
            return
        }
        mouse.accepted = pressAt(mouse.x, mouse.y, mouse.modifiers)
    }
```

and add the menu as a child of the overlay:

```qml
    DiffContextMenu {
        id: contextMenu
        hasSelection: overlay.selection ? overlay.selection.hasSelection : false
        onCopy: overlay.copyRequested(overlay.selection.copyText(false))
        onCopyWithMarkers: overlay.copyRequested(overlay.selection.copyText(true))
        onSelectAll: if (overlay.selection) overlay.selection.selectAll()
    }
```

- [ ] **Step 5: Run to verify the tests pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS.

- [ ] **Step 6: Document the menu**

Add a diff-pane section to `docs/spec/product/context-menus.md` in that file's
existing style: the three items, and the rule that the copy items are disabled
(not hidden) without a selection.

- [ ] **Step 7: Commit**

```bash
git add ui/qml/DiffContextMenu.qml ui/qml/DiffSelectionOverlay.qml ui/qml/qml.qrc \
        tests/ui/test_qml_diff_selection.cpp docs/spec/product/context-menus.md
git commit -m "feat(ui): diff context menu with copy and select all"
```

---

## Task 9: Wire `DiffView.qml`

**Files:**
- Modify: `ui/qml/DiffView.qml`
- Test: `tests/ui/test_qml_diff_selection.cpp`

**Interfaces:**
- Consumes: `DiffCodeText`, `DiffSelectionOverlay`, `DiffSelection`, `repoVm.copyToClipboard(QString)` (existing, `ui/include/gittide/ui/repoviewmodel.hpp:377`).
- Produces: object names `diffSelection` and `diffSelectionOverlay` inside `DiffView.qml`.

- [ ] **Step 1: Write the failing test**

Add to `class TestQmlDiffSelection`:

```cpp
    void diff_view_declares_a_selection_bound_to_its_list_model()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        // Cast the null explicitly — setContextProperty(QString, QObject*) is
        // ambiguous with the QVariant overload for a bare nullptr.
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"),
                                                 static_cast<QObject*>(nullptr));

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/DiffView.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> view(comp.create());
        QVERIFY2(view != nullptr, qPrintable(comp.errorString()));

        QObject* selection = view->findChild<QObject*>(QStringLiteral("diffSelection"));
        QObject* overlay = view->findChild<QObject*>(QStringLiteral("diffSelectionOverlay"));
        QObject* list = view->findChild<QObject*>(QStringLiteral("diffList"));
        QVERIFY(selection != nullptr);
        QVERIFY(overlay != nullptr);
        QVERIFY(list != nullptr);

        // The overlay drives that selection over that list.
        QCOMPARE(overlay->property("selection").value<QObject*>(), selection);
        QCOMPARE(overlay->property("list").value<QObject*>(), list);
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — no child named `diffSelection`.

- [ ] **Step 3: Modify `ui/qml/DiffView.qml`**

Add the import at the top, next to the existing ones:

```qml
import GitTide
```

Declare the selection inside the root `ColumnLayout` (a non-visual object goes to
`data`, not the layout):

```qml
    // Text selection shared by every diff row. Follows whichever model the list
    // is showing, and clears itself on a reset — a file switch, a refresh, or
    // entering/leaving stash preview.
    DiffSelection {
        id: diffSelection
        objectName: "diffSelection"
        model: diffList.model
    }
```

Wrap the `ListView` so the overlay can be its sibling — replace

```qml
    ListView {
        id: diffList
        objectName: "diffList"
        visible: diffView.hasContent
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
```

with

```qml
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: diffView.hasContent

        ListView {
            id: diffList
            objectName: "diffList"
            anchors.fill: parent
            clip: true
```

Keep the rest of the `ListView` body unchanged (model, scrollbar, `WheelScroller`,
delegate), close it, and add the overlay before closing the wrapper `Item`:

```qml
        }

        DiffSelectionOverlay {
            objectName: "diffSelectionOverlay"
            anchors.fill: parent
            list: diffList
            selection: diffSelection
            onCopyRequested: function(text) { if (repoVm) repoVm.copyToClipboard(text) }
        }
    }
```

Replace the delegate's code `Label` (the last child of the normal-row
`RowLayout`) with:

```qml
                DiffCodeText {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    row: index
                    selection: diffSelection
                    plainText: model.lineText
                    html: model.lineHtml ? model.lineHtml : ""
                    color: model.lineKind === "hunk"               ? theme.textMuted
                         : (model.lineHtml && model.lineHtml.length > 0) ? theme.textPrimary
                         : model.lineKind === "added"              ? theme.stateAdded
                         : model.lineKind === "removed"            ? theme.stateDeleted
                         : model.lineKind === "ours"               ? theme.stateAdded
                         : model.lineKind === "theirs"             ? theme.stateIncoming
                         : model.lineKind === "conflict-sep"       ? theme.textMuted
                         : model.lineKind === "conflict-end"       ? theme.textMuted
                         : theme.textPrimary
                }
```

Leave the conflict-start header `Label` as it is — those rows are not selectable
by design.

- [ ] **Step 4: Run to verify the test passes**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS, and `TestQmlShell` still green.

- [ ] **Step 5: Verify by hand in the running app**

Run the app, open a repo with changes, select a file, then check: drag-select
across several rows; drag past the bottom edge and confirm autoscroll extends the
selection; `Ctrl+C` and paste; `Ctrl+Shift+C` and paste; double- and triple-click;
right-click → the three menu items; click a line checkbox and a block checkbox and
confirm staging still works; on a conflicted file, confirm the Accept buttons
still respond.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/DiffView.qml tests/ui/test_qml_diff_selection.cpp
git commit -m "feat(ui): selectable, copyable text in the working-tree diff"
```

---

## Task 10: Wire `CommitDetail.qml`

**Files:**
- Modify: `ui/qml/CommitDetail.qml`
- Test: `tests/ui/test_qml_diff_selection.cpp`

**Interfaces:**
- Consumes: everything Task 9 consumes.
- Produces: object names `commitDiffSelection` and `commitDiffSelectionOverlay`.

- [ ] **Step 1: Write the failing test**

Add to `class TestQmlDiffSelection`:

```cpp
    void commit_detail_declares_its_own_selection_over_the_commit_diff()
    {
        ThemeManager mgr;
        QmlTheme theme(&mgr);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"),
                                                 static_cast<QObject*>(nullptr));
        engine.rootContext()->setContextProperty(QStringLiteral("avatarService"),
                                                 static_cast<QObject*>(nullptr));

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/CommitDetail.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> detail(comp.create());
        QVERIFY2(detail != nullptr, qPrintable(comp.errorString()));

        QObject* selection = detail->findChild<QObject*>(QStringLiteral("commitDiffSelection"));
        QObject* overlay = detail->findChild<QObject*>(QStringLiteral("commitDiffSelectionOverlay"));
        QObject* list = detail->findChild<QObject*>(QStringLiteral("commitDiffList"));
        QVERIFY(selection != nullptr);
        QVERIFY(overlay != nullptr);
        QCOMPARE(overlay->property("selection").value<QObject*>(), selection);
        QCOMPARE(overlay->property("list").value<QObject*>(), list);
    }
```

If `CommitDetail.qml` needs context properties beyond `repoVm` / `theme` /
`avatarService` to instantiate, add them as `nullptr` context properties the same
way — the test only cares that the component loads.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — no child named `commitDiffSelection`.

- [ ] **Step 3: Modify `ui/qml/CommitDetail.qml`**

Add `import GitTide` at the top. Declare the selection near the root:

```qml
    // Commit diffs are read-only, but selectable and copyable like the working
    // diff. A separate DiffSelection: the two lists select independently.
    DiffSelection {
        id: commitDiffSelection
        objectName: "commitDiffSelection"
        model: commitDiffList.model
    }
```

Wrap the read-only diff `ListView` (currently `SplitView.fillHeight: true`) in an
`Item` that carries the SplitView attached properties, and put the overlay
alongside it:

```qml
        Item {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 120

            ListView {
                id: commitDiffList
                objectName: "commitDiffList"
                anchors.fill: parent
                clip: true
                model: repoVm ? repoVm.commitDiff : null

                ScrollBar.vertical: AppScrollBar {}
                WheelScroller {}

                // The existing delegate Rectangle, moved verbatim except for the
                // code Label replacement in the next block.
                delegate: Rectangle { /* … */ }
            }

            DiffSelectionOverlay {
                objectName: "commitDiffSelectionOverlay"
                anchors.fill: parent
                list: commitDiffList
                selection: commitDiffSelection
                onCopyRequested: function(text) { if (repoVm) repoVm.copyToClipboard(text) }
            }
        }
```

Replace the delegate's code `Label` with the `DiffCodeText` form, keeping this
view's own colour expression:

```qml
                    DiffCodeText {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        row: index
                        selection: commitDiffSelection
                        plainText: model.lineText
                        html: model.lineHtml ? model.lineHtml : ""
                        color: model.lineKind === "hunk"   ? theme.textMuted
                             : (model.lineHtml && model.lineHtml.length > 0) ? theme.textPrimary
                             : model.lineKind === "added"  ? theme.stateAdded
                             : model.lineKind === "removed" ? theme.stateDeleted
                             : theme.textPrimary
                    }
```

- [ ] **Step 4: Run to verify the test passes**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS — whole UI suite green.

- [ ] **Step 5: Verify by hand**

In the running app, open the History tab, pick a commit, and repeat the Task 9
checks on the commit diff. Confirm the two panes select independently: a
selection in the commit diff does not highlight anything in the working diff.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/CommitDetail.qml tests/ui/test_qml_diff_selection.cpp
git commit -m "feat(ui): selectable, copyable text in the commit diff"
```

---

## Task 11: Close the change

**Files:**
- Modify: `docs/decisions.md`, `docs/spec/product/product.md`, `docs/plans/index.md`, this plan

**Interfaces:** none — documentation only.

- [x] **Step 1: Record the decision**

Add an entry to `docs/decisions.md` in that file's existing format, covering:

- **Decision:** diff text selection is per-row `TextEdit` items painting slices of
  a C++-held `DiffSelection`, driven by one `MouseArea` sibling of the list.
- **Why:** the `ListView` destroys off-screen delegates, so selection state
  cannot live in a delegate, and an autoscrolling drag would kill a
  delegate-owned mouse grab.
- **Rejected — one `TextEdit` for the whole diff:** free selection for nothing,
  but it destroys per-line staging checkboxes, block rows, the line-number gutter
  and the conflict headers, which are the pane's primary function.
- **Rejected — monospace column arithmetic:** mapping x to a column by fixed
  advance width is simple and wrong for tabs, CJK widths and whatever the
  platform resolves `"monospace"` to.
- **Consequence:** long lines lost their elide ellipsis — `TextEdit` has no
  `elide` — and are hard-clipped instead. Copy is unaffected: it reads the model.

- [x] **Step 2: Update the product spec**

Add the selection/copy flow to the diff-view section of
`docs/spec/product/product.md`: what is selectable (code column only), the two
copy forms, and when the selection clears. Keep it cross-cutting — the
symbol-level facts belong in the Doxygen comments already written.

- [x] **Step 3: Fill in this plan's Outcome and index it**

Set **Status** to `done` at the top of this file, fill in the Outcome section
below, and add a row for this plan to the table in `docs/plans/index.md`.

- [x] **Step 4: Full verification**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: the whole suite passes — core and UI. Paste the summary line into the
Outcome section.

- [x] **Step 5: Commit**

```bash
git add docs/decisions.md docs/spec/product/product.md docs/plans/index.md \
        docs/superpowers/plans/2026-08-05-diff-text-selection.md
git commit -m "docs: close out diff text selection and copy"
```

---

## Outcome

- **Shipped:** character-accurate text selection and copy across the diff panel,
  in both the working-tree diff (`DiffView.qml`) and the commit/history diff
  (`CommitDetail.qml`). Selection spans rows via `gittide::ui::DiffSelection`
  (row/column in model coordinates); `DiffCodeText.qml` paints each row's slice;
  `DiffSelectionOverlay.qml` (a `MouseArea` sibling of the list) drives drag with
  autoscroll, plain/double/triple click, `Ctrl+A`/`Ctrl+C`/`Ctrl+Shift+C`, and the
  right-click `DiffContextMenu.qml` (Copy / Copy with Diff Markers / Select All).
  Two theme tokens (`selectionBg`, `selectionText`) drive the paint. Selection
  clears on any model reset — file switch, refresh, or entering/leaving stash
  preview — and each of the two diff surfaces owns its own `DiffSelection`, so
  the two panes never cross-select.
- **Spec updated:** `docs/spec/design/design.md` (token table — done in Task 1);
  `docs/spec/product/keyboard-controls.md` §2 (done in Task 7);
  `docs/spec/product/context-menus.md` §4.6 (done in Task 8) and §5 (this task —
  added a cross-reference so the "all views use TapHandler" opener doesn't
  mislead a reader about the diff overlay's and the branch row's exceptions);
  `docs/spec/product/product.md` new "Diff selection & copy" section (this task);
  `docs/decisions.md` D64 (this task).
- **Code:** `ui/include/gittide/ui/diffselection.hpp` +
  `ui/src/diffselection.cpp` (`gittide::ui::DiffSelection`), `ui/qml/DiffCodeText.qml`,
  `ui/qml/DiffSelectionOverlay.qml`, `ui/qml/DiffContextMenu.qml`; wired into
  `ui/qml/DiffView.qml` and `ui/qml/CommitDetail.qml`; theme tokens in
  `ui/include/gittide/ui/theme.hpp` / `ui/src/theme.cpp` /
  `ui/include/gittide/ui/qmltheme.hpp` / `ui/src/qmltheme.cpp`.
- **Test run:**
  ```
  100% tests passed, 0 tests failed out of 221
  Total Test time (real) =  32.50 sec
  ```
  The `gittide_ui_tests` binary within that run: `Totals` across all classes sum
  to **507 passed, 0 failed, 0 skipped, 0 blacklisted** (summed from each test
  class's own `Totals:` line — the binary itself doesn't print a grand total).
- **Outstanding — not verified by hand.** Tasks 9 and 10 each had a "verify by
  hand in the running app" step (drag-select with autoscroll, `Ctrl+C`/`Ctrl+Shift+C`
  paste, double/triple click, the right-click menu, and — critically — confirming
  the per-line/block staging checkboxes and the conflict Accept buttons still
  work, plus that the working-diff and commit-diff selections don't cross-talk).
  Every agent on this plan, including this closing task, ran headless
  (`QT_QPA_PLATFORM=offscreen`); **the app was never actually run.** This is real
  risk for the two "must not regress" items called out in the plan's global
  constraints (staging checkboxes, conflict Accept buttons) — the QML tests cover
  the overlay's hit-testing logic against a stub list, not a real `ListView` with
  those controls composited underneath it. A manual pass through Task 9 Step 5 and
  Task 10 Step 5 is still owed before this is trusted in the running app.
