# QML Changes Pane (branch bar + file list + colored diff + commit) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the QML working pane real — a branch bar, a Changes/History tab bar, and a fully wired Changes view (tri-state file list, colored diff with per-line checkboxes, commit box) driven by the existing `RepoController` through a new QML-facing view-model layer.

**Architecture:** `RepoController` is signal-driven and exposes `QCoro::Task` methods that QML cannot call, and the QWidgets `ChangedFilesList`/`DiffView`/`BranchBar` are widgets, not data models. So this plan adds a thin QML-facing layer over the unchanged `RepoController`: two `QAbstractListModel`s (`ChangedFilesModel`, `DiffLinesModel`) that QML `ListView`s bind to, and one façade `QObject` (`RepoViewModel`) that owns a `RepoController` + both models + the staging-selection state, exposes `Q_PROPERTY`/`Q_INVOKABLE` for QML, and replicates the existing `ChangesView` staging/commit logic. New QML views (`WorkingPane`, `BranchBar`, `ChangesPane`, `DiffView`) replace the Plan 1 placeholder pane. The QWidgets UI and all its tests stay intact.

**Tech Stack:** C++23, Qt 6 (Quick, Qml, QuickControls2), QCoro (`QCoro::connect`, `QCoro::waitFor`), CMake ≥ 3.28, Qt Test (existing headless UI suite, `QT_QPA_PLATFORM=offscreen`).

## Global Constraints

- **No Qt in `core/`** — this plan touches only `ui/`, `app/`, `tests/`. Do not add includes to `core/`.
- **libgit2 and nlohmann/json stay PRIVATE to `core/`** — unchanged here.
- **Colour comes from a theme token, never a hex literal in a component** — QML reads `theme.<token>`. Models expose a *kind* string (`"added"`/`"modified"`/`"deleted"`/`"untracked"`) and a status *letter*; the QML delegate maps kind → `theme.stateAdded`/`stateModified`/`stateDeleted`/`stateUntracked`. No model knows a colour.
- **Paths cross the boundary via `gittide/ui/metatypes.hpp` helpers `pathToQString()` / `qstringToPath()`** (UTF-8, `generic_u8string`). Never `.string()`, never construct a path from a raw QString any other way.
- **Errors are values in `core/`** — unchanged; the view-model consumes `RepoController` signals (`operationFailed`) and re-emits, it does not throw.
- **TDD** — write the failing test first. New `ui/` sources → the `add_library(gittide_ui …)` list in `ui/CMakeLists.txt`. New UI tests → the `gittide_ui_test_sources` list in `tests/CMakeLists.txt` **and** a `#include "test_x.cpp"` + a `RUN(TestX);` line in `tests/ui/main.cpp` (miss either and it silently runs zero tests).
- **Qt 6 via `find_package`, never FetchContent** — Quick/Qml/QuickControls2 already found (Plan 1).
- **Code style:** Allman braces, `m_` members, lowercase file names, `.clang-format`. Split a pure rename from content changes into two commits.
- **Staging selection semantics (replicated from `ui/src/changesview.cpp`, do not redesign):** a `StageSelection{path, hunkIndex, lineIndices}` means whole-file when `hunkIndex == std::nullopt`; whole-hunk when `hunkIndex` set and `lineIndices` empty; specific lines when both set. Whole-file-checked → one whole-file selection; partially-checked → one selection per hunk carrying that hunk's checked line indices.
- **Tri-state values match Qt:** `Unchecked = 0`, `Partial = 1`, `Checked = 2`.
- **Commands:** configure `cmake -S . -B build`; build `cmake --build build --parallel`; UI tests `ctest --test-dir build --output-on-failure -R gittide_ui_tests`. Single target build: `cmake --build build --target gittide_ui_tests --parallel`.

---

## File Structure

**New files**
- `ui/include/gittide/ui/changedfilesmodel.hpp` / `ui/src/changedfilesmodel.cpp` — `ChangedFilesModel : QAbstractListModel`. Changed-files rows (dir/name/path/letter/kind/checkState) with whole-file tri-state. No theme, no controller.
- `ui/include/gittide/ui/difflinesmodel.hpp` / `ui/src/difflinesmodel.cpp` — `DiffLinesModel : QAbstractListModel`. Flattened diff rows (hunk header + lines) with per-line check state for added/removed lines.
- `ui/include/gittide/ui/repoviewmodel.hpp` / `ui/src/repoviewmodel.cpp` — `RepoViewModel : QObject`. Owns a `RepoController` + both models + the selection map; QML-facing properties/invokables; replicates `ChangesView` staging/commit logic.
- `ui/qml/WorkingPane.qml` — branch bar + Changes/History `TabBar` + `StackLayout`.
- `ui/qml/BranchBar.qml` — current-branch display (display-only this plan).
- `ui/qml/ChangesPane.qml` — files column (master checkbox + file list + commit box) and diff column.
- `ui/qml/DiffView.qml` — the colored diff `ListView` with per-line checkboxes.
- `tests/ui/test_changed_files_model.cpp` — `ChangedFilesModel` unit tests (pure).
- `tests/ui/test_diff_lines_model.cpp` — `DiffLinesModel` unit tests (pure).
- `tests/ui/test_repo_view_model.cpp` — `RepoViewModel` integration tests (real temp repo, async).

**Modified files**
- `ui/qml/qml.qrc` — add `WorkingPane.qml`, `BranchBar.qml`, `ChangesPane.qml`, `DiffView.qml`.
- `ui/qml/Main.qml` — replace the placeholder pane with `WorkingPane`.
- `ui/qml/Sidebar.qml` — repo-tree row activation calls `repoVm.open(repoPath)`.
- `ui/include/gittide/ui/qmlcontext.hpp` / `ui/src/qmlcontext.cpp` — `installQmlContext` gains a `RepoViewModel* repoVm` parameter (sets context property `repoVm`).
- `ui/CMakeLists.txt` — add the three new source pairs to `add_library(gittide_ui …)`.
- `app/qml_main.cpp` — construct a `RepoViewModel`, pass it to `installQmlContext`.
- `tests/CMakeLists.txt` — add the three new test files to `gittide_ui_test_sources`.
- `tests/ui/main.cpp` — `#include` the three new tests and add three `RUN(...)` lines.
- `tests/ui/test_qml_shell.cpp` — update the two existing `installQmlContext(...)` calls to pass a trailing `nullptr` for the new `repoVm` parameter.

**Explicitly deferred (NOT in this plan — named follow-ups):**
- Branch **bar actions**: branch dropdown (Local/Worktrees/Remote), switch/create/rename/delete, and Fetch/Pull/Push with ahead/behind badges. `RepoController` exposes no ahead/behind count today; the branch bar here is display-only (current branch name).
- History tab content (commit graph, gravatar list, read-only diff) — a later plan; this plan leaves a placeholder under the History tab.
- Avatar/gravatar in the commit box (initials placeholder only here).
- Overlays/dialogs, empty states, light-theme polish.
- Deleting the QWidgets UI (a later cutover plan).

---

### Task 1: `ChangedFilesModel` — changed files as a QML list model

