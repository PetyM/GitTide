# Plan 13 — Diff block checkbox (QML)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-06-22 |
| **Status** | `done` |
| **Spec** | [`spec/product/product.md` §Changes tab](../spec/product/product.md) |
| **Depends on** | Plan 9b — QML Changes/diff (DiffView.qml, DiffLinesModel) |

**Goal:** In the editable Changes diff, render a single tri-state **block
checkbox** above every run of two or more consecutive changed lines, so the user
can stage/unstage a whole change in one click.

**Architecture:** `DiffLinesModel` (the flattened QML list model: hunk-header +
line rows) gains a `"block"` row kind, inserted before each 2+ run of consecutive
changed lines. A new `blockState` role drives a tri-state `AppCheckBox` in
`DiffView.qml`. Toggling the block calls `RepoViewModel::setBlockChecked`, which
sets each covered line via the existing per-line path — so `lineToggled` →
`onLineToggled` selection plumbing is reused unchanged. Block rows are opt-in via
a new `setDiff(..., blocks)` flag, set only for the editable working diff; the
read-only history diff (`commitDiff`) passes `false` and is unaffected.

**Tech stack:** Qt 6 QML (`QtQuick.Controls.Basic`), `QAbstractListModel`,
QtTest, C++23.

## Global constraints

- **No Qt in `core/`** — this plan touches only `ui/` and `tests/ui/`; no core edit.
- `DiffLinesModel` feeds **both** `diffLines` (editable) and `commitDiff`
  (read-only history). Block rows appear ONLY when `setDiff` is called with
  `blocks == true`, which is ONLY the editable call site
  (`repoviewmodel.cpp` ~line 298). The `commitDiff` call (~line 440) stays
  `blocks == false`. The new `blocks` parameter defaults to `false`.
- A block toggle must drive the existing `lineToggled` signal (one emit per
  covered changed line via `setLineChecked`); it must NOT introduce a parallel
  selection path. `RepoViewModel::onLineToggled` stays the single sink.
- Block rows are NOT checkable lines: `checkable == false`, `checkableCount()` /
  `checkedCount()` / `checkedLines()` must ignore them (they already filter on
  `checkable`, so block rows must keep `checkable = false`).
- `blockState` is an `int` holding a `Qt::CheckState` value: `Unchecked = 0`,
  `PartiallyChecked = 1`, `Checked = 2`.
- New `ui/` sources are not added; existing files are edited. New tests go in the
  existing `tests/ui/test_diff_lines_model.cpp` / `test_repo_view_model.cpp`,
  already registered in the `gittide_ui_tests` target — no `CMakeLists` change.
- Colour only from a `theme` token in QML; no hex literals in widgets/QML.
- Allman braces, `m_` members, lowercase file names.

---

## Task 1: `DiffLinesModel` renders block rows with initial tri-state

Add the `blocks` flag, the `"block"` row kind, the `blockState` role, and
per-line back-references. Existing tests use the 3-arg `setDiff` (default
`blocks=false`) and must stay green unchanged; new tests pass `blocks=true`.

**Files:**
- Modify: `ui/include/gittide/ui/difflinesmodel.hpp`
- Modify: `ui/src/difflinesmodel.cpp`
- Test: `tests/ui/test_diff_lines_model.cpp`

**Interfaces (produced — used by Task 2 & 3):**

```cpp
// difflinesmodel.hpp
enum Roles
{
    KindRole = Qt::UserRole + 1,
    OldNoRole,
    NewNoRole,
    TextRole,
    CheckableRole,
    CheckedRole,
    HunkRole,
    LineRole,
    BlockStateRole, // int Qt::CheckState; meaningful only on "block" rows
};

// blocks: when true, insert a "block" row before each run of 2+ consecutive
// changed (added/removed) lines. Defaults false (history/read-only diff).
void setDiff(const gittide::DiffResult& result,
             const std::map<int, std::vector<int>>& checkedLines,
             bool wholeChecked,
             bool blocks = false);

// (added in Task 2)
Q_INVOKABLE void setBlockChecked(int row, bool checked);
```

New `Row` fields and private helpers:

