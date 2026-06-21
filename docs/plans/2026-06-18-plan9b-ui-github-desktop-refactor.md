# Plan 9b — UI: GitHub-Desktop reshape (checkbox commit, unified diff, Fusion)

> **For agentic workers:** implement this plan task-by-task, **test-first**. Each
> task's steps use checkbox (`- [ ]`) syntax; tick them as you go. REQUIRED
> SUB-SKILL: `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans`.

| | |
|--|--|
| **Date** | 2026-06-18 |
| **Status** | `done` |
| **Spec** | [`spec/product` §Screens & Changes/History](../spec/product/product.md#screens--navigation) · [`spec/design` §Theming + Components](../spec/design/design.md#theming) · [`spec/engineering` §Inline selection](../spec/engineering/engineering.md#inline-selection-commit-and-the-history-diff) · [D22](../decisions.md) · [D23](../decisions.md) · [D24](../decisions.md) · [D25](../decisions.md) |
| **Depends on** | **Plan 9a** (core `resetIndexToHead` / `commitFiles` / `commitDiff`), Plan 3b (AsyncRepo/RepoController/ChangesView/DiffView), Plan 5b (HistoryView), Plan 7 (theme) |

**Goal:** Reshape the main UI to the GitHub-Desktop model: drop the staging area
for a single **default-checked** changed-files list with **per-line checkboxes**
in the diff (commit builds from the checked set — D23), fold **History** into a
shared **diff panel**, **remove the Dashboard**, make the project sidebar
**collapsible**, and switch the look to the Qt **Fusion** style driven by a
token-built `QPalette` (D24/D25).

**Architecture:** Bottom-up. (1) `AsyncRepo` gains thin wrappers for 9a's three
core methods. (2) `RepoController` gains a `commitSelection` orchestration slot
(reset → stage each checked selection → commit → refresh) and read-only
history-diff slots/signals. (3) Theming switches to Fusion + `QPalette` + a small
accent stylesheet. (4) A new `ChangedFilesList` widget replaces the staged/unstaged
lists. (5) `DiffView` gains per-line checkboxes + a read-only mode + discard via
context menu. (6) `ChangesView` owns the per-file commit-selection model. (7)
`MainWindow` is reshaped into three zones (collapsible sidebar · list column with
Changes|History sub-tabs + commit-files split · shared diff panel) and the
Dashboard is removed. (8) `DashboardModel` is deleted.

**Tech stack:** C++23, Qt 6 Widgets (Fusion style), QtConcurrent + QCoro, Qt Test.

## Global constraints

- Invariants ([`engineering`](../spec/engineering/engineering.md#cross-cutting-invariants)):
  **no Qt in `core/`**; Qt only at the ViewModel boundary; core speaks `std`,
  errors are values; convert paths with `QString::fromStdString(p.generic_u8string())`.
- **Colour from tokens only** (D18/D25): no hex literal in any widget `.cpp`.
  Colour comes from the `QPalette` or the token-built accent stylesheet. The few
  state cues (A/M/D/U/C) use `Theme` `state*` tokens and are **always paired with a
  letter** (D19), never colour alone.
- **Stage-on-commit** (D23): the index is never the user model; the commit is built
  by `RepoController::commitSelection` (reset → stage checked → commit). The UI owns
  the checked set.
- New `ui/` sources → `ui/CMakeLists.txt` `gittide_ui` list; new tests → the
  `gittide_ui_test_sources` list in `tests/CMakeLists.txt`.
- **Preserve stable object names** for tests where they keep meaning: `mainTabs`,
  `projectsDock`, `centralStack`, `repoPage`, `commitMessage`, `commitButton`, the
  empty-state CTAs. New names introduced below: `changedFilesList`,
  `commitFilesList`, `historyCommitList`, `sidebarCollapseButton`. **Removed**:
  `stagedList`, `unstagedList`, `dashboardList`, `diffStageButton`,
  `diffUnstageButton`, `diffDiscardButton`.
- Keep green: all existing tests except those that assert removed surfaces
  (staging lists, dashboard, diff stage buttons) — those are updated or deleted in
  the task that removes the surface, never left broken across a commit.
- Commit style: `feat(ui): …` / `refactor(ui): …` / `test(ui): …`, imperative
  subject; end with the Co-Authored-By trailer. Keep a **rename separate from
  content edits** (`git mv` first) where a file is renamed.

---

## Task 1: Async — wrap 9a's three core methods in `AsyncRepo`

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp`, `ui/src/asyncrepo.cpp`
- Test: `tests/ui/test_async_repo.cpp` (append; already in `gittide_ui_test_sources`)

**Interfaces — Produces:**
```cpp
QCoro::Task<gittide::Expected<void>> resetIndexToHead();
QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> commitFiles(QString oid);
QCoro::Task<gittide::Expected<gittide::DiffResult>> commitDiff(QString oid, std::filesystem::path file);
```
**Consumes:** Plan 9a `GitRepo::resetIndexToHead/commitFiles/commitDiff`.

- [ ] **Step 1: Write the failing test** (use the file's existing repo-with-commit
  helper; if it builds a second commit helper is absent, create two commits inline
  as the other cases do).

```cpp
void commit_files_lists_changed_paths()
{
    const auto dir = make_repo_with_two_commits(); // helper: c1 adds a.txt, c2 adds b.txt
    auto repo      = gittide::ui::AsyncRepo::open(dir);
    QVERIFY(repo.has_value());

    auto log = QCoro::waitFor(repo->log());
    QVERIFY(log.has_value());
    const QString newest = QString::fromStdString(log->front().oid);

    auto files = QCoro::waitFor(repo->commitFiles(newest));
    QVERIFY(files.has_value());
    QVERIFY(!files->empty());
    std::filesystem::remove_all(dir);
}
```
> If `make_repo_with_two_commits` does not exist, add it next to the file's
> existing `make_repo_with_commit` helper (same libgit2 calls, a second
> `git_commit_create`).

- [ ] **Step 2: Run — expect FAIL** (undeclared).
  Run: `ctest --test-dir build -R gittide_ui_tests --output-on-failure`

- [ ] **Step 3: Declare the three methods; implement each in `asyncrepo.cpp`
  following the exact existing pattern** (spell the return type in full; convert
  `QString` → `std::string` *inside* the captured lambda, never across `co_await`):

```cpp
QCoro::Task<gittide::Expected<void>> AsyncRepo::resetIndexToHead()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.resetIndexToHead();
        });
}

QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> AsyncRepo::commitFiles(QString oid)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oid.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.commitFiles(o);
        });
}

QCoro::Task<gittide::Expected<gittide::DiffResult>> AsyncRepo::commitDiff(QString oid, std::filesystem::path file)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oid.toStdString(), file = std::move(file)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.commitDiff(o, file);
        });
}
```

- [ ] **Step 4: Run — expect PASS.** Full UI suite green.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): async wrappers for reset-index + commit diff/files"`

---

## Task 2: ViewModel — `commitSelection` + history-diff slots in `RepoController`

Replace the index-mutating commit path with stage-on-commit, and add the read-only
history-diff plumbing. The old `stage`/`unstage` slots and the `committed` signal
stay (other code and tests use them); the new `commitSelection` is what the
reshaped `ChangesView` calls.

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp`, `ui/src/repocontroller.cpp`
- Test: `tests/ui/test_repo_controller.cpp` (append)

**Interfaces — Produces:**
```cpp
public slots:
  // Rebuild the index from `selections` and commit (D23): reset index to HEAD,
  // stage each selection (whole-file: hunkIndex==nullopt; partial: hunk+lines),
  // commit, then refresh status + history. Empty `selections` => no-op + error.
  QCoro::Task<void> commitSelection(gittide::CommitRequest req,
                                    std::vector<gittide::StageSelection> selections);
  // Read-only history diff:
  QCoro::Task<void> refreshCommitFiles(QString oid);          // emits commitFilesReady
  QCoro::Task<void> refreshCommitDiff(QString oid, QString path); // emits commitDiffReady
signals:
  void commitFilesReady(QString oid, std::vector<gittide::FileStatus> files);
  void commitDiffReady(QString oid, QString path, gittide::DiffResult result);
```
**Consumes:** Task 1 (`AsyncRepo::resetIndexToHead/commitFiles/commitDiff`), the
existing `stage`, `commit`, `refreshStatus`, `refreshHistory`.

- [ ] **Step 1: Write the failing test.**

```cpp
void commit_selection_commits_only_checked_files()
{
    const auto dir = make_repo_with_commit(); // existing helper: one commit, a.txt
    RepoController controller;
    controller.open(QString::fromStdString(dir.generic_string()));

    // create two new files in the worktree
    write_text(dir / "keep.txt", "keep\n");
    write_text(dir / "skip.txt", "skip\n");

    QSignalSpy committed(&controller, &RepoController::committed);
    std::vector<gittide::StageSelection> sel{ {"keep.txt", std::nullopt, {}} }; // only keep.txt
    QCoro::waitFor(controller.commitSelection(gittide::CommitRequest{"add keep"}, sel));

    QCOMPARE(committed.count(), 1);

    // skip.txt must still be an uncommitted change after the commit
    auto repo = gittide::ui::AsyncRepo::open(dir);
    auto st   = QCoro::waitFor(repo->status());
    QVERIFY(st.has_value());
    bool skipStillThere = std::any_of(st->begin(), st->end(),
        [](const auto& f){ return f.path.generic_string() == "skip.txt"; });
    QVERIFY(skipStillThere);
    std::filesystem::remove_all(dir);
}