**Files:**
- Create: `ui/include/gittide/ui/changedfilesmodel.hpp`, `ui/src/changedfilesmodel.cpp`
- Test: `tests/ui/test_changed_files_model.cpp`
- Modify: `ui/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `gittide::FileStatus`, `gittide::StatusFlag`, `gittide::hasFlag` (`core/include/gittide/filestatus.hpp`); `pathToQString` (`gittide/ui/metatypes.hpp`).
- Produces: `class ChangedFilesModel : public QAbstractListModel` with check enum `Check { Unchecked = 0, Partial = 1, Checked = 2 }`; roles `fileDir`, `fileName`, `filePath`, `statusLetter`, `statusKind`, `checkState`; methods `void setFiles(const std::vector<gittide::FileStatus>&)`, `Q_INVOKABLE void setChecked(int row, bool checked)`, `void setCheckState(int row, Check state)`, `Check checkState(int row) const`, `QString pathAt(int row) const`, `int rowForPath(const QString&) const`, `int checkedCount() const`; static `static QString letterForFlags(gittide::StatusFlag)`, `static QString kindForFlags(gittide::StatusFlag)`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_changed_files_model.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModel>

#include "gittide/ui/changedfilesmodel.hpp"
#include "gittide/filestatus.hpp"

using gittide::ui::ChangedFilesModel;
using gittide::FileStatus;
using gittide::StatusFlag;

namespace
{
int roleKey(const ChangedFilesModel& m, const QByteArray& name)
{
    const auto roles = m.roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}
}

class TestChangedFilesModel : public QObject
{
    Q_OBJECT
private slots:
    void maps_flags_to_letter_and_kind()
    {
        ChangedFilesModel m;
        std::vector<FileStatus> files;
        files.push_back({std::filesystem::path("src/a.cpp"), StatusFlag::IndexNew});
        files.push_back({std::filesystem::path("b.txt"), StatusFlag::WtModified});
        files.push_back({std::filesystem::path("c.txt"), StatusFlag::WtNew});
        m.setFiles(files);

        QCOMPARE(m.rowCount(QModelIndex()), 3);

        const int letter = roleKey(m, "statusLetter");
        const int kind   = roleKey(m, "statusKind");
        const int dir    = roleKey(m, "fileDir");
        const int name   = roleKey(m, "fileName");

        QCOMPARE(m.data(m.index(0, 0), letter).toString(), QStringLiteral("A"));
        QCOMPARE(m.data(m.index(0, 0), kind).toString(), QStringLiteral("added"));
        QCOMPARE(m.data(m.index(0, 0), dir).toString(), QStringLiteral("src/"));
        QCOMPARE(m.data(m.index(0, 0), name).toString(), QStringLiteral("a.cpp"));

        QCOMPARE(m.data(m.index(1, 0), letter).toString(), QStringLiteral("M"));
        QCOMPARE(m.data(m.index(1, 0), kind).toString(), QStringLiteral("modified"));
        QCOMPARE(m.data(m.index(1, 0), dir).toString(), QString());

        QCOMPARE(m.data(m.index(2, 0), letter).toString(), QStringLiteral("U"));
        QCOMPARE(m.data(m.index(2, 0), kind).toString(), QStringLiteral("untracked"));
    }

    void files_default_to_checked()
    {
        ChangedFilesModel m;
        m.setFiles({{std::filesystem::path("a"), StatusFlag::WtModified}});
        QCOMPARE(m.checkState(0), ChangedFilesModel::Checked);
        QCOMPARE(m.checkedCount(), 1);
    }

    void toggling_a_file_emits_datachanged_and_updates_state()
    {
        ChangedFilesModel m;
        m.setFiles({{std::filesystem::path("a"), StatusFlag::WtModified}});
        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);

        m.setChecked(0, false);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.checkState(0), ChangedFilesModel::Unchecked);
        QCOMPARE(m.checkedCount(), 0);
    }

    void path_lookup_round_trips()
    {
        ChangedFilesModel m;
        m.setFiles({{std::filesystem::path("src/a.cpp"), StatusFlag::WtModified}});
        QCOMPARE(m.pathAt(0), QStringLiteral("src/a.cpp"));
        QCOMPARE(m.rowForPath(QStringLiteral("src/a.cpp")), 0);
        QCOMPARE(m.rowForPath(QStringLiteral("nope")), -1);
    }
};

#include "test_changed_files_model.moc"
```

- [ ] **Step 2: Wire the test into the runner (both edits)**

In `tests/CMakeLists.txt`, add to the `gittide_ui_test_sources` list:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_changed_files_model.cpp
```

In `tests/ui/main.cpp`, add near the other `#include "test_*.cpp"` lines:

```cpp
#include "test_changed_files_model.cpp"
```

and in `main()`, alongside the other `RUN(...)` calls:

```cpp
    RUN(TestChangedFilesModel);
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel`
Expected: FAIL to compile — `gittide/ui/changedfilesmodel.hpp` not found.

- [ ] **Step 4: Write the header**

Create `ui/include/gittide/ui/changedfilesmodel.hpp`:

```cpp
#pragma once
#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "gittide/filestatus.hpp"

namespace gittide::ui {

/// QML list model of changed files. One row per FileStatus. Carries a whole-file
/// tri-state check (Unchecked/Partial/Checked) used to build StageSelections at
/// commit time. Knows nothing about themes: it exposes a status letter
/// ("A"/"M"/"D"/"U"/"?") and a status kind ("added"/"modified"/"deleted"/
/// "untracked") and lets the QML delegate pick the colour token.
class ChangedFilesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Check
    {
        Unchecked = 0,
        Partial   = 1,
        Checked   = 2,
    };
    Q_ENUM(Check)

    enum Roles
    {
        DirRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        LetterRole,
        KindRole,
        CheckRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setFiles(const std::vector<gittide::FileStatus>& files);

    Q_INVOKABLE void setChecked(int row, bool checked);
    void setCheckState(int row, Check state);
    Check checkState(int row) const;
    QString pathAt(int row) const;
    int rowForPath(const QString& path) const;
    int checkedCount() const;

    static QString letterForFlags(gittide::StatusFlag flags);
    static QString kindForFlags(gittide::StatusFlag flags);

private:
    struct Row
    {
        QString dir;
        QString name;
        QString path;
        QString letter;
        QString kind;
        Check   check = Checked;
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
```

- [ ] **Step 5: Write the implementation**

Create `ui/src/changedfilesmodel.cpp`:

```cpp
#include "gittide/ui/changedfilesmodel.hpp"

#include "gittide/ui/metatypes.hpp"

namespace gittide::ui {

QString ChangedFilesModel::letterForFlags(gittide::StatusFlag flags)
{
    using F = gittide::StatusFlag;
    if (gittide::hasFlag(flags, F::IndexNew))
        return QStringLiteral("A");
    if (gittide::hasFlag(flags, F::IndexModified))
        return QStringLiteral("M");
    if (gittide::hasFlag(flags, F::IndexDeleted))
        return QStringLiteral("D");
    if (gittide::hasFlag(flags, F::WtNew))
        return QStringLiteral("U");
    if (gittide::hasFlag(flags, F::WtModified))
        return QStringLiteral("M");
    if (gittide::hasFlag(flags, F::WtDeleted))
        return QStringLiteral("D");
    return QStringLiteral("?");
}

QString ChangedFilesModel::kindForFlags(gittide::StatusFlag flags)
{
    using F = gittide::StatusFlag;
    if (gittide::hasFlag(flags, F::IndexNew))
        return QStringLiteral("added");
    if (gittide::hasFlag(flags, F::IndexModified))
        return QStringLiteral("modified");
    if (gittide::hasFlag(flags, F::IndexDeleted))
        return QStringLiteral("deleted");
    if (gittide::hasFlag(flags, F::WtNew))
        return QStringLiteral("untracked");
    if (gittide::hasFlag(flags, F::WtModified))
        return QStringLiteral("modified");
    if (gittide::hasFlag(flags, F::WtDeleted))
        return QStringLiteral("deleted");
    return QStringLiteral("modified");
}

int ChangedFilesModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant ChangedFilesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case DirRole:
        return r.dir;
    case NameRole:
        return r.name;
    case PathRole:
        return r.path;
    case LetterRole:
        return r.letter;
    case KindRole:
        return r.kind;
    case CheckRole:
        return static_cast<int>(r.check);
    default:
        return {};
    }
}

QHash<int, QByteArray> ChangedFilesModel::roleNames() const
{
    return {
        {DirRole, "fileDir"},
        {NameRole, "fileName"},
        {PathRole, "filePath"},
        {LetterRole, "statusLetter"},
        {KindRole, "statusKind"},
        {CheckRole, "checkState"},
    };
}

void ChangedFilesModel::setFiles(const std::vector<gittide::FileStatus>& files)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(files.size());
    for (const auto& f : files)
    {
        const QString full = pathToQString(f.path);
        const int slash    = full.lastIndexOf(QLatin1Char('/'));
        Row r;
        r.dir    = slash >= 0 ? full.left(slash + 1) : QString();
        r.name   = slash >= 0 ? full.mid(slash + 1) : full;
        r.path   = full;
        r.letter = letterForFlags(f.flags);
        r.kind   = kindForFlags(f.flags);
        r.check  = Checked;
        m_rows.push_back(std::move(r));
    }
    endResetModel();
}

void ChangedFilesModel::setChecked(int row, bool checked)
{
    setCheckState(row, checked ? Checked : Unchecked);
}

void ChangedFilesModel::setCheckState(int row, Check state)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    if (m_rows[static_cast<std::size_t>(row)].check == state)
        return;
    m_rows[static_cast<std::size_t>(row)].check = state;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {CheckRole});
}

ChangedFilesModel::Check ChangedFilesModel::checkState(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return Unchecked;
    return m_rows[static_cast<std::size_t>(row)].check;
}

QString ChangedFilesModel::pathAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return {};
    return m_rows[static_cast<std::size_t>(row)].path;
}

int ChangedFilesModel::rowForPath(const QString& path) const
{
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        if (m_rows[static_cast<std::size_t>(i)].path == path)
            return i;
    return -1;
}

int ChangedFilesModel::checkedCount() const
{
    int n = 0;
    for (const auto& r : m_rows)
        if (r.check != Unchecked)
            ++n;
    return n;
}

} // namespace gittide::ui
```

- [ ] **Step 6: Add the sources to `gittide_ui`**

