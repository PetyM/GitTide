# Plan 30 — Diff syntax highlighting

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-06-30 |
| **Status** | `done` |
| **Spec** | [`spec/product/2026-06-30-diff-syntax-highlighting-design.md`](../spec/product/2026-06-30-diff-syntax-highlighting-design.md); decisions D45, D46 |
| **Depends on** | — |

**Goal:** Render diff lines with language-aware syntax colouring in both the
working-changes diff and the read-only history diff, falling back to today's plain
rendering for unsupported languages.

**Architecture:** Entirely `ui/`. A new `SyntaxHighlighter` helper wraps
KSyntaxHighlighting (a `Repository` + an `AbstractHighlighter` subclass) and turns
a stream of lines into one HTML string per line. `DiffLinesModel` precomputes that
HTML at `setDiff()` time — highlighting each hunk's old side (context+removed) and
new side (context+added) as separate stateful streams — and exposes it as a new
`lineHtml` role. The QML delegates render `lineHtml` as `Text.RichText` when
present, else fall back to today's plain-text path.

**Tech stack:** C++23, Qt 6 (Gui/Quick), KSyntaxHighlighting (KF6), QTest.

## Global constraints

- Invariants in [`spec/engineering/engineering.md`](../spec/engineering/engineering.md):
  **no Qt in `core/`** (this plan touches only `ui/` — `core/` diff types are
  untouched); colour from theme tokens (relaxed for syntax classes per **D45** —
  KSyntax bundled themes, data-driven, not hex-in-widget).