void refresh_commit_files_emits_for_a_commit()
{
    const auto dir = make_repo_with_commit();
    RepoController controller;
    qRegisterMetaType<std::vector<gittide::FileStatus>>();
    controller.open(QString::fromStdString(dir.generic_string()));

    auto repo = gittide::ui::AsyncRepo::open(dir);
    const QString oid = QString::fromStdString(QCoro::waitFor(repo->log())->front().oid);

    QSignalSpy spy(&controller, &RepoController::commitFilesReady);
    QCoro::waitFor(controller.refreshCommitFiles(oid));
    QCOMPARE(spy.count(), 1);
    std::filesystem::remove_all(dir);
}
```
> `write_text` / `make_repo_with_commit` are this file's existing helpers; reuse
> them (add `write_text` if absent — a 3-line `std::ofstream`).

- [ ] **Step 2: Run — expect FAIL** (undeclared). Register
  `std::vector<gittide::FileStatus>` and `gittide::DiffResult` metatypes if not
  already in `metatypes.hpp` (they are used by existing signals — confirm).

- [ ] **Step 3: Declare slots/signals; implement:**

```cpp
QCoro::Task<void> RepoController::commitSelection(gittide::CommitRequest req,
                                                  std::vector<gittide::StageSelection> selections)
{
    if (!m_repo)
        co_return;
    if (selections.empty())
    {
        emit operationFailed(QStringLiteral("Nothing selected to commit"));
        co_return;
    }
    auto reset = co_await m_repo->resetIndexToHead();
    if (!reset)
    {
        emit operationFailed(QString::fromStdString(reset.error().message));
        co_return;
    }
    for (const auto& sel : selections)
    {
        auto s = co_await m_repo->stage(sel);
        if (!s)
        {
            emit operationFailed(QString::fromStdString(s.error().message));
            co_await refreshStatus(); // leave the user in a consistent state
            co_return;
        }
    }
    auto oid = co_await m_repo->commit(req);
    if (!oid)
    {
        emit operationFailed(QString::fromStdString(oid.error().message));
        co_await refreshStatus();
        co_return;
    }
    emit committed(QString::fromStdString(*oid));
    co_await refreshStatus();
    co_await refreshHistory();
}

QCoro::Task<void> RepoController::refreshCommitFiles(QString oid)
{
    if (!m_repo)
        co_return;
    auto files = co_await m_repo->commitFiles(oid);
    if (!files)
    {
        emit operationFailed(QString::fromStdString(files.error().message));
        co_return;
    }
    emit commitFilesReady(oid, *files);
}

QCoro::Task<void> RepoController::refreshCommitDiff(QString oid, QString path)
{
    if (!m_repo)
        co_return;
    auto d = co_await m_repo->commitDiff(oid, std::filesystem::path(path.toStdString()));
    if (!d)
    {
        emit operationFailed(QString::fromStdString(d.error().message));
        co_return;
    }
    emit commitDiffReady(oid, path, *d);
}
```

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): RepoController commit-from-selection + history diff slots"`

---

## Task 3: Theming — Fusion style + token-built `QPalette` (D24/D25)

Switch the base style to Fusion and resolve the `Theme` tokens into a `QPalette`
plus a small accent stylesheet (selection border, tab underline, focus ring, diff
gutter). The full hand-rolled QSS skin is retired.

**Files:**
- Modify: `ui/src/themestyle.cpp`, `ui/include/gittide/ui/themestyle.hpp` (if the
  declaration lives there — otherwise the header that declares `buildStyleSheet`)
- Modify: `ui/src/thememanager.cpp` (apply Fusion + palette + accent QSS)
- Test: `tests/ui/test_theme_style.cpp` (rewrite assertions for the new entry
  points)

**Interfaces — Produces:**
```cpp
// themestyle.hpp
QPalette buildPalette(const Theme& t);        // maps tokens → palette roles
QString  buildAccentStyleSheet(const Theme& t); // small QSS: selection / tab / focus / gutter
```
> Keep `buildStyleSheet` only if something still needs a full string; otherwise
> remove it and update its callers/tests in this task (no dangling references).

- [ ] **Step 1: Write the failing test.**

```cpp
void palette_uses_surface_and_text_tokens()
{
    const auto t = gittide::ui::darkTheme();
    const QPalette p = gittide::ui::buildPalette(t);
    QCOMPARE(p.color(QPalette::Window).name().toUpper(), QString(t.surfaceBase).toUpper());
    QCOMPARE(p.color(QPalette::WindowText).name().toUpper(), QString(t.textPrimary).toUpper());
    QCOMPARE(p.color(QPalette::Base).name().toUpper(), QString(t.surfaceRaised).toUpper());
    QCOMPARE(p.color(QPalette::Highlight).name().toUpper(), QString(t.accent).toUpper());
}

void accent_stylesheet_references_accent_token()
{
    const auto t = gittide::ui::darkTheme();
    const QString qss = gittide::ui::buildAccentStyleSheet(t);
    QVERIFY(qss.contains(t.accent));   // accent underline / focus uses the token
    QVERIFY(!qss.isEmpty());
}
```