In `ui/CMakeLists.txt`, add inside the `add_library(gittide_ui STATIC …)` list (e.g. after the `changesview` entries):

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/changedfilesmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/changedfilesmodel.hpp
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `TestChangedFilesModel` cases green.

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/changedfilesmodel.hpp ui/src/changedfilesmodel.cpp ui/CMakeLists.txt \
        tests/ui/test_changed_files_model.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): ChangedFilesModel — changed files as a QML list model"
```

---

### Task 2: `DiffLinesModel` — flattened colored diff as a QML list model

**Files:**
- Create: `ui/include/gittide/ui/difflinesmodel.hpp`, `ui/src/difflinesmodel.cpp`
- Test: `tests/ui/test_diff_lines_model.cpp`
- Modify: `ui/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `gittide::DiffResult`, `gittide::DiffHunk`, `gittide::DiffLine`, `gittide::DiffLineOrigin` (`core/include/gittide/diff.hpp`).
- Produces: `class DiffLinesModel : public QAbstractListModel` with roles `lineKind` (`"hunk"`/`"context"`/`"added"`/`"removed"`), `oldNo` (int, `-1` when absent), `newNo` (int), `lineText`, `checkable` (bool), `lineChecked` (bool), `hunkIndex` (int), `lineIndex` (int); methods `void setDiff(const gittide::DiffResult&, const std::map<int, std::vector<int>>& checkedLines, bool wholeChecked)`, `void clear()`, `Q_INVOKABLE void setLineChecked(int row, bool checked)`, `void setAllChecked(bool checked)`, `int checkableCount() const`, `int checkedCount() const`, `std::map<int, std::vector<int>> checkedLines() const`; signal `void lineToggled(int hunkIndex, int lineIndex, bool checked)`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_diff_lines_model.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModel>

#include "gittide/ui/difflinesmodel.hpp"
#include "gittide/diff.hpp"

using gittide::ui::DiffLinesModel;

namespace
{
int roleKey(const DiffLinesModel& m, const QByteArray& name)
{
    const auto roles = m.roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name)
            return it.key();
    return -1;
}

gittide::DiffResult oneHunkDiff()
{
    gittide::DiffLine ctx;
    ctx.origin    = gittide::DiffLineOrigin::Context;
    ctx.oldLineno = 1;
    ctx.newLineno = 1;
    ctx.text      = "ctx";
    gittide::DiffLine added;
    added.origin    = gittide::DiffLineOrigin::Added;
    added.oldLineno = -1;
    added.newLineno = 2;
    added.text      = "new";
    gittide::DiffLine removed;
    removed.origin    = gittide::DiffLineOrigin::Removed;
    removed.oldLineno = 2;
    removed.newLineno = -1;
    removed.text      = "old";

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
}

class TestDiffLinesModel : public QObject
{
    Q_OBJECT
private slots:
    void flattens_hunk_header_plus_lines()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, false);

        // 1 hunk header + 3 lines
        QCOMPARE(m.rowCount(QModelIndex()), 4);

        const int kind      = roleKey(m, "lineKind");
        const int checkable = roleKey(m, "checkable");

        QCOMPARE(m.data(m.index(0, 0), kind).toString(), QStringLiteral("hunk"));
        QCOMPARE(m.data(m.index(1, 0), kind).toString(), QStringLiteral("context"));
        QCOMPARE(m.data(m.index(2, 0), kind).toString(), QStringLiteral("added"));
        QCOMPARE(m.data(m.index(3, 0), kind).toString(), QStringLiteral("removed"));

        QCOMPARE(m.data(m.index(0, 0), checkable).toBool(), false);
        QCOMPARE(m.data(m.index(1, 0), checkable).toBool(), false);
        QCOMPARE(m.data(m.index(2, 0), checkable).toBool(), true);
        QCOMPARE(m.data(m.index(3, 0), checkable).toBool(), true);

        QCOMPARE(m.checkableCount(), 2);
    }

    void whole_checked_marks_all_changed_lines()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true);
        QCOMPARE(m.checkedCount(), 2);
        const auto checked = m.checkedLines();
        QCOMPARE(static_cast<int>(checked.size()), 1);
        QCOMPARE(static_cast<int>(checked.at(0).size()), 2); // both changed lines of hunk 0
    }

    void toggling_a_line_emits_linetoggled_and_updates_checked()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, false);
        QCOMPARE(m.checkedCount(), 0);

        QSignalSpy spy(&m, &DiffLinesModel::lineToggled);
        m.setLineChecked(2, true); // the "added" row

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0); // hunkIndex
        QCOMPARE(spy.at(0).at(1).toInt(), 1); // lineIndex within hunk (the 2nd line)
        QCOMPARE(spy.at(0).at(2).toBool(), true);
        QCOMPARE(m.checkedCount(), 1);
    }

    void clear_empties_the_model()
    {
        DiffLinesModel m;
        m.setDiff(oneHunkDiff(), {}, true);
        m.clear();
        QCOMPARE(m.rowCount(QModelIndex()), 0);
        QCOMPARE(m.checkableCount(), 0);
    }
};

#include "test_diff_lines_model.moc"
```

- [ ] **Step 2: Wire the test into the runner (both edits)**

In `tests/CMakeLists.txt`, add to `gittide_ui_test_sources`:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_diff_lines_model.cpp
```

In `tests/ui/main.cpp`, add the include:

```cpp
#include "test_diff_lines_model.cpp"
```

and the run line:

```cpp
    RUN(TestDiffLinesModel);
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel`
Expected: FAIL to compile — `gittide/ui/difflinesmodel.hpp` not found.

- [ ] **Step 4: Write the header**

Create `ui/include/gittide/ui/difflinesmodel.hpp`:

```cpp
#pragma once
#include <map>
#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "gittide/diff.hpp"

namespace gittide::ui {

/// QML list model of a single file's diff, flattened: one row for each hunk
/// header followed by one row per line. Added/Removed lines are checkable for
/// line-level staging; Context lines and hunk headers are not. Carries (hunkIndex,
/// lineIndex) on every line so a check maps back to a StageSelection. Emits
/// lineToggled() so the owning RepoViewModel can update its selection state.
class DiffLinesModel : public QAbstractListModel
{
    Q_OBJECT
public:
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
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // checkedLines: hunkIndex -> line indices (within the hunk) that are checked.
    // wholeChecked: when true, every changed line is checked regardless of checkedLines.
    void setDiff(const gittide::DiffResult& result, const std::map<int, std::vector<int>>& checkedLines, bool wholeChecked);
    void clear();

    Q_INVOKABLE void setLineChecked(int row, bool checked);
    void setAllChecked(bool checked);

    int checkableCount() const;
    int checkedCount() const;
    std::map<int, std::vector<int>> checkedLines() const;

signals:
    void lineToggled(int hunkIndex, int lineIndex, bool checked);

private:
    struct Row
    {
        QString kind; // "hunk" | "context" | "added" | "removed"
        int     oldNo = -1;
        int     newNo = -1;
        QString text;
        bool    checkable = false;
        bool    checked   = false;
        int     hunkIndex = -1;
        int     lineIndex = -1; // index within the hunk's lines (changed+context)
    };
    std::vector<Row> m_rows;
};

} // namespace gittide::ui
```

- [ ] **Step 5: Write the implementation**

Create `ui/src/difflinesmodel.cpp`:

```cpp
#include "gittide/ui/difflinesmodel.hpp"

#include <algorithm>

#include <QString>

namespace gittide::ui {

namespace {
QString hunkHeader(const gittide::DiffHunk& h)
{
    return QStringLiteral("@@ -%1,%2 +%3,%4 @@")
        .arg(h.oldStart)
        .arg(h.oldLines)
        .arg(h.newStart)
        .arg(h.newLines);
}
}

int DiffLinesModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant DiffLinesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const Row& r = m_rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case KindRole:
        return r.kind;
    case OldNoRole:
        return r.oldNo;
    case NewNoRole:
        return r.newNo;
    case TextRole:
        return r.text;
    case CheckableRole:
        return r.checkable;
    case CheckedRole:
        return r.checked;
    case HunkRole:
        return r.hunkIndex;
    case LineRole:
        return r.lineIndex;
    default:
        return {};
    }
}

QHash<int, QByteArray> DiffLinesModel::roleNames() const
{
    return {
        {KindRole, "lineKind"},
        {OldNoRole, "oldNo"},
        {NewNoRole, "newNo"},
        {TextRole, "lineText"},
        {CheckableRole, "checkable"},
        {CheckedRole, "lineChecked"},
        {HunkRole, "hunkIndex"},
        {LineRole, "lineIndex"},
    };
}

void DiffLinesModel::setDiff(const gittide::DiffResult& result, const std::map<int, std::vector<int>>& checkedLines, bool wholeChecked)
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

        for (int l = 0; l < static_cast<int>(hunk.lines.size()); ++l)
        {
            const gittide::DiffLine& line = hunk.lines[static_cast<std::size_t>(l)];
            Row r;
            r.oldNo     = line.oldLineno;
            r.newNo     = line.newLineno;
            r.text      = QString::fromStdString(line.text);
            r.hunkIndex = h;
            r.lineIndex = l;
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
                    const auto it = checkedLines.find(h);
                    r.checked     = it != checkedLines.end() && std::find(it->second.begin(), it->second.end(), l) != it->second.end();
                }
            }
            m_rows.push_back(std::move(r));
        }
    }
    endResetModel();
}

void DiffLinesModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void DiffLinesModel::setLineChecked(int row, bool checked)
{
    if (row < 0 || row >= static_cast<int>(m_rows.size()))
        return;
    Row& r = m_rows[static_cast<std::size_t>(row)];
    if (!r.checkable || r.checked == checked)
        return;
    r.checked             = checked;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {CheckedRole});
    emit lineToggled(r.hunkIndex, r.lineIndex, checked);
}

void DiffLinesModel::setAllChecked(bool checked)
{
    bool any = false;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        Row& r = m_rows[static_cast<std::size_t>(i)];
        if (r.checkable && r.checked != checked)
        {
            r.checked             = checked;
            const QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {CheckedRole});
            any = true;
        }
    }
    Q_UNUSED(any);
}

int DiffLinesModel::checkableCount() const
{
    int n = 0;
    for (const auto& r : m_rows)
        if (r.checkable)
            ++n;
    return n;
}

int DiffLinesModel::checkedCount() const
{
    int n = 0;
    for (const auto& r : m_rows)
        if (r.checkable && r.checked)
            ++n;
    return n;
}

std::map<int, std::vector<int>> DiffLinesModel::checkedLines() const
{
    std::map<int, std::vector<int>> out;
    for (const auto& r : m_rows)
        if (r.checkable && r.checked)
            out[r.hunkIndex].push_back(r.lineIndex);
    return out;
}

} // namespace gittide::ui
```

- [ ] **Step 6: Add the sources to `gittide_ui`**

In `ui/CMakeLists.txt`, add inside the `add_library(gittide_ui STATIC …)` list:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/difflinesmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/difflinesmodel.hpp
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `TestDiffLinesModel` cases green.

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/difflinesmodel.hpp ui/src/difflinesmodel.cpp ui/CMakeLists.txt \
        tests/ui/test_diff_lines_model.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): DiffLinesModel — flattened colored diff as a QML list model"
```

---

### Task 3: `RepoViewModel` — QML façade over `RepoController`

**Files:**
- Create: `ui/include/gittide/ui/repoviewmodel.hpp`, `ui/src/repoviewmodel.cpp`
- Test: `tests/ui/test_repo_view_model.cpp`
- Modify: `ui/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/ui/main.cpp`

**Interfaces:**
- Consumes: `RepoController` (signals `statusChanged`, `diffReady`, `headChanged`, `branchesChanged`, `committed`, `operationFailed`; slots `open`, and `QCoro::Task` methods `refreshStatus`, `refreshBranches`, `refreshDiff(QString, gittide::DiffTarget)`, `commitSelection(gittide::CommitRequest, std::vector<gittide::StageSelection>)`); `ChangedFilesModel` (Task 1); `DiffLinesModel` (Task 2); `gittide::FileStatus`, `gittide::DiffResult`, `gittide::HeadState`, `gittide::BranchInfo`, `gittide::StageSelection`, `gittide::CommitRequest`, `gittide::DiffTarget` (`core/`); `pathToQString`/`qstringToPath` (`gittide/ui/metatypes.hpp`); `QCoro::connect` (`<qcorotask.h>`).
- Produces: `class RepoViewModel : public QObject` with constructor `explicit RepoViewModel(QObject* parent = nullptr)`; `Q_PROPERTY`s `repoOpen` (bool, NOTIFY `changed`), `currentBranch` (QString, NOTIFY `branchChanged`), `activeFile` (QString, NOTIFY `activeFileChanged`), `checkedCount` (int, NOTIFY `checkedChanged`), `changedFiles` (`gittide::ui::ChangedFilesModel*`, CONSTANT), `diffLines` (`gittide::ui::DiffLinesModel*`, CONSTANT); `Q_INVOKABLE` `void open(const QString& path)`, `void selectFile(const QString& path)`, `void setFileChecked(int row, bool checked)`, `void setAllFilesChecked(bool checked)`, `void setLineChecked(int row, bool checked)`, `void setAllLinesChecked(bool checked)`, `void commit(const QString& summary, const QString& description)`; signals `changed()`, `branchChanged()`, `activeFileChanged()`, `checkedChanged()`, `committedOk()`, `operationFailed(const QString&)`; accessors `ChangedFilesModel* changedFiles() const`, `DiffLinesModel* diffLines() const`, `QString currentBranch() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/ui/test_repo_view_model.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModel>
#include <QRandomGenerator>

#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/ui/repoviewmodel.hpp"

using gittide::ui::RepoViewModel;

namespace repo_view_model_test {

// Self-contained dirty repo: one committed file "a.txt", then a worktree edit
// (mirrors make_dirty_repo() in test_repo_controller.cpp).
inline std::filesystem::path make_dirty_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-rvm-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    {
        std::ofstream(dir / "a.txt") << "one\n";
    }
    git_index* idx = nullptr;
    git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt");
    git_index_write(idx);
    git_oid tree_oid;
    git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr;
    git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr;
    git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid;
    git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, "init", tree, 0);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(idx);
    git_repository_free(raw);
    {
        std::ofstream(dir / "a.txt") << "one\ntwo\n";
    }
    git_libgit2_shutdown();
    return dir;
}

} // namespace repo_view_model_test

class TestRepoViewModel : public QObject
{
    Q_OBJECT
private slots:
    void open_populates_changed_files_and_branch()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);

        vm.open(QString::fromStdString(dir.generic_string()));

        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);
        QVERIFY(branchSpy.wait(3000));
        QVERIFY(!vm.currentBranch().isEmpty());

        std::filesystem::remove_all(dir);
    }

    void select_file_populates_diff()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        QSignalSpy diffSpy(vm.diffLines(), &QAbstractItemModel::modelReset);
        vm.selectFile(QStringLiteral("a.txt"));
        QVERIFY(diffSpy.wait(3000));
        QVERIFY(vm.diffLines()->rowCount(QModelIndex()) > 0);
        QCOMPARE(vm.activeFile(), QStringLiteral("a.txt"));

        std::filesystem::remove_all(dir);
    }

    void commit_succeeds_and_clears_changes()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();

        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 1);

        QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
        vm.commit(QStringLiteral("test commit"), QString());
        QVERIFY(committedSpy.wait(3000));

        // commitSelection refreshes status after committing → worktree clean.
        QCOMPARE(vm.changedFiles()->rowCount(QModelIndex()), 0);

        std::filesystem::remove_all(dir);
    }
};

#include "test_repo_view_model.moc"
```

- [ ] **Step 2: Wire the test into the runner (both edits)**

In `tests/CMakeLists.txt`, add to `gittide_ui_test_sources`:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repo_view_model.cpp
```

In `tests/ui/main.cpp`, add the include:

```cpp
#include "test_repo_view_model.cpp"
```

and the run line:

```cpp
    RUN(TestRepoViewModel);
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel`
Expected: FAIL to compile — `gittide/ui/repoviewmodel.hpp` not found.

- [ ] **Step 4: Write the header**

Create `ui/include/gittide/ui/repoviewmodel.hpp`:

```cpp
#pragma once
#include <map>
#include <vector>

#include <QObject>
#include <QString>

#include "gittide/branchinfo.hpp"
#include "gittide/diff.hpp"
#include "gittide/filestatus.hpp"
#include "gittide/ui/changedfilesmodel.hpp"
#include "gittide/ui/difflinesmodel.hpp"

namespace gittide::ui {

class RepoController;

/// QML-facing façade over the signal-driven RepoController. Owns the controller,
/// the two list models QML binds to, and the staging-selection state (whole-file
/// tri-state per path + per-hunk checked line indices). Kicks the controller's
/// QCoro::Task methods on behalf of QML (which cannot call them) and translates
/// its std::-typed signals into model updates and property changes. Staging/commit
/// semantics replicate ui/src/changesview.cpp.
class RepoViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool repoOpen READ repoOpen NOTIFY changed)
    Q_PROPERTY(QString currentBranch READ currentBranch NOTIFY branchChanged)
    Q_PROPERTY(QString activeFile READ activeFile NOTIFY activeFileChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY checkedChanged)
    Q_PROPERTY(gittide::ui::ChangedFilesModel* changedFiles READ changedFiles CONSTANT)
    Q_PROPERTY(gittide::ui::DiffLinesModel* diffLines READ diffLines CONSTANT)

public:
    explicit RepoViewModel(QObject* parent = nullptr);

    bool repoOpen() const;
    QString currentBranch() const;
    QString activeFile() const;
    int checkedCount() const;
    ChangedFilesModel* changedFiles() const;
    DiffLinesModel* diffLines() const;

    Q_INVOKABLE void open(const QString& path);
    Q_INVOKABLE void selectFile(const QString& path);
    Q_INVOKABLE void setFileChecked(int row, bool checked);
    Q_INVOKABLE void setAllFilesChecked(bool checked);
    Q_INVOKABLE void setLineChecked(int row, bool checked);
    Q_INVOKABLE void setAllLinesChecked(bool checked);
    Q_INVOKABLE void commit(const QString& summary, const QString& description);

signals:
    void changed();
    void branchChanged();
    void activeFileChanged();
    void checkedChanged();
    void committedOk();
    void operationFailed(const QString& message);

private:
    struct FileSel
    {
        ChangedFilesModel::Check        state = ChangedFilesModel::Checked;
        std::map<int, std::vector<int>> checkedLinesByHunk; // only meaningful when state == Partial
    };

    void onStatus(const std::vector<gittide::FileStatus>& files);
    void onDiff(const QString& path, const gittide::DiffResult& result);
    void onHead(const gittide::HeadState& head);
    void onBranches(const std::vector<gittide::BranchInfo>& branches);
    void onLineToggled(int hunkIndex, int lineIndex, bool checked);
    void recomputeActiveFileState();

    RepoController*    m_controller = nullptr;
    ChangedFilesModel* m_files      = nullptr;
    DiffLinesModel*    m_diff       = nullptr;

    bool                       m_open = false;
    QString                    m_branch;
    QString                    m_activeFile;
    gittide::DiffResult        m_activeDiff;
    std::map<QString, FileSel> m_sel;
};

} // namespace gittide::ui
```

- [ ] **Step 5: Write the implementation**

Create `ui/src/repoviewmodel.cpp`:

```cpp
#include "gittide/ui/repoviewmodel.hpp"

#include <algorithm>
#include <utility>

#include <qcorotask.h>

#include "gittide/ui/metatypes.hpp"
#include "gittide/ui/repocontroller.hpp"

namespace gittide::ui {

RepoViewModel::RepoViewModel(QObject* parent)
    : QObject(parent)
    , m_controller(new RepoController(this))
    , m_files(new ChangedFilesModel(this))
    , m_diff(new DiffLinesModel(this))
{
    connect(m_controller, &RepoController::statusChanged, this, &RepoViewModel::onStatus);
    connect(m_controller, &RepoController::diffReady, this, &RepoViewModel::onDiff);
    connect(m_controller, &RepoController::headChanged, this, &RepoViewModel::onHead);
    connect(m_controller, &RepoController::branchesChanged, this, &RepoViewModel::onBranches);
    connect(m_controller, &RepoController::operationFailed, this, &RepoViewModel::operationFailed);
    connect(m_diff, &DiffLinesModel::lineToggled, this, &RepoViewModel::onLineToggled);
}

bool RepoViewModel::repoOpen() const
{
    return m_open;
}

QString RepoViewModel::currentBranch() const
{
    return m_branch;
}

QString RepoViewModel::activeFile() const
{
    return m_activeFile;
}

int RepoViewModel::checkedCount() const
{
    return m_files->checkedCount();
}

ChangedFilesModel* RepoViewModel::changedFiles() const
{
    return m_files;
}

DiffLinesModel* RepoViewModel::diffLines() const
{
    return m_diff;
}

void RepoViewModel::open(const QString& path)
{
    m_controller->open(path);
    m_open = true;
    emit changed();
    QCoro::connect(m_controller->refreshStatus(), this, [] {});
    QCoro::connect(m_controller->refreshBranches(), this, [] {});
}

void RepoViewModel::selectFile(const QString& path)
{
    m_activeFile = path;
    emit activeFileChanged();
    QCoro::connect(m_controller->refreshDiff(path, gittide::DiffTarget::WorktreeVsHead), this, [] {});
}

void RepoViewModel::setFileChecked(int row, bool checked)
{
    const QString path = m_files->pathAt(row);
    if (path.isEmpty())
        return;
    FileSel& fs = m_sel[path];
    fs.state    = checked ? ChangedFilesModel::Checked : ChangedFilesModel::Unchecked;
    fs.checkedLinesByHunk.clear();
    m_files->setCheckState(row, fs.state);
    if (path == m_activeFile)
        m_diff->setAllChecked(checked);
    emit checkedChanged();
}

void RepoViewModel::setAllFilesChecked(bool checked)
{
    for (int row = 0; row < m_files->rowCount(QModelIndex()); ++row)
        setFileChecked(row, checked);
}

void RepoViewModel::setLineChecked(int row, bool checked)
{
    // Routes through DiffLinesModel, which emits lineToggled() → onLineToggled().
    m_diff->setLineChecked(row, checked);
}

void RepoViewModel::setAllLinesChecked(bool checked)
{
    if (m_activeFile.isEmpty())
        return;
    m_diff->setAllChecked(checked);
    FileSel& fs = m_sel[m_activeFile];
    fs.checkedLinesByHunk = m_diff->checkedLines();
    recomputeActiveFileState();
}

void RepoViewModel::commit(const QString& summary, const QString& description)
{
    std::vector<gittide::StageSelection> selections;
    for (int row = 0; row < m_files->rowCount(QModelIndex()); ++row)
    {
        const QString path                     = m_files->pathAt(row);
        const ChangedFilesModel::Check rowState = m_files->checkState(row);
        if (rowState == ChangedFilesModel::Unchecked)
            continue;
        const std::filesystem::path fsPath = qstringToPath(path);
        if (rowState == ChangedFilesModel::Partial)
        {
            const auto it = m_sel.find(path);
            if (it != m_sel.end())
                for (const auto& [hunk, lines] : it->second.checkedLinesByHunk)
                    selections.push_back(gittide::StageSelection{.path = fsPath, .hunkIndex = hunk, .lineIndices = lines});
        }
        else // Checked: whole file
        {
            selections.push_back(gittide::StageSelection{.path = fsPath, .hunkIndex = std::nullopt, .lineIndices = {}});
        }
    }

    std::string message = summary.toStdString();
    if (!description.isEmpty())
        message += "\n\n" + description.toStdString();

    QCoro::connect(m_controller->commitSelection(gittide::CommitRequest{.message = message}, std::move(selections)), this,
        [this]() { emit committedOk(); });
}

void RepoViewModel::onStatus(const std::vector<gittide::FileStatus>& files)
{
    m_files->setFiles(files);
    m_sel.clear();
    for (const auto& f : files)
        m_sel[pathToQString(f.path)] = FileSel{ChangedFilesModel::Checked, {}};
    m_activeFile.clear();
    m_activeDiff = {};
    m_diff->clear();
    emit activeFileChanged();
    emit checkedChanged();
}

void RepoViewModel::onDiff(const QString& path, const gittide::DiffResult& result)
{
    if (path != m_activeFile)
        return;
    m_activeDiff      = result;
    const FileSel& fs = m_sel[path];
    m_diff->setDiff(result, fs.checkedLinesByHunk, fs.state == ChangedFilesModel::Checked);
}

void RepoViewModel::onHead(const gittide::HeadState& head)
{
    QString label;
    if (head.unborn)
        label = QStringLiteral("(no commits)");
    else if (head.detached)
        label = QStringLiteral("detached @ ") + QString::fromStdString(head.oid).left(7);
    else if (!head.branch.empty())
        label = QString::fromStdString(head.branch);

    if (label.isEmpty() || label == m_branch)
        return;
    m_branch = label;
    emit branchChanged();
}

void RepoViewModel::onBranches(const std::vector<gittide::BranchInfo>& branches)
{
    for (const auto& b : branches)
    {
        if (b.isHead)
        {
            const QString name = QString::fromStdString(b.name);
            if (!name.isEmpty() && name != m_branch)
            {
                m_branch = name;
                emit branchChanged();
            }
            return;
        }
    }
}

void RepoViewModel::onLineToggled(int hunkIndex, int lineIndex, bool checked)
{
    if (m_activeFile.isEmpty())
        return;
    FileSel& fs = m_sel[m_activeFile];
    auto& lines = fs.checkedLinesByHunk[hunkIndex];
    if (checked)
    {
        if (std::find(lines.begin(), lines.end(), lineIndex) == lines.end())
            lines.push_back(lineIndex);
    }
    else
    {
        lines.erase(std::remove(lines.begin(), lines.end(), lineIndex), lines.end());
        if (lines.empty())
            fs.checkedLinesByHunk.erase(hunkIndex);
    }
    recomputeActiveFileState();
}

void RepoViewModel::recomputeActiveFileState()
{
    if (m_activeFile.isEmpty())
        return;
    const int total   = m_diff->checkableCount();
    const int checked = m_diff->checkedCount();
    ChangedFilesModel::Check state;
    if (checked == 0)
        state = ChangedFilesModel::Unchecked;
    else if (total > 0 && checked == total)
        state = ChangedFilesModel::Checked;
    else
        state = ChangedFilesModel::Partial;

    FileSel& fs = m_sel[m_activeFile];
    fs.state    = state;
    if (state != ChangedFilesModel::Partial)
        fs.checkedLinesByHunk.clear();

    const int row = m_files->rowForPath(m_activeFile);
    if (row >= 0)
        m_files->setCheckState(row, state);
    emit checkedChanged();
}

} // namespace gittide::ui
```