- New `ui/` sources → `ui/CMakeLists.txt`. New tests → the
  `gittide_ui_test_sources` list in `tests/CMakeLists.txt` **and** an `#include` +
  `QTest::qExec` block in `tests/ui/main.cpp` (both edits — see that file's header).
- KSyntaxHighlighting is linked **PRIVATE** to `gittide_ui`; no public ui header
  includes a KSyntax header (`syntaxhighlighter.hpp` forward-declares
  `KSyntaxHighlighting::Repository`).
- Must keep passing: every existing `objectName`, the existing
  `test_diff_lines_model` / `test_difflinesmodel_conflict` assertions (the new
  `setDiff` parameter is appended with a default so current callers/tests compile
  unchanged).
- TDD: failing test first. Allman braces, `m_` members, lowercase filenames
  (`.clang-format`).

---

## Task 1: Add the KSyntaxHighlighting dependency (build spike)

This task is a **build-vetting spike** — it must prove the dependency configures,
builds, and links before any feature code is written. If the primary approach does
not configure within reason, switch to the fallback and **stop to report** before
continuing.

**Files:**
- Modify: `cmake/Dependencies.cmake` (inside the `if(GITGUI_BUILD_UI)` block)
- Modify: `ui/CMakeLists.txt` (link the new target)
- Modify: `README.md` (document the system package / CI dependency)

**Interfaces:**
- Produces: a linkable CMake target `KF6::SyntaxHighlighting` available to
  `gittide_ui`.

- [ ] **Step 1: Add the dependency (primary: FetchContent).** In
  `cmake/Dependencies.cmake`, inside `if(GITGUI_BUILD_UI)` **after** the existing
  `find_package(Qt6 ...)` line, add:

```cmake
  # --- Syntax highlighting (KDE Frameworks) ---
  # KSyntaxHighlighting needs ECM (KDE's CMake modules) on the module path first.
  FetchContent_Declare(
    ecm
    GIT_REPOSITORY https://invent.kde.org/frameworks/extra-cmake-modules.git
    GIT_TAG        v6.5.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(ecm)
  list(APPEND CMAKE_MODULE_PATH "${ecm_SOURCE_DIR}/modules" "${ecm_SOURCE_DIR}/kde-modules")

  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)            # KSyntax's own tests
  FetchContent_Declare(
    KF6SyntaxHighlighting
    GIT_REPOSITORY https://invent.kde.org/frameworks/syntax-highlighting.git
    GIT_TAG        v6.5.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(KF6SyntaxHighlighting)
```

- [ ] **Step 2: Link it PRIVATE.** In `ui/CMakeLists.txt`, add a `PRIVATE` clause
  to the existing `target_link_libraries(gittide_ui PUBLIC ...)` call (KSyntax is
  an implementation detail — no public ui header includes it, only forward-declares
  `Repository`):

```cmake
target_link_libraries(gittide_ui PUBLIC
  gittide_core Qt6::Gui Qt6::Concurrent Qt6::Svg Qt6::Qml Qt6::Quick Qt6::QuickControls2 QCoro6::Core)
target_link_libraries(gittide_ui PRIVATE KF6::SyntaxHighlighting)
```

- [ ] **Step 3: Configure and build.** Run:

```bash
cmake -S . -B build
cmake --build build --target gittide_ui --parallel
```

Expected: configures and builds `gittide_ui` with no errors. The first configure
clones ECM + syntax-highlighting (slow once).

- [ ] **Step 4: Fallback if FetchContent fails.** If Step 3 cannot configure
  (ECM/KDE module resolution errors that are not quickly fixable), replace the
  Step 1 block with a system-package lookup:

```cmake
  # Requires the system package: Debian/Ubuntu `libkf6syntaxhighlighting-dev`,
  # Fedora `kf6-syntax-highlighting-devel`, macOS `brew install ksyntaxhighlighting`.
  find_package(KF6SyntaxHighlighting REQUIRED)
```

Install on this machine: `sudo apt-get install -y libkf6syntaxhighlighting-dev`,
then re-run Step 3. **Stop and report** which approach worked (it changes CI and
the D45 wording).

- [ ] **Step 5: Document the dependency.** In `README.md`'s build/dependencies
  section, add a line noting KSyntaxHighlighting is required (system package name
  if Step 4 was used, or "fetched automatically" if Step 1 worked), so CI and new
  contributors install it.

- [ ] **Step 6: Commit.**

```bash
git add cmake/Dependencies.cmake ui/CMakeLists.txt README.md
git commit -m "build(ui): add KSyntaxHighlighting dependency"
```

---

## Task 2: `SyntaxHighlighter` helper

**Files:**
- Create: `ui/include/gittide/ui/syntaxhighlighter.hpp`
- Create: `ui/src/syntaxhighlighter.cpp`
- Modify: `ui/CMakeLists.txt` (add both to the `gittide_ui` source list)
- Create: `tests/ui/test_syntaxhighlighter.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/ui/main.cpp` (register the test)

**Interfaces:**
- Produces:
  ```cpp
  namespace gittide::ui {
  class SyntaxHighlighter {
  public:
      SyntaxHighlighter();
      ~SyntaxHighlighter();
      /// True when a language definition exists for @p filePath's name/extension.
      bool hasDefinition(const QString& filePath) const;
      /// Highlight @p lines as one stateful stream (state carries line→line, so
      /// multi-line comments/strings colour correctly). Returns one HTML string
      /// per input line: HTML-escaped text with `<span style="color:#rrggbb">`
      /// runs from the active KSyntax theme. Returns an empty vector when no
      /// definition matches @p filePath (caller falls back to plain text).
      /// @p dark selects the bundled dark vs light theme.
      std::vector<QString> highlightLines(const QString& filePath,
                                          const std::vector<QString>& lines,
                                          bool dark) const;
  };
  }
  ```

- [ ] **Step 1: Write the failing test.** Create
  `tests/ui/test_syntaxhighlighter.cpp`:

```cpp
#include <QtTest>
#include <vector>

#include "gittide/ui/syntaxhighlighter.hpp"

using gittide::ui::SyntaxHighlighter;

class TestSyntaxHighlighter : public QObject
{
    Q_OBJECT
private slots:
    void cppKeywordGetsColourSpan()
    {
        SyntaxHighlighter hl;
        QVERIFY(hl.hasDefinition("main.cpp"));
        const std::vector<QString> out =
            hl.highlightLines("main.cpp", {QStringLiteral("int x = 1;")}, /*dark=*/true);
        QCOMPARE(out.size(), std::size_t(1));
        // Some token must be wrapped in a colour span.
        QVERIFY(out[0].contains(QStringLiteral("<span style=\"color:#")));
    }

    void escapesHtmlSpecials()
    {
        SyntaxHighlighter hl;
        const std::vector<QString> out =
            hl.highlightLines("main.cpp", {QStringLiteral("a < b && c > d;")}, true);
        QCOMPARE(out.size(), std::size_t(1));
        QVERIFY(out[0].contains(QStringLiteral("&lt;")));
        QVERIFY(out[0].contains(QStringLiteral("&gt;")));
        QVERIFY(out[0].contains(QStringLiteral("&amp;")));
        QVERIFY(!out[0].contains(QStringLiteral("< b")));   // raw '<' must be escaped
    }

    void unknownExtensionReturnsEmpty()
    {
        SyntaxHighlighter hl;
        QVERIFY(!hl.hasDefinition("notes.weirdext"));
        const std::vector<QString> out =
            hl.highlightLines("notes.weirdext", {QStringLiteral("anything")}, true);
        QVERIFY(out.empty());
    }
};
```

  Register it: add
  `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_syntaxhighlighter.cpp` to the
  `gittide_ui_test_sources` list in `tests/CMakeLists.txt`; in `tests/ui/main.cpp`
  add `#include "test_syntaxhighlighter.cpp"` with the other includes and, in
  `main()`, `TestSyntaxHighlighter t_sh; status |= QTest::qExec(&t_sh, argc, argv);`.

- [ ] **Step 2: Run it to verify it fails.**

```bash
cmake --build build --target gittide_ui_tests --parallel
```

  Expected: **compile failure** — `syntaxhighlighter.hpp` does not exist yet.

- [ ] **Step 3: Write the header.** Create
  `ui/include/gittide/ui/syntaxhighlighter.hpp`:

```cpp
#pragma once
#include <memory>
#include <vector>

#include <QString>

namespace KSyntaxHighlighting
{
class Repository;
}

namespace gittide::ui
{

/// Lexical syntax highlighter over KSyntaxHighlighting. Owns a Repository (the
/// expensive-to-build definition/theme store) and produces per-line HTML for a
/// stateful stream of lines. UI-only; no core dependency. See Plan 30 / D45.
class SyntaxHighlighter
{
public:
    SyntaxHighlighter();
    ~SyntaxHighlighter();

    bool hasDefinition(const QString& filePath) const;

    std::vector<QString> highlightLines(const QString& filePath,
                                        const std::vector<QString>& lines,
                                        bool dark) const;

private:
    std::unique_ptr<KSyntaxHighlighting::Repository> m_repo;
};

} // namespace gittide::ui
```

- [ ] **Step 4: Write the implementation.** Create
  `ui/src/syntaxhighlighter.cpp`:

```cpp
#include "gittide/ui/syntaxhighlighter.hpp"

#include <QColor>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

using namespace KSyntaxHighlighting;

namespace gittide::ui
{

namespace
{

/// AbstractHighlighter that assembles one HTML string per highlighted line.
/// applyFormat() is called for formatted spans; gaps between spans (and any
/// trailing remainder) are emitted as escaped, default-coloured text.
class HtmlCollector : public AbstractHighlighter
{
public:
    void beginLine(const QString& text)
    {
        m_text = text;
        m_html.clear();
        m_cursor = 0;
    }

    QString endLine()
    {
        if (m_cursor < m_text.size())
            m_html += m_text.mid(m_cursor).toHtmlEscaped();
        return m_html;
    }

protected:
    void applyFormat(int offset, int length, const Format& format) override
    {
        if (length <= 0)
            return;
        if (offset > m_cursor)
            m_html += m_text.mid(m_cursor, offset - m_cursor).toHtmlEscaped();

        const QString seg = m_text.mid(offset, length).toHtmlEscaped();
        const QColor  col = format.textColor(theme());
        if (col.isValid())
            m_html += QStringLiteral("<span style=\"color:%1\">%2</span>").arg(col.name(), seg);
        else
            m_html += seg;
        m_cursor = offset + length;
    }

private:
    QString m_text;
    QString m_html;
    int     m_cursor = 0;
};

} // namespace

SyntaxHighlighter::SyntaxHighlighter()
    : m_repo(std::make_unique<Repository>())
{
}

SyntaxHighlighter::~SyntaxHighlighter() = default;

bool SyntaxHighlighter::hasDefinition(const QString& filePath) const
{
    return m_repo->definitionForFileName(filePath).isValid();
}

std::vector<QString> SyntaxHighlighter::highlightLines(const QString& filePath,
                                                       const std::vector<QString>& lines,
                                                       bool dark) const
{
    const Definition def = m_repo->definitionForFileName(filePath);
    if (!def.isValid())
        return {};

    HtmlCollector hl;
    hl.setDefinition(def);
    hl.setTheme(m_repo->defaultTheme(dark ? Repository::DarkTheme : Repository::LightTheme));

    std::vector<QString> out;
    out.reserve(lines.size());
    State state;
    for (const QString& ln : lines)
    {
        hl.beginLine(ln);
        state = hl.highlightLine(ln, state);
        out.push_back(hl.endLine());
    }
    return out;
}

} // namespace gittide::ui
```

  Add both files to the `gittide_ui` source list in `ui/CMakeLists.txt` (next to
  `difflinesmodel.cpp` / its header).

- [ ] **Step 5: Run the test to verify it passes.**

```bash
cmake --build build --target gittide_ui_tests --parallel
ctest --test-dir build -R gittide_ui_tests --output-on-failure
```

  Expected: PASS. If `cppKeywordGetsColourSpan` fails because the test environment
  has no syntax definitions installed, that is a real environment problem — confirm
  `definitionForFileName("main.cpp")` resolves on this machine (it should with the
  dependency from Task 1).

- [ ] **Step 6: Commit.**

```bash
git add ui/include/gittide/ui/syntaxhighlighter.hpp ui/src/syntaxhighlighter.cpp \
        ui/CMakeLists.txt tests/ui/test_syntaxhighlighter.cpp \
        tests/CMakeLists.txt tests/ui/main.cpp
git commit -m "feat(ui): add SyntaxHighlighter (KSyntax -> per-line HTML)"
```

---

## Task 3: `DiffLinesModel` — `lineHtml` role + precompute

**Files:**
- Modify: `ui/include/gittide/ui/difflinesmodel.hpp`
- Modify: `ui/src/difflinesmodel.cpp`
- Modify: `tests/ui/test_diff_lines_model.cpp`

**Interfaces:**
- Consumes: `SyntaxHighlighter` (Task 2).
- Produces:
  - New role `HtmlRole` exposed as `"lineHtml"`.
  - `setDiff(..., bool blocks = false, const QString& filePath = QString())` — the
    appended `filePath` drives highlighting; empty/unsupported → all `lineHtml`
    empty.
  - `Q_INVOKABLE void setSyntaxDark(bool dark)` — re-highlights and emits
    `dataChanged` over `HtmlRole` when the value changes.

- [ ] **Step 1: Write the failing test.** Append to
  `tests/ui/test_diff_lines_model.cpp` a new test in its existing test class (it
  already has `roleKey()` and a one-hunk diff helper). Add:

```cpp
void codeRowsCarryHtmlForKnownLanguage()
{
    DiffLinesModel m;
    gittide::DiffResult diff = oneHunkDiff();   // existing helper: ctx/added/removed
    m.setDiff(diff, {}, false, /*blocks=*/false, QStringLiteral("main.cpp"));

    const int htmlRole = roleKey(m, "lineHtml");
    const int kindRole = roleKey(m, "lineKind");
    QVERIFY(htmlRole != -1);

    bool sawCodeHtml = false;
    for (int i = 0; i < m.rowCount(); ++i)
    {
        const QString kind = m.data(m.index(i, 0), kindRole).toString();
        const QString html = m.data(m.index(i, 0), htmlRole).toString();
        if (kind == "hunk")
            QVERIFY(html.isEmpty());            // headers never highlighted
        if (kind == "added" || kind == "removed" || kind == "context")
            if (!html.isEmpty())
                sawCodeHtml = true;
    }
    QVERIFY(sawCodeHtml);                        // at least one code row got HTML
}

void unknownLanguageLeavesHtmlEmpty()
{
    DiffLinesModel m;
    gittide::DiffResult diff = oneHunkDiff();
    m.setDiff(diff, {}, false, false, QStringLiteral("notes.weirdext"));

    const int htmlRole = roleKey(m, "lineHtml");
    for (int i = 0; i < m.rowCount(); ++i)
        QVERIFY(m.data(m.index(i, 0), htmlRole).toString().isEmpty());
}
```

  (No `tests/ui/main.cpp` change — the class is already registered.)

- [ ] **Step 2: Run it to verify it fails.**

```bash
cmake --build build --target gittide_ui_tests --parallel
```

  Expected: **compile failure** — `setDiff` has no 5-arg form and `lineHtml` role
  does not exist.

- [ ] **Step 3: Extend the header.** In
  `ui/include/gittide/ui/difflinesmodel.hpp`:

  - Add the include and member. After the existing includes add
    `#include "gittide/ui/syntaxhighlighter.hpp"`.
  - In the `Roles` enum, add `HtmlRole` after `ConflictRegionRole`.
  - Change the `setDiff` declaration to append the parameter:

```cpp
    void setDiff(const gittide::DiffResult& result,
                 const std::map<int, std::vector<int>>& checkedLines,
                 bool wholeChecked,
                 bool blocks = false,
                 const QString& filePath = QString());
```

  - Add the invokable after `setAllChecked`:

```cpp
    /// Re-highlight all rows for the given theme brightness (true = dark). Emits
    /// dataChanged over HtmlRole when the value changes. No-op if unchanged.
    Q_INVOKABLE void setSyntaxDark(bool dark);
```

  - In `struct Row`, add `QString html; ///< per-line syntax HTML; empty = plain`.
  - In the private members (next to `m_conflictText`), add:

```cpp
    SyntaxHighlighter m_highlighter; ///< owns the KSyntax Repository (built once)
    bool    m_syntaxDark = true;     ///< theme brightness for highlighting
    QString m_filePath;              ///< path of the diff'd file (drives language)
```

  - Declare the private helper next to `refreshBlock`:

```cpp
    void rehighlightRows(); ///< fill Row::html per hunk, per side; no signals
```

- [ ] **Step 4: Implement.** In `ui/src/difflinesmodel.cpp`:

  - In `data()`, add a case:

```cpp
    case HtmlRole:
        return r.html;
```

  - In `roleNames()`, add `{HtmlRole, "lineHtml"},`.

  - Change the `setDiff` signature to match the header (add the `filePath` param),
    store it, and call the highlight pass just before `endResetModel()`:

```cpp
void DiffLinesModel::setDiff(const gittide::DiffResult& result,
                             const std::map<int, std::vector<int>>& checkedLines,
                             bool wholeChecked,
                             bool blocks,
                             const QString& filePath)
{
    beginResetModel();
    m_rows.clear();
    m_filePath = filePath;
    // ... existing hunk/line/block building loop unchanged ...
    rehighlightRows();
    endResetModel();
}
```

  - Add the highlight pass and the invokable (anywhere after `setDiff`, before the
    conflict section):

```cpp
void DiffLinesModel::rehighlightRows()
{
    for (Row& r : m_rows)
        r.html.clear();
    if (m_filePath.isEmpty() || !m_highlighter.hasDefinition(m_filePath))
        return;

    int maxHunk = -1;
    for (const Row& r : m_rows)
        maxHunk = std::max(maxHunk, r.hunkIndex);

    const QString kContext = QStringLiteral("context");
    const QString kAdded   = QStringLiteral("added");
    const QString kRemoved = QStringLiteral("removed");

    auto run = [&](const std::vector<int>& idxs)
    {
        if (idxs.empty())
            return;
        std::vector<QString> texts;
        texts.reserve(idxs.size());
        for (int i : idxs)
            texts.push_back(m_rows[static_cast<std::size_t>(i)].text);
        const std::vector<QString> html =
            m_highlighter.highlightLines(m_filePath, texts, m_syntaxDark);
        if (html.size() != idxs.size())
            return; // defensive: highlighter gave an unexpected count
        for (std::size_t k = 0; k < idxs.size(); ++k)
            m_rows[static_cast<std::size_t>(idxs[k])].html = html[k];
    };

    // Per hunk: old side = context+removed, new side = context+added, in row
    // order. State resets per hunk (gaps between hunks are unobservable).
    for (int h = 0; h <= maxHunk; ++h)
    {
        std::vector<int> oldRows, newRows;
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        {
            const Row& r = m_rows[static_cast<std::size_t>(i)];
            if (r.hunkIndex != h)
                continue;
            if (r.kind == kContext)
            {
                oldRows.push_back(i);
                newRows.push_back(i);
            }
            else if (r.kind == kRemoved)
                oldRows.push_back(i);
            else if (r.kind == kAdded)
                newRows.push_back(i);
        }
        run(oldRows); // fills context + removed
        run(newRows); // fills added (and re-fills context with identical HTML)
    }
}

void DiffLinesModel::setSyntaxDark(bool dark)
{
    if (m_syntaxDark == dark)
        return;
    m_syntaxDark = dark;
    rehighlightRows();
    if (!m_rows.empty())
        emit dataChanged(index(0, 0),
                         index(static_cast<int>(m_rows.size()) - 1, 0),
                         {HtmlRole});
}
```

- [ ] **Step 5: Run the tests to verify they pass.**

```bash
cmake --build build --target gittide_ui_tests --parallel
ctest --test-dir build -R gittide_ui_tests --output-on-failure
```

  Expected: PASS, including the pre-existing `test_diff_lines_model` and
  `test_difflinesmodel_conflict` cases (the new param defaults preserve their
  behaviour).

- [ ] **Step 6: Commit.**

```bash
git add ui/include/gittide/ui/difflinesmodel.hpp ui/src/difflinesmodel.cpp \
        tests/ui/test_diff_lines_model.cpp
git commit -m "feat(ui): DiffLinesModel precomputes per-line syntax HTML (lineHtml role)"
```

---

## Task 4: Wire the path through + render RichText in the delegates

**Files:**
- Modify: `ui/src/repoviewmodel.cpp` (3 `setDiff` call sites)
- Modify: `ui/qml/DiffView.qml` (working diff delegate + theme wiring)
- Modify: `ui/qml/CommitDetail.qml` (history diff delegate + theme wiring)

**Interfaces:**
- Consumes: `DiffLinesModel::setDiff(..., filePath)` and `setSyntaxDark` (Task 3).

- [ ] **Step 1: Pass the file path at each `setDiff` call.** In
  `ui/src/repoviewmodel.cpp`:

  - In `onDiff` (~line 613):

```cpp
    m_diff->setDiff(result, fs.checkedLinesByHunk,
                    fs.state == ChangedFilesModel::Checked, /*blocks=*/true, path);
```

  - In `onCommitDiff` (~line 793):

```cpp
    m_commitDiff->setDiff(result, {}, false, /*blocks=*/false, path);
```

  - In `onRangeDiff` (~line 887):

```cpp
    m_commitDiff->setDiff(result, {}, false, /*blocks=*/false, path);
```

  (`path` is the repo-relative file path already passed into each of these slots;
  its extension is all KSyntax needs.)

- [ ] **Step 2: Render RichText in the working-changes delegate.** In
  `ui/qml/DiffView.qml`, replace the code/text `Label` (the last `Label` in the
  normal-row `RowLayout`, currently binding `text: model.lineText`) with:

```qml
                Label {
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    textFormat: model.lineHtml && model.lineHtml.length > 0
                                ? Text.RichText : Text.PlainText
                    text: model.lineHtml && model.lineHtml.length > 0
                          ? model.lineHtml : model.lineText
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

  (When `lineHtml` is present the per-span colours win; `color` is only the base
  for any unspanned remainder. Add/remove signalling stays via the row background
  and the `+`/`−` sign column, both unchanged.)

- [ ] **Step 3: Keep the highlight theme in sync (working diff).** In
  `ui/qml/DiffView.qml`, on the root `ColumnLayout` (`id: diffView`), add:

```qml
    // Keep the diff model's syntax theme aligned with the app theme.
    property bool syntaxDark: theme.dark
    onSyntaxDarkChanged: if (repoVm && repoVm.diffLines) repoVm.diffLines.setSyntaxDark(syntaxDark)
    Component.onCompleted: if (repoVm && repoVm.diffLines) repoVm.diffLines.setSyntaxDark(theme.dark)
```

- [ ] **Step 4: Render RichText in the history-diff delegate.** In
  `ui/qml/CommitDetail.qml`, replace the code `Label` (lines ~192–202, binding
  `text: model.lineText`) with:

```qml
                Label {
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    textFormat: model.lineHtml && model.lineHtml.length > 0
                                ? Text.RichText : Text.PlainText
                    text: model.lineHtml && model.lineHtml.length > 0
                          ? model.lineHtml : model.lineText
                    color: model.lineKind === "hunk"   ? theme.textMuted
                         : (model.lineHtml && model.lineHtml.length > 0) ? theme.textPrimary
                         : model.lineKind === "added"  ? theme.stateAdded
                         : model.lineKind === "removed" ? theme.stateDeleted
                         : theme.textPrimary
                }
```

- [ ] **Step 5: Keep the highlight theme in sync (history diff).** In
  `ui/qml/CommitDetail.qml`, on its root item, add the same wiring against
  `repoVm.commitDiff`:

```qml
    property bool syntaxDark: theme.dark
    onSyntaxDarkChanged: if (repoVm && repoVm.commitDiff) repoVm.commitDiff.setSyntaxDark(syntaxDark)
    Component.onCompleted: if (repoVm && repoVm.commitDiff) repoVm.commitDiff.setSyntaxDark(theme.dark)
```

  (If the root item already has a `Component.onCompleted`, merge the body rather
  than adding a second handler.)

- [ ] **Step 6: Build everything and run the full suite.**

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

  Expected: all tests pass (no `objectName` changed; the existing QML tests assert
  structure, not text colour).

- [ ] **Step 7: Manual check.** Launch the app, open a repo with a `.cpp`/`.py`/
  `.js` change, confirm the working-changes diff and a history commit's diff show
  coloured code; open a file with an unknown extension and confirm it renders as
  before. Toggle the theme (light/dark) and confirm colours update.

- [ ] **Step 8: Commit.**

```bash
git add ui/src/repoviewmodel.cpp ui/qml/DiffView.qml ui/qml/CommitDetail.qml
git commit -m "feat(ui): render syntax-highlighted diff lines as RichText"
```

---

## Task 5: Documentation & close-out

**Files:**
- Modify: `docs/spec/product/product.md` (or the diff section) — note diffs are
  syntax-highlighted.
- Modify: `docs/spec/design/design.md` — note syntax colours come from KSyntax
  bundled themes layered over the add/remove backgrounds (D45).
- Modify: `docs/spec/engineering/engineering.md` — note the
  `SyntaxHighlighter` → `DiffLinesModel.lineHtml` → delegate seam, KSyntax as a
  `ui/`-private dependency.
- Modify: `docs/decisions.md` — if Task 1 used the `find_package` fallback, adjust
  D45's "FetchContent" wording to match what shipped.
- Modify: `docs/plans/index.md` — add the Plan 30 row.
- Modify: this plan's **Outcome** section.
- Modify: `docs/wishlist/diff-syntax-highlighting.md` and
  `docs/wishlist/index.md` — flip Status to `done`, set **Shipped** date, move the
  file to `docs/wishlist/shipped/` and its row to the Shipped table.

- [ ] **Step 1: Update the spec** sections above to describe the shipped behaviour
  (symbol-level facts stay in the new Doxygen comments; keep the spec cross-cutting).

- [ ] **Step 2: Reconcile D45** with the dependency mechanism actually used in
  Task 1 (FetchContent vs `find_package`).

- [ ] **Step 3: Close the plan** — set Status `done`, fill **Outcome** (what
  shipped, where it lives: `SyntaxHighlighter`, `DiffLinesModel::lineHtml`,
  `DiffView.qml`/`CommitDetail.qml`), add the `docs/plans/index.md` row.

- [ ] **Step 4: Close the wish** — Status `done`, **Shipped** date, move file to
  `docs/wishlist/shipped/`, move its row to the Shipped table in
  `docs/wishlist/index.md`, fix links.

- [ ] **Step 5: Commit.**

```bash
git add docs/
git commit -m "docs: close out Plan 30 (diff syntax highlighting)"
```

---

## Outcome

Shipped: language-aware syntax highlighting on diff lines in both the
working-changes diff and the read-only history diff, via KSyntaxHighlighting
(KF6). Unsupported languages fall back to the prior plain rendering unchanged.

Code (branch `feat/diff-syntax-highlighting`, 6 commits, merged to master as
"Merge Plan 30"):
- `ui/include/gittide/ui/syntaxhighlighter.{hpp,cpp}` — `SyntaxHighlighter`
  wraps a KSyntax `Repository` + an `AbstractHighlighter` that emits per-line
  HTML (escaped text + colour `<span>`s). `hpp` forward-declares
  `KSyntaxHighlighting::Repository`; KSyntax stays PRIVATE to `gittide_ui`.
- `DiffLinesModel` — new `HtmlRole` (`lineHtml`); `setDiff(..., filePath)`
  precomputes HTML per code row via a single-pass per-hunk old/new stateful
  stream split; `setSyntaxDark(bool)` re-highlights on theme change.
- `DiffView.qml` / `CommitDetail.qml` — code label renders `lineHtml` as
  `Text.RichText` (with `clip: true`) when present, else the prior plain path;
  `setSyntaxDark(theme.dark)` wired per view.
- `cmake/Dependencies.cmake` — KSyntaxHighlighting + ECM via FetchContent
  (QCoro-before-ECM module-path ordering; `KF6::SyntaxHighlighting` ALIAS).

Decisions: **D45** (KSyntaxHighlighting + bundled themes — FetchContent path
used, matching the design), **D46** (rich-text-per-line). (Renumbered from a
provisional D42/D43 to avoid collision with the concurrent stash-management
D44.)

Operational note: the KSyntax/ECM FetchContent dependency registers ~80 ECM
self-tests (e.g. `ECMPoQmToolsTest`) into the project's ctest context; they
cannot be suppressed in-repo, so **CI must run `ctest -E '^ECM'`** to keep the
project suite clean.

Spec updated: this design doc
(`spec/product/2026-06-30-diff-syntax-highlighting-design.md`) holds the
cross-cutting design. **Deferred (shared-doc coordination):** the
`spec/product/product.md`, `spec/design/design.md`,
`spec/engineering/engineering.md` cross-references, the `decisions.md`
D45/D46 entries, and the `wishlist/index.md` Shipped-table move were left to a
later pass because the same files were being live-edited by the concurrent
Plan 31 (stash) session; the D45/D46 text already sits in the working-tree
`decisions.md`.

Follow-up minors (non-blocking): `BUILD_TESTING OFF` not restored after the
KSyntax fetch; no dedicated `setSyntaxDark` behavioural test.