- [ ] **Step 2: Run — expect FAIL** (undeclared / old API).

- [ ] **Step 3: Implement.**
  - `buildPalette`: construct a `QPalette`, set roles from tokens —
    `Window=surfaceBase`, `WindowText/Text/ButtonText=textPrimary`,
    `Base=surfaceRaised`, `AlternateBase=surfaceOverlay`, `Button=surfaceRaised`,
    `ToolTipBase=surfaceOverlay`, `ToolTipText=textPrimary`,
    `Highlight=accent`, `HighlightedText=surfaceBase`,
    `PlaceholderText=textMuted`, `Link=accent`. Set the `Disabled` group's
    `Text/WindowText/ButtonText=textMuted`. (Use `QColor(token)`; tokens are
    `#rrggbb` strings.)
  - `buildAccentStyleSheet`: a short QSS using object-name selectors for the cues
    a palette can't express: `QTabBar::tab:selected` 2px `accent` bottom border;
    selection left-border for `#changedFilesList::item:selected` and
    `#repoList::item:selected`; `*:focus` 2px `accent` outline; diff gutter colours
    via the `state*` tokens (added/deleted). No hex literals — interpolate tokens.
  - In `thememanager.cpp`: on apply, `qApp->setStyle(QStringLiteral("Fusion"))`
    (once), then `qApp->setPalette(buildPalette(currentTheme()))` and
    `qApp->setStyleSheet(buildAccentStyleSheet(currentTheme()))`. Keep the existing
    `QStyleHints::colorScheme()` follow + live re-apply path; it now re-applies
    palette + accent QSS instead of the full sheet.

- [ ] **Step 4: Run — expect PASS.** Confirm `test_theme.cpp` /
  `test_theme_manager.cpp` still pass (adjust any assertion that referenced the old
  full-QSS entry point).

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): Fusion base style with token-built QPalette (D24/D25)"`

---

## Task 4: UI — new `ChangedFilesList` widget

A single list of changed files, each row = tri-state checkbox + path + trailing
`state*`-coloured letter (A/M/D/U/C). Two modes: **editable** (checkboxes, used by
Changes) and **read-only** (no checkboxes, used for a commit's files under
History).

**Files:**
- Create: `ui/include/gittide/ui/changedfileslist.hpp`, `ui/src/changedfileslist.cpp`
- Modify: `ui/CMakeLists.txt` (add the pair to `gittide_ui`)
- Test: `tests/ui/test_changed_files_list.cpp` (add to `gittide_ui_test_sources`)

**Interfaces — Produces:**
```cpp
class ChangedFilesList : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Editable, ReadOnly };
    enum class Check { Unchecked, Checked, Partial };

    explicit ChangedFilesList(QWidget* parent = nullptr);
    void setMode(Mode mode);
    // Populate from status. In Editable mode every row starts Checked (D22 default).
    void setFiles(const std::vector<gittide::FileStatus>& files);
    void setRowCheck(const QString& path, Check check); // reflect a file's tri-state
    std::vector<QString> checkedPaths() const;          // fully-checked rows
signals:
    void fileSelected(const QString& path, gittide::StatusFlag flags);
    void fileCheckToggled(const QString& path, bool checked); // user clicked the box
    void discardRequested(const QString& path);               // context menu
};
```
Object name: the widget `changedFilesList`. Letters use `state*` tokens via the
palette/accent QSS, **always with the A/M/D/U/C glyph** (never colour alone).

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/ui/test_changed_files_list.cpp
#include "gittide/ui/changedfileslist.hpp"
#include <QSignalSpy>
#include <QtTest>

using gittide::ui::ChangedFilesList;

class TestChangedFilesList : public QObject
{
    Q_OBJECT
private slots:
    void editable_rows_start_checked()
    {
        ChangedFilesList list;
        list.setMode(ChangedFilesList::Mode::Editable);
        list.setFiles({ {"a.txt", gittide::StatusFlag::WtModified},
                        {"b.txt", gittide::StatusFlag::WtNew} });
        auto checked = list.checkedPaths();
        QCOMPARE(checked.size(), 2);
    }