- [ ] **Step 6: Add the sources to `gittide_ui`**

In `ui/CMakeLists.txt`, add inside the `add_library(gittide_ui STATIC …)` list:

```cmake
  ${CMAKE_CURRENT_SOURCE_DIR}/src/repoviewmodel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gittide/ui/repoviewmodel.hpp
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `TestRepoViewModel` three cases green (open populates files + branch; selectFile populates diff; commit succeeds and clears).

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp ui/CMakeLists.txt \
        tests/ui/test_repo_view_model.cpp tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): RepoViewModel — QML facade over RepoController"
```

---

### Task 4: Wire `repoVm` into the QML context + branch bar + open-on-select

**Files:**
- Modify: `ui/include/gittide/ui/qmlcontext.hpp`, `ui/src/qmlcontext.cpp`
- Create: `ui/qml/WorkingPane.qml`, `ui/qml/BranchBar.qml`
- Modify: `ui/qml/Main.qml`, `ui/qml/Sidebar.qml`, `ui/qml/qml.qrc`, `app/qml_main.cpp`, `tests/ui/test_qml_shell.cpp`
- Test: `tests/ui/test_qml_shell.cpp` (add a case)

**Interfaces:**
- Consumes: `RepoViewModel` (Task 3) context property `repoVm`; `QmlTheme`, `RepoListModel`, `ProjectController` (existing context props).
- Produces: updated free function `void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController, RepoViewModel* repoVm)` (adds the trailing `repoVm` parameter, sets context property `repoVm`). `WorkingPane.qml` (`objectName: "workingPane"`) hosting `BranchBar` + a `TabBar` (Changes/History) placeholder + a `StackLayout`. `BranchBar.qml` (`objectName: "branchBar"`) exposing a `text` alias bound to `repoVm.currentBranch`.

- [ ] **Step 1: Update `installQmlContext` signature (failing build first)**

Edit `ui/include/gittide/ui/qmlcontext.hpp` — add the forward declaration and parameter:

```cpp
#pragma once

class QQmlContext;

namespace gittide::ui {

class QmlTheme;
class RepoListModel;
class ProjectController;
class RepoViewModel;

// Single source of the QML context wiring used by both the app entry point and
// the shell test. Sets the context properties Main.qml binds to: `theme`,
// `repoModel`, `projectController`, `repoVm`. projectController/repoVm may be null
// in tests that don't exercise them.
void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController, RepoViewModel* repoVm);

} // namespace gittide::ui
```

Edit `ui/src/qmlcontext.cpp`:

```cpp
#include "gittide/ui/qmlcontext.hpp"

#include <QQmlContext>

#include "gittide/ui/projectcontroller.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/repoviewmodel.hpp"

namespace gittide::ui {

void installQmlContext(QQmlContext* ctx, QmlTheme* theme, RepoListModel* repoModel, ProjectController* projectController, RepoViewModel* repoVm)
{
    ctx->setContextProperty(QStringLiteral("theme"), theme);
    ctx->setContextProperty(QStringLiteral("repoModel"), repoModel);
    ctx->setContextProperty(QStringLiteral("projectController"), projectController);
    ctx->setContextProperty(QStringLiteral("repoVm"), repoVm);
}

} // namespace gittide::ui
```

- [ ] **Step 2: Update the two existing call sites + add the failing test case**

In `app/qml_main.cpp`, add the include and construct a `RepoViewModel`, then pass it. Add near the other `gittide/ui/...` includes:

```cpp
#include "gittide/ui/repoviewmodel.hpp"
```

Immediately before the `QQmlApplicationEngine engine;` line, add:

```cpp
    RepoViewModel repoVm;
```

and change the `installQmlContext(...)` call to:

```cpp
    installQmlContext(engine.rootContext(), &qmlTheme, controller.repos(), &controller, &repoVm);
```

In `tests/ui/test_qml_shell.cpp`, update the **two existing** `installQmlContext(...)` calls (in `main_qml_loads_without_errors` and `repo_tree_is_bound_to_the_model`) to pass a trailing `nullptr`:

```cpp
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, nullptr);
```

Then add this new slot and the include it needs. Add at the top of the file:

```cpp
#include "gittide/ui/repoviewmodel.hpp"
#include <git2.h>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>
```

Add a file-local dirty-repo helper in the same anonymous namespace style used by the other tests (place it above the test class):

```cpp
namespace qml_shell_test {
inline std::filesystem::path make_dirty_repo()
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-qsh-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    {
        std::ofstream(dir / "a.txt") << "one\n";
    }
    git_index* idx = nullptr;
    git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt");
    git_index_write(idx);
    git_oid tree_oid;
    git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr;
    git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr;
    git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid;
    git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, "init", tree, 0);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(idx);
    git_repository_free(raw);
    {
        std::ofstream(dir / "a.txt") << "one\ntwo\n";
    }
    git_libgit2_shutdown();
    return dir;
}
}
```

Add the test slot to `TestQmlShell`:

```cpp
    void branch_bar_binds_to_view_model()
    {
        const auto dir = qml_shell_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(branchSpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* bar = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("branchBar"));
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->property("text").toString(), vm.currentBranch());

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel`
Expected: FAIL — `Main.qml` has no child named `branchBar` (and/or compile error until `installQmlContext` arity is consistent — fix all call sites in this task).

- [ ] **Step 4: Create `BranchBar.qml`**

Create `ui/qml/BranchBar.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: branchBar
    objectName: "branchBar"
    property alias text: branchLabel.text

    implicitHeight: 56
    color: theme.surfaceRaised

    Rectangle { // bottom hairline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12

        Rectangle { // accent-tinted current-branch chip
            Layout.preferredHeight: 36
            Layout.preferredWidth: branchCol.implicitWidth + 28
            radius: 6
            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.14)
            border.color: theme.accent
            border.width: 1

            ColumnLayout {
                id: branchCol
                anchors.centerIn: parent
                spacing: 0
                Label {
                    id: branchLabel
                    text: repoVm ? repoVm.currentBranch : ""
                    color: theme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Label {
                    text: "Current branch"
                    color: theme.textMuted
                    font.pixelSize: 11
                }
            }
        }

        Item { Layout.fillWidth: true }
    }
}
```

- [ ] **Step 5: Create `WorkingPane.qml`**

Create `ui/qml/WorkingPane.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: workingPane
    objectName: "workingPane"
    spacing: 0

    BranchBar {
        Layout.fillWidth: true
    }

    TabBar {
        id: tabs
        objectName: "changesTabBar"
        Layout.fillWidth: true
        background: Rectangle { color: theme.surfaceRaised }
        TabButton {
            text: "Changes"
            contentItem: Label {
                text: parent.text
                color: parent.checked ? theme.textPrimary : theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: "transparent" }
        }
        TabButton {
            text: "History"
            contentItem: Label {
                text: parent.text
                color: parent.checked ? theme.textPrimary : theme.textMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: "transparent" }
        }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: tabs.currentIndex

        // Index 0: Changes — filled in by Task 5/6 (ChangesPane).
        Item { objectName: "changesTabBody" }

        // Index 1: History — placeholder (later plan).
        Item {
            objectName: "historyTabBody"
            Label {
                anchors.centerIn: parent
                text: "History — coming soon"
                color: theme.textMuted
                font.pixelSize: 13
            }
        }
    }
}
```

- [ ] **Step 6: Swap the placeholder pane in `Main.qml`**

In `ui/qml/Main.qml`, replace the placeholder `Rectangle { … "Select a repository" … }` block with:

```qml
        WorkingPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
```

- [ ] **Step 7: Wire open-on-select in `Sidebar.qml`**

In `ui/qml/Sidebar.qml`, give the `TreeViewDelegate` an activation handler that opens the repo. Add inside the `delegate: TreeViewDelegate { … }` body (e.g. right after `indentation: 16`):

```qml
                onClicked: if (repoVm) repoVm.open(model.repoPath)
```

(If the `TreeViewDelegate` already defines `onClicked`, merge the call into the existing handler instead of adding a second one.)

- [ ] **Step 8: Register the new QML files**

In `ui/qml/qml.qrc`, add the two files under the `/qml` prefix:

```xml
    <file>WorkingPane.qml</file>
    <file>BranchBar.qml</file>
```