```cpp
struct Row
{
    QString kind; // "hunk" | "context" | "added" | "removed" | "block"
    int     oldNo = -1;
    int     newNo = -1;
    QString text;
    bool    checkable = false;
    bool    checked   = false;
    int     hunkIndex = -1;
    int     lineIndex = -1;
    // Block support:
    int              blockRow   = -1; // on a line row: index of its block row, or -1
    std::vector<int> coveredRows;     // on a "block" row: the line rows it controls
    int              blockState = 0;  // on a "block" row: Qt::CheckState as int
};

private:
    Row  makeLineRow(const gittide::DiffLine& line, int hunkIndex, int lineIndex,
                     bool wholeChecked,
                     const std::map<int, std::vector<int>>& checkedLines) const;
    int  computeBlockState(int blockRow);   // sets m_rows[blockRow].blockState, no signal
    void refreshBlock(int blockRow);        // computeBlockState + emit dataChanged
```

- [ ] **Step 1: Write the failing tests**

Add to `class TestDiffLinesModel`. The `oneHunkDiff()` fixture is `ctx, added,
removed` — `added`+`removed` are a consecutive 2-run, so with `blocks=true` the
flattened rows become `[0]=hunk, [1]=context, [2]=block, [3]=added, [4]=removed`.

```cpp
    void block_row_inserted_over_consecutive_run()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true, /*blocks=*/true);

        // hunk + context + block + added + removed
        QCOMPARE(m.rowCount(QModelIndex()), 5);

        const int kind = roleKey(m, "lineKind");
        QCOMPARE(m.data(m.index(2, 0), kind).toString(), QStringLiteral("block"));
        QCOMPARE(m.data(m.index(3, 0), kind).toString(), QStringLiteral("added"));
        QCOMPARE(m.data(m.index(4, 0), kind).toString(), QStringLiteral("removed"));

        // The block row is not itself a checkable line.
        const int checkable = roleKey(m, "checkable");
        QCOMPARE(m.data(m.index(2, 0), checkable).toBool(), false);
        QCOMPARE(m.checkableCount(), 2); // unchanged: only the two changed lines
    }

    void block_initial_state_checked_when_whole_checked()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true, /*blocks=*/true);
        const int blockState = roleKey(m, "blockState");
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::Checked));
    }

    void block_initial_state_unchecked_when_none_checked()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, false, /*blocks=*/true);
        const int blockState = roleKey(m, "blockState");
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::Unchecked));
    }

    void block_initial_state_partial_when_some_checked()
    {
        DiffLinesModel m;
        // Only lineIndex 1 (the "added" line) checked in hunk 0; "removed" (idx 2) not.
        std::map<int, std::vector<int>> checked{{0, {1}}};
        m.setDiff(oneHunkDiff(), checked, false, /*blocks=*/true);
        const int blockState = roleKey(m, "blockState");
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::PartiallyChecked));
    }

    void no_block_row_when_blocks_disabled()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true, /*blocks=*/false);
        // Same as legacy flatten: hunk + 3 lines, no block row.
        QCOMPARE(m.rowCount(QModelIndex()), 4);
        const int kind = roleKey(m, "lineKind");
        QCOMPARE(m.data(m.index(2, 0), kind).toString(), QStringLiteral("added"));
    }

    void no_block_row_for_lone_changed_line()
    {
        // hunk: context, added, context  -> the added line is a lone run.
        gittide::DiffLine c1; c1.origin = gittide::DiffLineOrigin::Context;
        c1.oldLineno = 1; c1.newLineno = 1; c1.text = "a";
        gittide::DiffLine ad; ad.origin = gittide::DiffLineOrigin::Added;
        ad.oldLineno = -1; ad.newLineno = 2; ad.text = "b";
        gittide::DiffLine c2; c2.origin = gittide::DiffLineOrigin::Context;
        c2.oldLineno = 2; c2.newLineno = 3; c2.text = "c";
        gittide::DiffHunk h; h.oldStart = 1; h.oldLines = 2; h.newStart = 1; h.newLines = 3;
        h.lines = {c1, ad, c2};
        gittide::DiffResult r; r.hunks = {h};

        DiffLinesModel m;
        m.setDiff(r, {}, true, /*blocks=*/true);
        // hunk + ctx + added + ctx, no block row.
        QCOMPARE(m.rowCount(QModelIndex()), 4);
        const int kind = roleKey(m, "lineKind");
        for (int i = 0; i < m.rowCount(QModelIndex()); ++i)
            QVERIFY(m.data(m.index(i, 0), kind).toString() != QStringLiteral("block"));
    }
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — `setDiff` does not accept a 4th arg (compile error) until
implemented; once it compiles, block-row assertions fail.

- [ ] **Step 3: Add roles, fields, and helper declarations to `difflinesmodel.hpp`**

Add `BlockStateRole` to the `Roles` enum (after `LineRole`), the three `Row`
fields (`blockRow`, `coveredRows`, `blockState`), the `blocks` parameter on
`setDiff`, and the `makeLineRow` / `computeBlockState` / `refreshBlock` private
declarations — exactly as in the Interfaces block above. (`setBlockChecked` is
added in Task 2.)

- [ ] **Step 4: Implement in `difflinesmodel.cpp`**

Add the `BlockStateRole` data case and role name:

```cpp
    case BlockStateRole:
        return r.blockState;