    void toggling_a_row_emits_and_drops_it_from_checked()
    {
        ChangedFilesList list;
        list.setMode(ChangedFilesList::Mode::Editable);
        list.setFiles({ {"a.txt", gittide::StatusFlag::WtModified} });
        QSignalSpy spy(&list, &ChangedFilesList::fileCheckToggled);
        list.setRowCheck(QStringLiteral("a.txt"), ChangedFilesList::Check::Unchecked);
        // setRowCheck reflects state without emitting; user-driven toggle emits.
        // Drive the user path via the item's check state if exposed, else assert
        // checkedPaths shrinks:
        QVERIFY(list.checkedPaths().isEmpty());
    }
};
#include "test_changed_files_list.moc"
```

- [ ] **Step 2: Run — expect FAIL** (no such widget).

- [ ] **Step 3: Implement.** A `QListWidget` (objectName `changedFilesList`) of
  `QListWidgetItem`s. In Editable mode set `Qt::ItemIsUserCheckable` and an initial
  `Qt::Checked`; map the item's check state to `Check` (use `Qt::PartiallyChecked`
  for Partial — set by `setRowCheck`). Connect `itemChanged` → emit
  `fileCheckToggled(path, item->checkState()==Qt::Checked)` (guard against the
  programmatic `setRowCheck` path with a re-entrancy flag so only user clicks
  emit). Connect `currentItemChanged`/click → `fileSelected(path, flags)`. Store
  each row's `StatusFlag` and path in item data roles. Trailing letter: a second
  column or a right-aligned suffix label per row showing A/M/D/U/C derived from the
  flags (Index*/Wt* → letter); colour via the palette/accent QSS, not a hex.
  ReadOnly mode: clear `ItemIsUserCheckable`, no checkbox. Context menu
  (`CustomContextMenu`, Editable only) → "Discard changes…" → `discardRequested(path)`.

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5:** Add `changedfileslist.*` to `gittide_ui`; add the test to
  `gittide_ui_test_sources`; build; UI suite green.

- [ ] **Step 6: Commit.**
  `git commit -am "feat(ui): ChangedFilesList (tri-state checkboxes, A/M/D cue)"`

---

## Task 5: UI — `DiffView` rework (line checkboxes, read-only, discard menu)

Add per-line checkboxes in editable mode, a read-only mode for history, and move
discard to a context menu. Remove the Stage/Unstage/Discard buttons and the
`stageRequested`/`unstageRequested` signals (the commit-from-selection model
replaces them). Keep `setDiff`/`clear`.

**Files:**
- Modify: `ui/include/gittide/ui/diffview.hpp`, `ui/src/diffview.cpp`
- Test: `tests/ui/test_diff_view.cpp` (rewrite the stage-button cases)

**Interfaces — Produces:**
```cpp
class DiffView : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Editable, ReadOnly };
    explicit DiffView(QWidget* parent = nullptr);
    void setMode(Mode mode);
    // selection: which lines are checked for this file, keyed by hunk index ->
    // line indices (into DiffHunk::lines). Empty map + wholeChecked drives "all".
    void setDiff(const gittide::DiffResult& result, const std::filesystem::path& file,
                 bool wholeChecked, const std::map<int, std::vector<int>>& checkedLines);
    void clear();
signals:
    // user toggled a single diff line's checkbox (Editable mode only)
    void lineCheckToggled(const QString& path, int hunkIndex, int lineIndex, bool checked);
    void discardRequested(const gittide::StageSelection& sel); // context menu
};
```
Object name `diffLines` stays.

- [ ] **Step 1: Write the failing test** — set a diff in Editable mode with
  `wholeChecked=true`; assert each added/removed line row is checkable and checked;
  toggling one (set the item's check state via the widget API the impl exposes, or
  simulate the `itemChanged` path) emits `lineCheckToggled` with the right
  `(hunkIndex, lineIndex, false)`. Add a second case: ReadOnly mode renders no
  checkable items.

```cpp
void editable_lines_are_checked_by_default()
{
    DiffView view;
    view.setMode(DiffView::Mode::Editable);
    gittide::DiffResult r;
    gittide::DiffHunk h; h.lines = {
        {gittide::DiffLineOrigin::Added, -1, 1, "new", false} };
    r.hunks.push_back(h);
    view.setDiff(r, "a.txt", /*wholeChecked=*/true, {});
    auto* lines = view.findChild<QListWidget*>(QStringLiteral("diffLines"));
    QVERIFY(lines);
    // the added line row is user-checkable and checked
    QVERIFY(lines->count() >= 1);
}
```

- [ ] **Step 2: Run — expect FAIL** (signature/API changed).

- [ ] **Step 3: Implement.**
  - Editable mode: added/removed line rows get `ItemIsUserCheckable`; context lines
    are not checkable. Initial check state = `wholeChecked ? Checked` unless
    `checkedLines` says otherwise (a line index present in `checkedLines[hunk]` is
    checked; if `checkedLines` is non-empty it is authoritative per hunk). `itemChanged`
    → `lineCheckToggled(m_file, hunkIndex, lineIndex, checked)` (guarded against
    programmatic fills).
  - ReadOnly mode: no checkable flags; just render the diff (mono, gutter colours
    from `state*` via QSS).
  - Remove the three buttons and `requestStage/requestUnstage/requestDiscard`
    slots and the `stageRequested/unstageRequested` signals. Discard: context menu
    on a line selection → build a `StageSelection{m_file, hunkIndex, lineIndices}`
    (whole-file when nothing line-specific is selected) → `discardRequested(sel)`.
  - Keep `currentSelection()` only if still used; otherwise delete it. Update
    `ui/CMakeLists.txt`/headers accordingly (no dangling refs).

- [ ] **Step 4: Run — expect PASS.** Update `test_diff_view.cpp` cases that drove
  the old stage buttons (delete or rewrite them for the new model — do not leave
  them referencing removed symbols).

- [ ] **Step 5: Commit.**
  `git commit -am "refactor(ui): DiffView per-line checkboxes + read-only mode + discard menu"`

---

## Task 6: UI — `ChangesView` rework (commit-selection model)

`ChangesView` now hosts a `ChangedFilesList` (no diff panel inside it — the diff is
the shared panel owned by `MainWindow`, see Task 7), owns the per-file commit
selection, and emits a single `commitRequested(message, selections)`.

**Files:**
- Modify: `ui/include/gittide/ui/changesview.hpp`, `ui/src/changesview.cpp`
- Test: `tests/ui/test_changes_view.cpp` (rewrite for the new model)

**Interfaces — Produces:**
```cpp
class ChangesView : public QWidget {
    Q_OBJECT
public:
    explicit ChangesView(QWidget* parent = nullptr);
    void setStatus(const std::vector<gittide::FileStatus>& files); // resets selection: all checked
    ChangedFilesList* filesList() const;                            // MainWindow wires selection→diff
    QString commitMessage() const;