- [ ] **Step 9: Run to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `branch_bar_binds_to_view_model` green (branch label equals `vm.currentBranch()`); existing shell cases stay green.

- [ ] **Step 10: Commit**

```bash
git add ui/include/gittide/ui/qmlcontext.hpp ui/src/qmlcontext.cpp \
        ui/qml/WorkingPane.qml ui/qml/BranchBar.qml ui/qml/Main.qml ui/qml/Sidebar.qml ui/qml/qml.qrc \
        app/qml_main.cpp tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): wire RepoViewModel into QML context, branch bar, open-on-select"
```

---

### Task 5: Changes file-list column + commit box

**Files:**
- Create: `ui/qml/ChangesPane.qml`
- Modify: `ui/qml/WorkingPane.qml`, `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (add a case)

**Interfaces:**
- Consumes: `repoVm.changedFiles` (`ChangedFilesModel`), `repoVm.checkedCount`, `repoVm.currentBranch`, `repoVm.setFileChecked(row, checked)`, `repoVm.setAllFilesChecked(checked)`, `repoVm.selectFile(path)`, `repoVm.commit(summary, description)`; `theme` tokens.
- Produces: `ChangesPane.qml` with a left column: header tri-state master checkbox (`objectName: "fileHeaderCheck"`), file `ListView` (`objectName: "fileList"`, `model: repoVm.changedFiles`), and a commit box (`objectName: "commitSummary"`, `objectName: "commitDescription"`, `objectName: "commitButton"`). The diff column is added in Task 6; for this task the right column is an empty placeholder `Item { objectName: "diffColumn" }`.

- [ ] **Step 1: Write the failing test (add a case to `TestQmlShell`)**

Add this slot to `TestQmlShell` in `tests/ui/test_qml_shell.cpp`:

```cpp
    void file_list_binds_to_changed_files_model()
    {
        const auto dir = qml_shell_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* list = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("fileList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->property("model").value<QAbstractItemModel*>(), vm.changedFiles());

        QObject* btn = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("commitButton"));
        QVERIFY(btn != nullptr);

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — no child named `fileList`.

- [ ] **Step 3: Create `ChangesPane.qml`**

Create `ui/qml/ChangesPane.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: changesPane
    objectName: "changesPane"
    spacing: 0

    // ---- Files + commit column (fixed width) ----
    ColumnLayout {
        Layout.preferredWidth: 320
        Layout.fillHeight: true
        spacing: 0

        // Header with tri-state master checkbox + count
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 10

            CheckBox {
                objectName: "fileHeaderCheck"
                tristate: true
                // 0 Unchecked, 1 PartiallyChecked, 2 Checked — mirror file states.
                checkState: {
                    var n = repoVm ? repoVm.checkedCount : 0
                    var total = repoVm && repoVm.changedFiles ? repoVm.changedFiles.rowCount() : 0
                    if (n === 0) return Qt.Unchecked
                    if (n === total) return Qt.Checked
                    return Qt.PartiallyChecked
                }
                onClicked: if (repoVm) repoVm.setAllFilesChecked(checkState !== Qt.Checked)
            }
            Label {
                text: "Changed files"
                color: theme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: repoVm && repoVm.changedFiles
                      ? (repoVm.checkedCount + " / " + repoVm.changedFiles.rowCount())
                      : ""
                color: theme.textMuted
                font.pixelSize: 11
            }
        }

        ListView {
            id: fileList
            objectName: "fileList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: repoVm ? repoVm.changedFiles : null

            delegate: Rectangle {
                width: ListView.view.width
                height: 30
                color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8

                    CheckBox {
                        checkState: model.checkState === 2 ? Qt.Checked
                                    : model.checkState === 1 ? Qt.PartiallyChecked
                                    : Qt.Unchecked
                        onClicked: if (repoVm) repoVm.setFileChecked(index, checkState !== Qt.Checked)
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        font.family: "monospace"
                        font.pixelSize: 12
                        textFormat: Text.RichText
                        text: "<font color='" + theme.textMuted + "'>" + model.fileDir + "</font>"
                              + "<font color='" + theme.textPrimary + "'>" + model.fileName + "</font>"
                    }
                    Label {
                        text: model.statusLetter
                        font.family: "monospace"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: model.statusKind === "added" ? theme.stateAdded
                               : model.statusKind === "deleted" ? theme.stateDeleted
                               : model.statusKind === "untracked" ? theme.stateUntracked
                               : theme.stateModified
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: {
                        fileList.currentIndex = index
                        if (repoVm) repoVm.selectFile(model.filePath)
                    }
                }
            }
        }

        // ---- Commit box ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8

            TextField {
                id: commitSummary
                objectName: "commitSummary"
                Layout.fillWidth: true
                placeholderText: "Summary (required)"
                color: theme.textPrimary
                background: Rectangle {
                    radius: 6
                    color: theme.surfaceBase
                    border.color: theme.border
                    border.width: 1
                }
            }
            TextArea {
                id: commitDescription
                objectName: "commitDescription"
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                placeholderText: "Description"
                color: theme.textPrimary
                background: Rectangle {
                    radius: 6
                    color: theme.surfaceBase
                    border.color: theme.border
                    border.width: 1
                }
            }
            Button {
                id: commitButton
                objectName: "commitButton"
                Layout.fillWidth: true
                enabled: repoVm && repoVm.checkedCount > 0 && commitSummary.text.length > 0
                contentItem: Label {
                    text: repoVm
                          ? ("Commit " + repoVm.checkedCount + " file" + (repoVm.checkedCount === 1 ? "" : "s")
                             + " to " + repoVm.currentBranch)
                          : "Commit"
                    color: parent.enabled ? theme.surfaceBase : theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                }
                background: Rectangle {
                    radius: 6
                    color: parent.enabled ? theme.accent : theme.surfaceOverlay
                }
                onClicked: {
                    if (repoVm) repoVm.commit(commitSummary.text, commitDescription.text)
                }
            }
        }
    }

    // Hairline divider
    Rectangle {
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        color: theme.border
    }

    // ---- Diff column placeholder (filled in Task 6) ----
    Item {
        objectName: "diffColumn"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    // Clear the commit fields once a commit succeeds.
    Connections {
        target: repoVm
        function onCommittedOk() {
            commitSummary.text = ""
            commitDescription.text = ""
        }
    }
}
```

- [ ] **Step 4: Mount `ChangesPane` under the Changes tab**

In `ui/qml/WorkingPane.qml`, replace the `Item { objectName: "changesTabBody" }` placeholder (StackLayout index 0) with:

```qml
        ChangesPane {
            objectName: "changesTabBody"
        }
```

- [ ] **Step 5: Register `ChangesPane.qml`**

In `ui/qml/qml.qrc`, add under the `/qml` prefix:

```xml
    <file>ChangesPane.qml</file>
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `file_list_binds_to_changed_files_model` green (`fileList.model == vm.changedFiles()`, `commitButton` present); existing cases stay green.

- [ ] **Step 7: Commit**

```bash
git add ui/qml/ChangesPane.qml ui/qml/WorkingPane.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): QML Changes file list + commit box bound to RepoViewModel"
```

---

### Task 6: Colored diff column with per-line checkboxes

**Files:**
- Create: `ui/qml/DiffView.qml`
- Modify: `ui/qml/ChangesPane.qml`, `ui/qml/qml.qrc`
- Test: `tests/ui/test_qml_shell.cpp` (add a case)

**Interfaces:**
- Consumes: `repoVm.diffLines` (`DiffLinesModel`), `repoVm.activeFile`, `repoVm.setLineChecked(row, checked)`, `repoVm.setAllLinesChecked(checked)`; `theme` tokens (`stateAdded`/`stateModified`/`stateDeleted`, surfaces, text).
- Produces: `DiffView.qml` with a header (master checkbox `objectName: "diffHeaderCheck"`, active-file path) and a diff `ListView` (`objectName: "diffList"`, `model: repoVm.diffLines`) whose delegate colors added/removed lines and shows a per-line checkbox only when `model.checkable`.

- [ ] **Step 1: Write the failing test (add a case to `TestQmlShell`)**

Add this slot to `TestQmlShell`:

```cpp
    void diff_list_binds_to_diff_lines_model()
    {
        const auto dir = qml_shell_test::make_dirty_repo();

        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RepoViewModel vm;

        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QSignalSpy diffSpy(vm.diffLines(), &QAbstractItemModel::modelReset);
        vm.selectFile(QStringLiteral("a.txt"));
        QVERIFY(diffSpy.wait(3000));

        QQmlApplicationEngine engine;
        installQmlContext(engine.rootContext(), &theme, &repoModel, nullptr, &vm);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);

        QObject* diff = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("diffList"));
        QVERIFY(diff != nullptr);
        QCOMPARE(diff->property("model").value<QAbstractItemModel*>(), vm.diffLines());

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: FAIL — no child named `diffList`.