```
```cpp
        {BlockStateRole, "blockState"},
```

Extract the line-row builder (the per-line body currently inside `setDiff`) into
`makeLineRow`:

```cpp
DiffLinesModel::Row DiffLinesModel::makeLineRow(
    const gittide::DiffLine& line, int hunkIndex, int lineIndex, bool wholeChecked,
    const std::map<int, std::vector<int>>& checkedLines) const
{
    Row r;
    r.oldNo     = line.oldLineno;
    r.newNo     = line.newLineno;
    r.text      = QString::fromStdString(line.text);
    r.hunkIndex = hunkIndex;
    r.lineIndex = lineIndex;
    switch (line.origin)
    {
    case gittide::DiffLineOrigin::Added:
        r.kind      = QStringLiteral("added");
        r.checkable = true;
        break;
    case gittide::DiffLineOrigin::Removed:
        r.kind      = QStringLiteral("removed");
        r.checkable = true;
        break;
    case gittide::DiffLineOrigin::Context:
    default:
        r.kind      = QStringLiteral("context");
        r.checkable = false;
        break;
    }
    if (r.checkable)
    {
        if (wholeChecked)
        {
            r.checked = true;
        }
        else
        {
            const auto it = checkedLines.find(hunkIndex);
            r.checked     = it != checkedLines.end() &&
                        std::find(it->second.begin(), it->second.end(), lineIndex) != it->second.end();
        }
    }
    return r;
}

int DiffLinesModel::computeBlockState(int blockRow)
{
    const Row& b = m_rows[static_cast<std::size_t>(blockRow)];
    int total = 0, checked = 0;
    for (int cr : b.coveredRows)
    {
        const Row& r = m_rows[static_cast<std::size_t>(cr)];
        if (!r.checkable)
            continue;
        ++total;
        if (r.checked)
            ++checked;
    }
    const int state = checked == 0 ? int(Qt::Unchecked)
                      : checked == total ? int(Qt::Checked)
                                         : int(Qt::PartiallyChecked);
    m_rows[static_cast<std::size_t>(blockRow)].blockState = state;
    return state;
}