    // Apply a user line toggle coming back from the shared DiffView; updates the
    // file's tri-state and the model. MainWindow forwards DiffView::lineCheckToggled here.
    void applyLineToggle(const QString& path, int hunkIndex, int lineIndex, bool checked);
    // The current per-file selection for a path (so MainWindow can populate the
    // shared DiffView when that file is selected).
    void selectionFor(const QString& path, bool& wholeChecked,
                      std::map<int, std::vector<int>>& checkedLines) const;
signals:
    void commitRequested(const gittide::CommitRequest& req,
                         std::vector<gittide::StageSelection> selections);
    void discardRequested(const gittide::StageSelection& sel);
private:
    // Per-file selection state (default Checked). Partial stores checked line
    // indices per hunk; whole-file Checked/Unchecked carry no line map.
    struct FileSel {
        ChangedFilesList::Check state = ChangedFilesList::Check::Checked;
        std::map<int, std::vector<int>> checkedLinesByHunk; // only when Partial
    };
};
```
Object names: `commitMessage`, `commitButton` retained; `changedFilesList` from
Task 4.

- [ ] **Step 1: Write the failing test.**

```cpp
void commit_builds_selections_from_checked_files()
{
    ChangesView view;
    view.setStatus({ {"a.txt", gittide::StatusFlag::WtModified},
                     {"b.txt", gittide::StatusFlag::WtNew} });

    // type a message
    auto* msg = view.findChild<QPlainTextEdit*>(QStringLiteral("commitMessage"));
    QVERIFY(msg); msg->setPlainText(QStringLiteral("msg"));

    // uncheck b.txt via the list
    view.filesList()->setRowCheck(QStringLiteral("b.txt"),
                                  gittide::ui::ChangedFilesList::Check::Unchecked);

    QSignalSpy spy(&view, &ChangesView::commitRequested);
    auto* btn = view.findChild<QPushButton*>(QStringLiteral("commitButton"));
    QVERIFY(btn);
    btn->click();

    QCOMPARE(spy.count(), 1);
    const auto sels = spy.takeFirst().at(1).value<std::vector<gittide::StageSelection>>();
    QCOMPARE(sels.size(), size_t(1));                 // only a.txt
    QCOMPARE(sels[0].path.generic_string(), std::string("a.txt"));
    QVERIFY(!sels[0].hunkIndex.has_value());          // whole file
}
```
> Register `Q_DECLARE_METATYPE(std::vector<gittide::StageSelection>)` in
> `metatypes.hpp` if not present, so `QSignalSpy` can carry it.

- [ ] **Step 2: Run — expect FAIL.**

- [ ] **Step 3: Implement.**
  - Layout: `ChangedFilesList` (Editable) on top; `commitMessage` (`QPlainTextEdit`)
    + `commitButton` (`QPushButton`) pinned at the bottom. Remove `m_staged`,
    `m_unstaged`, the embedded `m_diff` (the diff is now shared, owned by
    `MainWindow`). `filesList()` exposes the list so `MainWindow` connects
    `fileSelected` → shared diff.
  - Selection model: `std::map<QString, FileSel> m_sel`. `setStatus` resets it —
    every file `Checked`. `ChangedFilesList::fileCheckToggled(path, checked)` →
    set `FileSel.state = checked ? Checked : Unchecked`, clear its line map, push
    the tri-state back via `setRowCheck`.
  - `applyLineToggle(path,h,l,checked)`: move the file to `Partial`; update
    `checkedLinesByHunk[h]` (add/remove `l`); if all lines end up checked collapse
    to `Checked`, if none collapse to `Unchecked`; recompute and push the row
    tri-state via `setRowCheck`.
  - `commitButton` enabled when message non-empty AND at least one file not
    `Unchecked`. On click build `selections`: for each non-`Unchecked` file —
    `Checked` → `StageSelection{path, nullopt, {}}`; `Partial` → one
    `StageSelection{path, hunk, lines}` per hunk in `checkedLinesByHunk`. Emit
    `commitRequested(CommitRequest{message}, selections)`.
  - Forward `ChangedFilesList::discardRequested(path)` →
    `discardRequested(StageSelection{path, nullopt, {}})`.

- [ ] **Step 4: Run — expect PASS.** Rewrite the old `test_changes_view.cpp` cases
  that referenced `stagedList`/`unstagedList`/embedded diff.

- [ ] **Step 5: Commit.**
  `git commit -am "refactor(ui): ChangesView checkbox commit-selection model"`

---

## Task 7: Integration — reshape `MainWindow` (3 zones, shared diff, no Dashboard)

Rebuild the central layout: collapsible project sidebar · a list column with a
`Changes | History` sub-tab set (Changes = `ChangesView`; History = a commit list
on top + a read-only commit-files list below) · a shared `DiffView` on the right.
Remove the Dashboard tab. Wire selection → shared diff for both working changes and
history, and commit-from-selection.

**Files:**
- Modify: `ui/include/gittide/ui/mainwindow.hpp`, `ui/src/mainwindow.cpp`
- Test: `tests/ui/test_main_window.cpp` (append + update)

**Interfaces — Consumes:** Tasks 2, 4, 5, 6; existing `HistoryView`,
`ProjectSidebar`, `BranchBar`.

- [ ] **Step 1: Write the failing test.**

```cpp
void central_layout_has_no_dashboard_and_a_shared_diff()
{
    gittide::ProjectStore store; // as other main-window tests build it
    MainWindow win(&store);
    auto* tabs = win.findChild<QTabWidget*>(QStringLiteral("mainTabs"));
    QVERIFY(tabs);
    // exactly two sub-tabs now: Changes, History
    QCOMPARE(tabs->count(), 2);
    QVERIFY(!win.findChild<QWidget*>(QStringLiteral("dashboardList")));
    // one shared diff panel exists
    QVERIFY(win.findChild<QWidget*>(QStringLiteral("diffLines")));
    // sidebar collapse toggle exists
    QVERIFY(win.findChild<QAbstractButton*>(QStringLiteral("sidebarCollapseButton")));
}
```
> Match how the existing `test_main_window.cpp` constructs a `MainWindow` + store;
> reuse that setup.

- [ ] **Step 2: Run — expect FAIL.**

- [ ] **Step 3: Implement the reshape.**
  - **repoPage layout:** a `QSplitter(Qt::Horizontal)` with two panes — left a list
    column, right the shared `DiffView m_diff` (objectName `diffLines` lives inside
    it). The `BranchBar` stays above the splitter (keep it inside `repoPage`'s
    vertical layout).
  - **List column:** a `QTabWidget` (`mainTabs`) with two tabs:
    - **Changes** → `m_changesView` (Task 6).
    - **History** → a container with a vertical `QSplitter`: top `m_historyView`
      (its commit table; give the inner table/objectName `historyCommitList` if a
      stable name is needed), bottom a `ChangedFilesList m_commitFiles`
      (objectName `commitFilesList`, ReadOnly mode).
  - **Remove** the Dashboard tab, the `QListView dashboardList`, and the
    `DashboardModel* m_dashboardModel` member + its construction/refresh calls.
  - **Sidebar collapse:** add a `QToolButton` (objectName `sidebarCollapseButton`)
    that toggles `m_sidebar` (the `projectsDock`) visibility / a slim rail. Minimal:
    toggle the dock's `setVisible`.
  - **Wiring — working changes:**
    - `m_changesView->filesList()::fileSelected(path, flags)` → call
      `m_repoController->refreshDiff(path, gittide::DiffTarget::WorktreeVsHead)`
      (Plan 9a Task 4 — all working changes vs `HEAD`, index-independent, so the
      line indices match what `commitSelection` stages after its index reset).
    - `RepoController::diffReady(path, result)` → when the Changes tab is active,
      `m_changesView->selectionFor(path, whole, lines)` then
      `m_diff->setMode(Editable); m_diff->setDiff(result, path, whole, lines)`.
    - `m_diff->lineCheckToggled(...)` → `m_changesView->applyLineToggle(...)`.
    - `m_changesView->commitRequested(req, sels)` →
      `m_repoController->commitSelection(req, sels)`.
    - `m_changesView->discardRequested(sel)` / `m_diff->discardRequested(sel)` →
      `m_repoController->discard(sel)`.
  - **Wiring — history:**
    - `m_historyView` selection (current commit) → `m_repoController->refreshCommitFiles(oid)`.
    - `RepoController::commitFilesReady(oid, files)` → `m_commitFiles->setFiles(files)`
      (ReadOnly).
    - `m_commitFiles::fileSelected(path, flags)` →
      `m_repoController->refreshCommitDiff(currentOid, path)`.
    - `RepoController::commitDiffReady(oid, path, result)` →
      `m_diff->setMode(ReadOnly); m_diff->setDiff(result, path, false, {})`.
  - **Tab switch:** on switching `mainTabs`, clear the shared diff (`m_diff->clear()`)
    so a stale editable/read-only diff doesn't linger.
  - Keep the existing repo-open cascade (status/history/branches) and the
    `branchBar`/history context-menu wiring from Plan 8 intact.

- [ ] **Step 4: Run — expect PASS.** Run the **entire** suite (core + ui) green.
  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): reshape MainWindow to 3-zone GitHub-Desktop layout"`