- [ ] **Step 3: Create `DiffView.qml`**

Create `ui/qml/DiffView.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: diffView
    objectName: "diffView"
    spacing: 0

    // ---- Header: master line checkbox + active file path ----
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        spacing: 10

        CheckBox {
            objectName: "diffHeaderCheck"
            tristate: true
            checkState: {
                if (!repoVm || !repoVm.diffLines) return Qt.Unchecked
                // Derived purely for display; toggling stages/unstages all lines.
                return Qt.PartiallyChecked
            }
            visible: repoVm && repoVm.activeFile.length > 0
            onClicked: if (repoVm) repoVm.setAllLinesChecked(checkState !== Qt.Checked)
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            font.family: "monospace"
            font.pixelSize: 12
            color: theme.textSecondary
            text: repoVm ? repoVm.activeFile : ""
        }
    }

    ListView {
        id: diffList
        objectName: "diffList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: repoVm ? repoVm.diffLines : null

        delegate: Rectangle {
            width: ListView.view.width
            height: 20
            color: model.lineKind === "added" ? Qt.rgba(theme.stateAdded.r, theme.stateAdded.g, theme.stateAdded.b, 0.12)
                   : model.lineKind === "removed" ? Qt.rgba(theme.stateDeleted.r, theme.stateDeleted.g, theme.stateDeleted.b, 0.12)
                   : model.lineKind === "hunk" ? theme.surfaceOverlay
                   : "transparent"

            RowLayout {
                anchors.fill: parent
                spacing: 6

                // Per-line checkbox column (only for changed lines)
                Item {
                    Layout.preferredWidth: 22
                    Layout.fillHeight: true
                    CheckBox {
                        anchors.centerIn: parent
                        visible: model.checkable
                        checked: model.lineChecked
                        onClicked: if (repoVm) repoVm.setLineChecked(index, !model.lineChecked)
                    }
                }

                // Old/new line-number gutter
                Label {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignRight
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: theme.textMuted
                    text: model.lineKind === "hunk" ? ""
                          : (model.oldNo > 0 ? model.oldNo : "") + " " + (model.newNo > 0 ? model.newNo : "")
                }

                // Sign
                Label {
                    Layout.preferredWidth: 10
                    font.family: "monospace"
                    font.pixelSize: 12
                    text: model.lineKind === "added" ? "+" : model.lineKind === "removed" ? "−" : ""
                    color: model.lineKind === "added" ? theme.stateAdded
                           : model.lineKind === "removed" ? theme.stateDeleted
                           : theme.textMuted
                }

                // Code / hunk header text
                Label {
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    text: model.lineText
                    color: model.lineKind === "hunk" ? theme.textMuted
                           : model.lineKind === "added" ? theme.stateAdded
                           : model.lineKind === "removed" ? theme.stateDeleted
                           : theme.textPrimary
                }
            }
        }
    }
}
```

- [ ] **Step 4: Mount `DiffView` in the diff column**

In `ui/qml/ChangesPane.qml`, replace the diff-column placeholder

```qml
    Item {
        objectName: "diffColumn"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
```

with:

```qml
    DiffView {
        objectName: "diffColumn"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
```

- [ ] **Step 5: Register `DiffView.qml`**

In `ui/qml/qml.qrc`, add under the `/qml` prefix:

```xml
    <file>DiffView.qml</file>
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build build --target gittide_ui_tests --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — `diff_list_binds_to_diff_lines_model` green (`diffList.model == vm.diffLines()`); existing cases stay green.

- [ ] **Step 7: Commit**

```bash
git add ui/qml/DiffView.qml ui/qml/ChangesPane.qml ui/qml/qml.qrc tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): colored QML diff view with per-line staging checkboxes"
```

---

### Task 7: End-to-end run + manual verification

**Files:**
- Modify: none (verification task). If the app fails to render, fix the offending QML/wiring file touched in Tasks 4–6.

**Interfaces:**
- Consumes: the full stack assembled in Tasks 1–6.
- Produces: a confirmed-runnable `gittide_qml_app` showing the Changes pane over a real repo.

- [ ] **Step 1: Full build + full UI test suite**

Run: `cmake -S . -B build && cmake --build build --parallel && ctest --test-dir build --output-on-failure`
Expected: PASS — all suites green, including the new `TestChangedFilesModel`, `TestDiffLinesModel`, `TestRepoViewModel`, and the extended `TestQmlShell`.

- [ ] **Step 2: Launch the app and exercise the Changes flow**

Run: `./build/app/gittide_qml_app`
Expected, with a `projects.json` containing at least one repo with changes:
- Dark window, branded sidebar, branch bar showing the current branch in an accent chip.
- Clicking a repo in the tree opens it; the Changes tab lists changed files with colored status letters and tri-state checkboxes; the header master checkbox reflects/toggles all files.
- Selecting a file shows a colored diff (green additions, red deletions, dimmed context, hunk headers) with per-line checkboxes on changed lines.
- The commit button reads "Commit N file(s) to <branch>", is disabled until a summary is typed and at least one file is checked, and on click commits and clears the fields; the file list refreshes (committed changes disappear).
- The History tab shows the "History — coming soon" placeholder.

(Headless/CI note: prefer the `TestQmlShell` coverage from Tasks 4–6 over launching the app; the GUI launch is a human smoke check.)

- [ ] **Step 3: Commit (only if a fix was needed)**

If Step 2 required a QML/wiring fix, commit it:

```bash
git add -A
git commit -m "fix(ui): <describe the Changes-pane rendering fix>"
```

If no fix was needed, this task produces no commit — record the manual verification result in the progress ledger instead.

---

## Self-Review

**Spec coverage (this plan's slice — the Changes pane):**
- QML-facing data layer over the signal-driven controller → Tasks 1–3 (`ChangedFilesModel`, `DiffLinesModel`, `RepoViewModel`). ✔
- Branch bar (display-only current branch) → Task 4. ✔
- Changes/History tab bar; History placeholder → Task 4 + Task 7. ✔
- File list with tri-state master + per-file checkboxes, status letter/colour from theme tokens → Task 5. ✔
- Colored diff with hunk headers, green/red lines, per-line checkboxes + diff master checkbox → Task 6. ✔
- Commit box (summary + description + dynamic button label) wired through `RepoViewModel.commit` replicating `ChangesView` selection semantics → Tasks 3, 5. ✔
- Open-on-select from the sidebar tree → Task 4. ✔
- Deferred items (branch actions/fetch-pull-push, History content, gravatar, overlays, light-theme polish, QWidgets deletion) **named** in the File Structure "deferred" block — no silent caps. ✔

**Placeholder scan:** No "TBD"/"add error handling"/"similar to". Every code step shows full code. The one model-field assumption (`HeadState.oid` is the full hex, `.left(7)` for short form) matches the explored `HeadState` (`oid` = full 40-char hex). `make_dirty_repo` is inlined per test file (not shared) to avoid a cross-target link dependency on `tests/support`.

**Type consistency:**
- `installQmlContext(QQmlContext*, QmlTheme*, RepoListModel*, ProjectController*, RepoViewModel*)` — the 5-arg form is defined in Task 4 and used identically in `qml_main.cpp`, all `TestQmlShell` cases (Tasks 4–6), with the two pre-existing call sites updated to pass trailing `nullptr`.
- `ChangedFilesModel::Check { Unchecked=0, Partial=1, Checked=2 }` used consistently in `RepoViewModel` and matched against QML integer `model.checkState` (0/1/2) in `ChangesPane.qml`.
- Role keys are stable across model ↔ QML: `checkState`, `filePath`, `fileDir`, `fileName`, `statusLetter`, `statusKind` (Task 1/Task 5); `lineKind`, `lineText`, `checkable`, `lineChecked`, `oldNo`, `newNo`, `hunkIndex`, `lineIndex` (Task 2/Task 6).
- `RepoViewModel` invokables (`open`, `selectFile`, `setFileChecked`, `setAllFilesChecked`, `setLineChecked`, `setAllLinesChecked`, `commit`) and properties (`changedFiles`, `diffLines`, `currentBranch`, `checkedCount`, `activeFile`, `repoOpen`) match every QML reference in `BranchBar.qml`, `ChangesPane.qml`, `DiffView.qml`, `Sidebar.qml`.
- `RepoController` method/signal names (`open`, `refreshStatus`, `refreshBranches`, `refreshDiff`, `commitSelection`, `statusChanged`, `diffReady`, `headChanged`, `branchesChanged`, `operationFailed`) are used exactly as verified in `ui/include/gittide/ui/repocontroller.hpp`.
- `gittide::DiffTarget::WorktreeVsHead`, `gittide::StageSelection{path,hunkIndex,lineIndices}`, `gittide::CommitRequest{message}` match `core/include/gittide/diff.hpp`.