void DiffLinesModel::refreshBlock(int blockRow)
{
    computeBlockState(blockRow);
    const QModelIndex idx = index(blockRow, 0);
    emit dataChanged(idx, idx, {BlockStateRole});
}
```

Rewrite the `setDiff` body to scan runs and insert block rows:

```cpp
void DiffLinesModel::setDiff(const gittide::DiffResult& result,
                             const std::map<int, std::vector<int>>& checkedLines,
                             bool wholeChecked,
                             bool blocks)
{
    beginResetModel();
    m_rows.clear();
    for (int h = 0; h < static_cast<int>(result.hunks.size()); ++h)
    {
        const gittide::DiffHunk& hunk = result.hunks[static_cast<std::size_t>(h)];

        Row header;
        header.kind      = QStringLiteral("hunk");
        header.text      = hunkHeader(hunk);
        header.hunkIndex = h;
        m_rows.push_back(std::move(header));

        auto isChanged = [&](int l)
        {
            const auto o = hunk.lines[static_cast<std::size_t>(l)].origin;
            return o == gittide::DiffLineOrigin::Added || o == gittide::DiffLineOrigin::Removed;
        };

        const int n = static_cast<int>(hunk.lines.size());
        int       l = 0;
        while (l < n)
        {
            if (blocks && isChanged(l))
            {
                int e = l;
                while (e < n && isChanged(e))
                    ++e;

                int blockRow = -1;
                if (e - l >= 2)
                {
                    blockRow      = static_cast<int>(m_rows.size());
                    Row b;
                    b.kind        = QStringLiteral("block");
                    b.hunkIndex   = h;
                    m_rows.push_back(std::move(b));
                }

                for (int k = l; k < e; ++k)
                {
                    Row r     = makeLineRow(hunk.lines[static_cast<std::size_t>(k)], h, k,
                                            wholeChecked, checkedLines);
                    r.blockRow = blockRow;
                    m_rows.push_back(std::move(r));
                    if (blockRow >= 0)
                        m_rows[static_cast<std::size_t>(blockRow)].coveredRows.push_back(
                            static_cast<int>(m_rows.size()) - 1);
                }
                if (blockRow >= 0)
                    computeBlockState(blockRow); // no signal during model reset

                l = e;
            }
            else
            {
                m_rows.push_back(makeLineRow(hunk.lines[static_cast<std::size_t>(l)], h, l,
                                             wholeChecked, checkedLines));
                ++l;
            }
        }
    }
    endResetModel();
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — all six new slots pass; the four pre-existing
`TestDiffLinesModel` slots (which call the 3-arg `setDiff`, `blocks` defaulting
to `false`) remain green.

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(ui): DiffLinesModel block rows over 2+ changed-line runs

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>" -- ui/include/gittide/ui/difflinesmodel.hpp ui/src/difflinesmodel.cpp tests/ui/test_diff_lines_model.cpp
```

## Task 2: Block toggle + per-line sync + RepoViewModel wiring

Toggling a block sets every covered line through the existing per-line path
(`setLineChecked` → `lineToggled`). Toggling one line refreshes its block's
tri-state. `RepoViewModel` exposes `setBlockChecked` to QML.

**Files:**
- Modify: `ui/include/gittide/ui/difflinesmodel.hpp` (declare `setBlockChecked`)
- Modify: `ui/src/difflinesmodel.cpp` (`setBlockChecked`, sync in `setLineChecked`,
  refresh in `setAllChecked`)
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` (`Q_INVOKABLE setBlockChecked`)
- Modify: `ui/src/repoviewmodel.cpp` (forward to model)
- Test: `tests/ui/test_diff_lines_model.cpp`

**Interfaces (consumes):** `computeBlockState`/`refreshBlock`/`coveredRows`/
`blockRow` from Task 1, the existing `setLineChecked` and `lineToggled`.
**Produces:** `DiffLinesModel::setBlockChecked(int,bool)` and
`RepoViewModel::setBlockChecked(int,bool)` (Q_INVOKABLE, used by Task 3 QML).

- [ ] **Step 1: Write the failing tests**

```cpp
    void toggling_block_off_unchecks_all_covered_and_emits_per_line()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true, /*blocks=*/true); // block at row 2, lines 3,4
        QCOMPARE(m.checkedCount(), 2);

        QSignalSpy spy(&m, &DiffLinesModel::lineToggled);
        m.setBlockChecked(2, false);

        QCOMPARE(spy.count(), 2);          // one emit per covered changed line
        QCOMPARE(m.checkedCount(), 0);
        const int blockState = roleKey(m, "blockState");
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::Unchecked));
        for (int e = 0; e < spy.count(); ++e)
            QCOMPARE(spy.at(e).at(2).toBool(), false);
    }

    void toggling_block_on_checks_all_covered()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, false, /*blocks=*/true);
        QCOMPARE(m.checkedCount(), 0);

        QSignalSpy spy(&m, &DiffLinesModel::lineToggled);
        m.setBlockChecked(2, true);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(m.checkedCount(), 2);
        const int blockState = roleKey(m, "blockState");
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::Checked));
    }

    void toggling_block_only_emits_for_changed_lines()
    {
        DiffLinesModel m;
        std::map<int, std::vector<int>> checked{{0, {1}}}; // "added" already checked
        m.setDiff(oneHunkDiff(), checked, false, /*blocks=*/true);

        QSignalSpy spy(&m, &DiffLinesModel::lineToggled);
        m.setBlockChecked(2, true); // only the "removed" line actually changes

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toInt(), 2); // lineIndex 2 (the removed line within hunk)
        QCOMPARE(spy.at(0).at(2).toBool(), true);
    }

    void unchecking_one_line_makes_block_partial()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true, /*blocks=*/true);
        const int blockState = roleKey(m, "blockState");
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::Checked));

        m.setLineChecked(3, false); // uncheck the "added" line
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::PartiallyChecked));

        m.setLineChecked(4, false); // uncheck the "removed" line too
        QCOMPARE(m.data(m.index(2, 0), blockState).toInt(), int(Qt::Unchecked));
    }

    void block_state_emits_datachanged_on_line_toggle()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true, /*blocks=*/true);
        const int blockState = roleKey(m, "blockState");

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        m.setLineChecked(3, false);

        // At least one dataChanged carried BlockStateRole for the block row (row 2).
        bool sawBlock = false;
        for (int i = 0; i < spy.count(); ++i)
        {
            const auto roles = spy.at(i).at(2).value<QList<int>>();
            const auto tl    = spy.at(i).at(0).value<QModelIndex>();
            if (tl.row() == 2 && roles.contains(blockState))
                sawBlock = true;
        }
        QVERIFY(sawBlock);
    }
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — `setBlockChecked` undeclared (compile error); after declaring,
the sync/emit assertions fail.

- [ ] **Step 3: Implement `setBlockChecked` + line sync + setAllChecked refresh**

Declare in `difflinesmodel.hpp` (public): `Q_INVOKABLE void setBlockChecked(int row, bool checked);`

In `difflinesmodel.cpp`, append the block sync at the end of `setLineChecked`
(after the existing `emit lineToggled(...)`):

```cpp
    const int blockRow = r.blockRow;
    if (blockRow >= 0)
        refreshBlock(blockRow);
```

Add `setBlockChecked`:

```cpp
void DiffLinesModel::setBlockChecked(int row, bool checked)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    if (m_rows[static_cast<std::size_t>(row)].kind != QStringLiteral("block"))
        return;
    // Copy: setLineChecked() refreshes the block row mid-loop.
    const std::vector<int> covered = m_rows[static_cast<std::size_t>(row)].coveredRows;
    for (int cr : covered)
        setLineChecked(cr, checked); // emits lineToggled per changed line, refreshes block
    refreshBlock(row);               // ensure correct state even if nothing changed
}
```

In `setAllChecked`, after the existing loop that flips every checkable line, add a
pass that refreshes block rows so the header "check all" keeps block boxes in
sync:

```cpp
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        if (m_rows[static_cast<std::size_t>(i)].kind == QStringLiteral("block"))
            refreshBlock(i);