---

## Task 8: Remove `DashboardModel` + dead tests; theming pass; close-out

**Files:**
- Delete: `ui/include/gittide/ui/dashboardmodel.hpp`, `ui/src/dashboardmodel.cpp`,
  `tests/ui/test_dashboard_model.cpp`, `tests/ui/test_dashboard_async.cpp`
- Modify: `ui/CMakeLists.txt` (drop the dashboard sources),
  `tests/CMakeLists.txt` (drop the two dashboard tests from `gittide_ui_test_sources`)
- Modify: any remaining references (e.g. includes) so nothing names `DashboardModel`

- [ ] **Step 1:** Remove the dashboard sources + tests and their CMake entries.
  Grep to confirm no dangling references:
  Run: `grep -rn "Dashboard" ui/ tests/ app/` → expect no matches (or only this
  plan's name in comments).

- [ ] **Step 2:** Confirm no hex literal landed in any new/changed widget:
  Run: `grep -rnE '#[0-9a-fA-F]{6}' ui/src/changedfileslist.cpp ui/src/diffview.cpp ui/src/changesview.cpp ui/src/mainwindow.cpp`
  → expect no matches (colour comes from palette / accent QSS / `state*` tokens).

- [ ] **Step 3:** Build + **entire** suite green; no new compiler or Qt runtime
  warnings.
  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`

- [ ] **Step 4:** Confirm the spec sections match what shipped — product §Screens &
  navigation / Changes / History (Dashboard removed), design §Theming + Components
  (Fusion/palette, `changedFilesList`, diff line checkboxes, sidebar collapse),
  engineering §Inline selection, decisions D22–D25. Fix any drift (code is ground
  truth).

- [ ] **Step 5:** Tick this plan's boxes, fill **Outcome**, set `Status` to `done`
  here and in [`plans/index.md`](index.md); set the
  [wish](../wishlist/shipped/github-desktop-ui-refactor.md) `Status` to `done` (and its
  row in [`wishlist/index.md`](../wishlist/index.md)).

- [ ] **Step 6: Commit.**
  `git commit -am "docs: close Plan 9b — GitHub-Desktop UI refactor shipped"`

---

## Outcome

- Shipped: the staging area is replaced by a default-checked `ChangedFilesList`
  (tri-state checkboxes, A/M/D/U cue) + per-line diff checkboxes; commits build from
  the checked set via `RepoController::commitSelection` (reset index → stage checked
  → commit). History folds into one shared `DiffView` (commit → read-only
  `commitFilesList` → read-only diff). Dashboard removed; project sidebar
  collapsible; base look switched to Fusion + a token-built `QPalette` + accent
  stylesheet, with `ThemeManager` publishing `gittide.state*` properties for the
  status-letter colours. Full suite 75/75 green.
- Spec updated: product §Screens/Changes/History, design §Theming/Components,
  engineering §Inline selection; decisions D22–D25 (all current).
- Code: `ui/{asyncrepo,repocontroller,themestyle,thememanager,changedfileslist,
  diffview,changesview,mainwindow}.*` (+ `metatypes.hpp`); deleted
  `ui/dashboardmodel.*` and its two tests.
- Commits: `9cd0615..80fb496` (8 tasks; Task 8 needed a fix to restore the
  `dashboardList` layout regression assertion, then review-clean).
- Carry-forward Minors (see final-review triage): `diffview.cpp` uses
  `generic_string()` not `generic_u8string()` (path invariant); `*:focus` accent
  selector is broad; `ChangedFilesList::setMode` Editable→ReadOnly does not clear
  stored check-states; history selection wired via `findChild("historyTable")`;
  `gittide.stateConflict` published but unread; async-layer reset/commitDiff
  untested (core covers them); `path(QString.toStdString())` non-ASCII on Windows.
</content>