```

- [ ] **Step 4: Wire `RepoViewModel::setBlockChecked`**

In `repoviewmodel.hpp`, beside `setLineChecked`/`setAllLinesChecked`:

```cpp
    Q_INVOKABLE void setBlockChecked(int row, bool checked);
```

In `repoviewmodel.cpp`, beside `setLineChecked`:

```cpp
void RepoViewModel::setBlockChecked(int row, bool checked)
{
    // Routes through DiffLinesModel; covered lines emit lineToggled() → onLineToggled().
    m_diff->setBlockChecked(row, checked);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — all five new slots pass; existing slots stay green.

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(ui): block checkbox toggles whole run and syncs with line toggles

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>" -- ui/include/gittide/ui/difflinesmodel.hpp ui/src/difflinesmodel.cpp ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp tests/ui/test_diff_lines_model.cpp
```

## Task 3: Render the block checkbox in `DiffView.qml` and enable blocks

Add a delegate branch for `lineKind === "block"` (a bare tri-state `AppCheckBox`
in the gutter column) and switch the editable working diff to `blocks = true`.

**Files:**
- Modify: `ui/qml/DiffView.qml`
- Modify: `ui/src/repoviewmodel.cpp` (the editable `setDiff` call, ~line 298)

**Interfaces (consumes):** the `blockState` role and
`RepoViewModel::setBlockChecked` from Tasks 1–2.

- [ ] **Step 1: Enable block rows for the editable diff**

In `repoviewmodel.cpp`, the editable working-diff call currently reads:

```cpp
    m_diff->setDiff(result, fs.checkedLinesByHunk, fs.state == ChangedFilesModel::Checked);
```

Change it to pass `blocks = true`:

```cpp
    m_diff->setDiff(result, fs.checkedLinesByHunk, fs.state == ChangedFilesModel::Checked, /*blocks=*/true);
```

Leave the `commitDiff` call (`m_diff/​m_commitDiff->setDiff(result, {}, false)`,
~line 440) untouched — history stays block-free.

- [ ] **Step 2: Add the block-checkbox delegate branch in `DiffView.qml`**

In the delegate's checkbox-column `Item` (the one holding the per-line
`AppCheckBox`), add a second `AppCheckBox` shown only for block rows. The
existing per-line box already gates on `visible: model.checkable` (false for
block rows), so the two never overlap:

```qml
                // Per-line checkbox column (changed lines) + block checkbox (block rows)
                Item {
                    Layout.preferredWidth: 22
                    Layout.fillHeight: true
                    AppCheckBox {
                        anchors.centerIn: parent
                        visible: model.checkable
                        checked: model.lineChecked
                        accentColor: model.lineKind === "added" ? theme.stateAdded
                                     : model.lineKind === "removed" ? theme.stateDeleted
                                     : theme.accent
                        onClicked: if (repoVm) repoVm.setLineChecked(index, !model.lineChecked)
                    }
                    AppCheckBox {
                        anchors.centerIn: parent
                        objectName: "diffBlockCheck"
                        visible: model.lineKind === "block"
                        tristate: true
                        checkState: model.blockState
                        onClicked: if (repoVm) repoVm.setBlockChecked(index, model.blockState !== Qt.Checked)
                    }
                }
```

(The block row's other columns render empty: `model.checkable` is false, its
`lineText` is empty, and `lineKind === "block"` matches none of the
added/removed/hunk colour branches, so the row is a bare checkbox — matching the
spec's "bare row".)

- [ ] **Step 3: Build and verify no QML errors**

DiffView is QML — there is no unit test for the delegate. Verify by building and
launching headless so QML type/binding errors surface:

Run: `cmake --build build --parallel`
Expected: build succeeds.

Run: `QT_QPA_PLATFORM=offscreen ./build/app/gittide --version 2>&1 | head` (or the
project's usual launch; adjust the binary path if different).
Expected: starts and exits with no `QML ... is not a type` / binding-loop /
`Unable to assign` warnings on stderr referencing `DiffView.qml`.

If a manual visual check is possible, open a repo with a multi-line change in the
Changes tab and confirm: a bare checkbox sits above each 2+ run; clicking it
toggles the whole run; unchecking one line shows the block box as partial.

- [ ] **Step 4: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS — no regression across core + UI tests.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(ui): show diff block checkbox in DiffView and enable it for edits

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>" -- ui/qml/DiffView.qml ui/src/repoviewmodel.cpp
```

---

## Outcome

> - Shipped: tri-state block checkbox over every 2+ run of consecutive changed
>   lines in the editable Changes diff; one click stages/unstages the run, and
>   toggling individual lines keeps the block box in sync (checked / partial /
>   unchecked). Lone single-line changes get none; read-only history is
>   unaffected.
> - Spec updated: [`spec/product/product.md` §Changes tab](../spec/product/product.md)
>   now describes the block checkbox.
> - Code: `ui/src/difflinesmodel.cpp` + `difflinesmodel.hpp` — `"block"` row
>   kind, `blockState` role, `setDiff(..., blocks)` flag, `setBlockChecked`,
>   `computeBlockState`/`refreshBlock`, per-line→block sync in `setLineChecked`;
>   `ui/src/repoviewmodel.cpp` — `Q_INVOKABLE setBlockChecked` + `blocks=true`
>   at the editable diff (line 304; `commitDiff` stays `false`);
>   `ui/qml/DiffView.qml` — bare tristate `AppCheckBox` for block rows.
> - Tests: 11 new model slots in `tests/ui/test_diff_lines_model.cpp` (structure,
>   initial tri-state, toggle emit-counts, reverse sync, `dataChanged` roles).
>   No `core/` change. QML delegate verified by build + offscreen smoke (no
>   delegate unit test, per codebase convention).
> - Commits: `897186c` (model rows) · `9e70a9c` (toggle + wiring) · `74c67b4`
>   (QML + enable). Final whole-branch review: ready to merge, no Critical/
>   Important findings.
