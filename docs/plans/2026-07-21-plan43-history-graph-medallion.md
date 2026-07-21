# Plan 43 — History/graph refinements + commit medallion

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-07-21 |
| **Status** | `planned` |
| **Spec** | [`spec/product/2026-07-21-history-graph-commit-medallion-design.md`](../spec/product/2026-07-21-history-graph-commit-medallion-design.md) |
| **Depends on** | Plan 9a/9b (commit selection + history diff), Plan 5b (history/graph view) |

**Goal:** Drop the hash from the History list; align the Graph tab into fixed
columns with vertically-stacked ref chips and dynamic row height; replace the
bare `Commit <hash>` header with a commit medallion (summary, body, author,
+/− and files-changed stats, copyable hash), fed by a new core
`commitDetail()` and async controller/viewmodel wiring.

**Architecture:** New pure-core `GitRepo::commitDetail(oid)` returns a
`CommitDetail` value (summary/body/author/stats) via a commit lookup +
`git_diff_get_stats` on the commit-vs-first-parent diff. The controller wraps it
as a QCoro task emitting `commitDetailReady`; the ViewModel caches the fields
into Q_PROPERTYs bound by `CommitDetail.qml`. The two QML-only changes (history
hash removal, graph columns) and the `GraphColumn` paint tweak are independent.

**Tech stack:** C++23 core (libgit2, `std::expected`), Qt6 QCoro async bridge,
Qt Quick/QML, Catch2 core tests.

## Global constraints

- No Qt in `core/`. libgit2/nlohmann PRIVATE to core. Core speaks `std`, returns
  `Expected<T>` — no exceptions across layers. (`spec/engineering/engineering.md`.)
- Colour only from theme tokens — no hex literals in QML.
- Paths via `generic_u8string()`, never `.string()`; use libgit2 API, never git
  command strings.
- New `core/` sources → `core/CMakeLists.txt`; new tests → the matching list in
  `tests/CMakeLists.txt`. (No new source *files* here — edits land in existing
  translation units; the new test file `tests/core/test_commit_detail.cpp` must be
  added to `tests/CMakeLists.txt`.)
- Keep passing: existing commit-selection flow, `commitFilesList` /
  `commitDiffList` object names, `checkoutCommitButton`.
- TDD: failing test first. Build: `cmake --build build --parallel`. Test:
  `ctest --test-dir build --output-on-failure`.

---

## Task 1: Core `GitRepo::commitDetail(oid)`

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare struct + method)
- Modify: `core/src/gitrepo.cpp` (implement, near `commitFiles` ~line 2272)
- Create: `tests/core/test_commit_detail.cpp`
- Modify: `tests/CMakeLists.txt` (add the test to the core test list)

**Interfaces:**
- Produces:
  ```cpp
  namespace gittide {
  struct CommitDetail
  {
      std::string summary;        // first line of the message
      std::string body;           // remaining message lines (may be empty)
      std::string authorName;
      std::string authorEmail;
      int64_t     authorTime = 0; // author unix timestamp
      int filesChanged = 0;
      int additions    = 0;
      int deletions    = 0;
  };
  }
  // In class GitRepo:
  Expected<CommitDetail> commitDetail(std::string oid) const;
  ```

- [ ] **Step 1: Write the failing test** — `tests/core/test_commit_detail.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::test::TempRepo;

TEST_CASE("commitDetail reports message split, author and line stats", "[commitdetail]")
{
    TempRepo repo;
    repo.setIdentity("Jane Doe", "jane@example.com");
    repo.writeFile("a.txt", "one\ntwo\n");
    repo.commitAll("base");

    // Second commit: +2 lines added to a new file, 1 line changed in a.txt.
    repo.writeFile("a.txt", "one\nCHANGED\n");
    repo.writeFile("b.txt", "x\ny\n");
    repo.commitAll("Add b and edit a\n\nSecond line of body.\nThird line.");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto log = gr->log(0);
    REQUIRE(log);
    const std::string tip = log->at(0).oid; // newest first

    auto d = gr->commitDetail(tip);
    REQUIRE(d);
    CHECK(d->summary == "Add b and edit a");
    CHECK(d->body == "Second line of body.\nThird line.");
    CHECK(d->authorName == "Jane Doe");
    CHECK(d->authorEmail == "jane@example.com");
    CHECK(d->authorTime > 0);
    CHECK(d->filesChanged == 2);     // a.txt modified, b.txt added
    CHECK(d->additions == 3);        // b.txt: 2, a.txt: 1
    CHECK(d->deletions == 1);        // a.txt: "two" removed
}

TEST_CASE("commitDetail on the root commit diffs against the empty tree", "[commitdetail]")
{
    TempRepo repo;
    repo.setIdentity("Jane Doe", "jane@example.com");
    repo.writeFile("a.txt", "one\ntwo\n");
    repo.commitAll("root");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto log = gr->log(0);
    REQUIRE(log);

    auto d = gr->commitDetail(log->at(0).oid);
    REQUIRE(d);
    CHECK(d->summary == "root");
    CHECK(d->body.empty());
    CHECK(d->filesChanged == 1);
    CHECK(d->additions == 2);
    CHECK(d->deletions == 0);
}
```

Add the file to `tests/CMakeLists.txt` in the same list that holds
`test_commit_email.cpp` (search for that name and append
`test_commit_detail.cpp` beside it).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --parallel && ctest --test-dir build -R commitdetail --output-on-failure`
Expected: FAIL — `commitDetail` not declared / link error.

- [ ] **Step 3: Declare struct + method.** In `core/include/gittide/gitrepo.hpp`,
  add the `CommitDetail` struct near the other commit-facing structs, and declare
  the method next to `commitFiles` (~line 151):

```cpp
// Summary/body split, author identity, and +/- line + files-changed stats for
// the commit identified by the 40-char hex oid, computed against its first
// parent (root commit: against an empty tree). Mirrors commitFiles()'s diff base.
Expected<CommitDetail> commitDetail(std::string oid) const;
```

- [ ] **Step 4: Implement** in `core/src/gitrepo.cpp` (after `commitFiles`, reusing
  the existing `commitTrees` helper at line 2205):

```cpp
Expected<CommitDetail> GitRepo::commitDetail(std::string oidHex) const
{
    git_oid oid;
    int rc = git_oid_fromstr(&oid, oidHex.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    git_commit* commit = nullptr;
    rc                 = git_commit_lookup(&commit, m_repo, &oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    CommitDetail out;
    if (const char* s = git_commit_summary(commit))
        out.summary = s;
    if (const char* b = git_commit_body(commit))
        out.body = b;
    if (const git_signature* a = git_commit_author(commit))
    {
        out.authorName  = a->name ? a->name : "";
        out.authorEmail = a->email ? a->email : "";
        out.authorTime  = a->when.time;
    }

    git_tree* tree       = nullptr;
    git_tree* parentTree = nullptr;
    if (auto r = commitTrees(oidHex, &tree, &parentTree); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> tree_guard(tree, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> parent_guard(parentTree, git_tree_free);

    git_diff* raw = nullptr;
    rc            = git_diff_tree_to_tree(&raw, m_repo, parentTree, tree, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    git_diff_stats* stats = nullptr;
    rc                    = git_diff_get_stats(&stats, raw);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    out.filesChanged = static_cast<int>(git_diff_stats_files_changed(stats));
    out.additions    = static_cast<int>(git_diff_stats_insertions(stats));
    out.deletions    = static_cast<int>(git_diff_stats_deletions(stats));
    git_diff_stats_free(stats);

    return out;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --parallel && ctest --test-dir build -R commitdetail --output-on-failure`
Expected: PASS (both cases).

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/core/test_commit_detail.cpp tests/CMakeLists.txt
git commit -m "feat(core): commitDetail() — message split, author, +/- stats"
```

---

## Task 2: Controller `refreshCommitDetail` + `commitDetailReady`

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp` (declare task + signal)
- Modify: `ui/src/repocontroller.cpp` (implement, near `refreshCommitFiles` ~line 682)
- Modify: `ui/include/gittide/ui/metatypes.hpp` (register `CommitDetail` for the signal)

**Interfaces:**
- Consumes: `GitRepo::commitDetail` (Task 1).
- Produces:
  ```cpp
  QCoro::Task<void> refreshCommitDetail(QString oid);         // public
  void commitDetailReady(QString oid, gittide::CommitDetail detail);  // signal
  ```

- [ ] **Step 1: Register the metatype.** In `ui/include/gittide/ui/metatypes.hpp`,
  beside `Q_DECLARE_METATYPE(std::vector<gittide::FileStatus>)` (line 47):

```cpp
Q_DECLARE_METATYPE(gittide::CommitDetail)
```

- [ ] **Step 2: Declare in the controller header.** Add the public method near
  `refreshCommitFiles`, and the signal beside `commitFilesReady`
  (`repocontroller.hpp:202`):

```cpp
// Fetch summary/body/author/stats for a commit; emits commitDetailReady on success.
QCoro::Task<void> refreshCommitDetail(QString oid);
```
```cpp
void commitDetailReady(QString oid, gittide::CommitDetail detail);
```

- [ ] **Step 3: Implement** in `ui/src/repocontroller.cpp`, mirroring
  `refreshCommitFiles` (line 682). Add a one-off `qRegisterMetaType` where the
  controller registers its other core types (search the ctor for
  `qRegisterMetaType<std::vector<gittide::FileStatus>>` and add beside it:
  `qRegisterMetaType<gittide::CommitDetail>();`):

```cpp
QCoro::Task<void> RepoController::refreshCommitDetail(QString oid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto detail = co_await m_repo->commitDetail(oid);
    if (!self)
        co_return;
    if (!detail)
    {
        emit operationFailed(QString::fromStdString(detail.error().message));
        co_return;
    }
    emit commitDetailReady(oid, *detail);
}
```

(Confirm `commitDetail` is reachable through the `AsyncRepo`/`m_repo` wrapper the
same way `commitFiles` is — it forwards to `GitRepo` on the worker. If `m_repo`
is an `AsyncRepo` that needs an explicit wrapper method, add a
`QCoro::Task<Expected<CommitDetail>> commitDetail(QString)` forwarder there next to
its `commitFiles` forwarder.)

- [ ] **Step 4: Build**

Run: `cmake --build build --parallel`
Expected: compiles clean.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp ui/include/gittide/ui/metatypes.hpp
git commit -m "feat(ui): controller refreshCommitDetail + commitDetailReady"
```

---

## Task 3: ViewModel detail properties + wiring

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` (Q_PROPERTYs, slot, signal, members)
- Modify: `ui/src/repoviewmodel.cpp` (connect, slot, kick off in `selectCommit`, clear on deselect)

**Interfaces:**
- Consumes: `RepoController::commitDetailReady` (Task 2).
- Produces QML-bound properties: `detailSummary`, `detailBody`, `detailAuthor`,
  `detailAuthorEmail`, `detailDate`, `detailFilesChanged`, `detailAdditions`,
  `detailDeletions`, all `NOTIFY commitDetailChanged`.

- [ ] **Step 1: Declare properties + slot + members** in `repoviewmodel.hpp`,
  beside `selectedCommit` (line 69):

```cpp
Q_PROPERTY(QString detailSummary READ detailSummary NOTIFY commitDetailChanged)
Q_PROPERTY(QString detailBody READ detailBody NOTIFY commitDetailChanged)
Q_PROPERTY(QString detailAuthor READ detailAuthor NOTIFY commitDetailChanged)
Q_PROPERTY(QString detailAuthorEmail READ detailAuthorEmail NOTIFY commitDetailChanged)
Q_PROPERTY(QString detailDate READ detailDate NOTIFY commitDetailChanged)
Q_PROPERTY(int detailFilesChanged READ detailFilesChanged NOTIFY commitDetailChanged)
Q_PROPERTY(int detailAdditions READ detailAdditions NOTIFY commitDetailChanged)
Q_PROPERTY(int detailDeletions READ detailDeletions NOTIFY commitDetailChanged)
```

Getters (inline or in .cpp — match the file's convention; `selectedCommit` uses a
.cpp getter):
```cpp
QString detailSummary() const { return m_detailSummary; }
QString detailBody() const { return m_detailBody; }
QString detailAuthor() const { return m_detailAuthor; }
QString detailAuthorEmail() const { return m_detailAuthorEmail; }
QString detailDate() const { return m_detailDate; }
int detailFilesChanged() const { return m_detailFilesChanged; }
int detailAdditions() const { return m_detailAdditions; }
int detailDeletions() const { return m_detailDeletions; }
```
Signal (in the `signals:` block, near `selectedCommitChanged`):
```cpp
void commitDetailChanged();
```
Private slot (near `onCommitFiles`):
```cpp
void onCommitDetail(const QString& oid, const gittide::CommitDetail& detail);
```
Members (near `m_selectedCommit`):
```cpp
QString m_detailSummary, m_detailBody, m_detailAuthor, m_detailAuthorEmail, m_detailDate;
int m_detailFilesChanged = 0, m_detailAdditions = 0, m_detailDeletions = 0;
```

- [ ] **Step 2: Connect + implement the slot** in `repoviewmodel.cpp`. Add the
  connection beside the `commitFilesReady` connect (line 45):

```cpp
connect(m_controller, &RepoController::commitDetailReady, this, &RepoViewModel::onCommitDetail);
```
Slot (add near `onCommitFiles`, ~line 815). Format the date exactly like the
history rows (`historylistmodel.cpp:79`):
```cpp
void RepoViewModel::onCommitDetail(const QString& oid, const gittide::CommitDetail& detail)
{
    if (oid != m_selectedCommit)
        return; // a newer selection superseded this fetch
    m_detailSummary      = QString::fromStdString(detail.summary);
    m_detailBody         = QString::fromStdString(detail.body);
    m_detailAuthor       = QString::fromStdString(detail.authorName);
    m_detailAuthorEmail  = QString::fromStdString(detail.authorEmail);
    m_detailDate         = QDateTime::fromSecsSinceEpoch(detail.authorTime)
                               .toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    m_detailFilesChanged = detail.filesChanged;
    m_detailAdditions    = detail.additions;
    m_detailDeletions    = detail.deletions;
    emit commitDetailChanged();
}
```
Ensure `#include <QDateTime>` is present in `repoviewmodel.cpp` (add if missing).

- [ ] **Step 3: Kick off the fetch in `selectCommit`.** In `selectCommit`
  (`repoviewmodel.cpp:765`), beside the existing
  `QCoro::connect(m_controller->refreshCommitFiles(oid), this, [] {});` (line 780):

```cpp
QCoro::connect(m_controller->refreshCommitDetail(oid), this, [] {});
```

- [ ] **Step 4: Clear on deselect.** Add a private helper and call it wherever
  `m_selectedCommit.clear()` runs (lines 174, 199, 898, 1186 — the deselect /
  range-mode paths):

```cpp
void RepoViewModel::clearCommitDetail()
{
    m_detailSummary.clear(); m_detailBody.clear();
    m_detailAuthor.clear();  m_detailAuthorEmail.clear(); m_detailDate.clear();
    m_detailFilesChanged = m_detailAdditions = m_detailDeletions = 0;
    emit commitDetailChanged();
}
```
Call `clearCommitDetail();` immediately after each `m_selectedCommit.clear();`.
(Declare `void clearCommitDetail();` in the header's private section.)

- [ ] **Step 5: Build + run existing UI tests**

Run: `cmake --build build --parallel && ctest --test-dir build -R gittide_ui_tests --output-on-failure`
Expected: PASS — no regressions. (Per memory: the ECMPoQmToolsTest failure is
spurious and unrelated.)

- [ ] **Step 6: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp
git commit -m "feat(ui): expose selected-commit detail props on RepoViewModel"
```

---

## Task 4: Commit medallion in `CommitDetail.qml`

**Files:**
- Modify: `ui/qml/CommitDetail.qml` (replace the header RowLayout, lines 34-57)

**Interfaces:**
- Consumes: `repoVm.detailSummary/detailBody/detailAuthor/detailAuthorEmail/`
  `detailDate/detailFilesChanged/detailAdditions/detailDeletions`,
  `repoVm.selectedCommit`, `repoVm.copyToClipboard`, `repoVm.checkoutCommit`.

- [ ] **Step 1: Replace the header block.** Swap the `RowLayout` at
  `CommitDetail.qml:34-57` for the medallion below. Keep the range/hint label
  (lines 20-30) and the `SplitView` (line 62+) untouched. Visibility gate stays:
  single-commit mode only (`historyDetailHeader`/`historyDetailHint` empty).

```qml
    // Commit medallion: summary, body, author, stats, copyable hash.
    // Shown only for a single-commit selection (range/stash keep their own header).
    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: 12
        spacing: 6
        visible: repoVm && repoVm.selectedCommit.length > 0
                 && repoVm.historyDetailHeader.length === 0
                 && repoVm.historyDetailHint.length === 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: repoVm ? repoVm.detailSummary : ""
                color: theme.textPrimary
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            AppButton {
                objectName: "checkoutCommitButton"
                variant: "secondary"
                text: "Checkout"
                onClicked: if (repoVm) repoVm.checkoutCommit(repoVm.selectedCommit)
            }
        }

        Label {
            Layout.fillWidth: true
            visible: repoVm && repoVm.detailBody.length > 0
            text: repoVm ? repoVm.detailBody : ""
            color: theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: repoVm ? (repoVm.detailAuthor
                            + (repoVm.detailAuthorEmail.length > 0
                               ? " <" + repoVm.detailAuthorEmail + ">" : "")
                            + "  ·  " + repoVm.detailDate) : ""
            color: theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        RowLayout {
            spacing: 12
            Label {
                text: (repoVm ? repoVm.detailFilesChanged : 0)
                      + (repoVm && repoVm.detailFilesChanged === 1 ? " file changed" : " files changed")
                color: theme.textMuted
                font.pixelSize: 11
            }
            Label {
                text: "+" + (repoVm ? repoVm.detailAdditions : 0)
                color: theme.stateAdded
                font.family: "monospace"
                font.pixelSize: 11
            }
            Label {
                text: "−" + (repoVm ? repoVm.detailDeletions : 0)
                color: theme.stateDeleted
                font.family: "monospace"
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            spacing: 6
            Label {
                text: repoVm && repoVm.selectedCommit.length > 0
                      ? repoVm.selectedCommit.substring(0, 10) : ""
                color: theme.textMuted
                font.family: "monospace"
                font.pixelSize: 11
            }
            AppButton {
                objectName: "copyHashButton"
                variant: "secondary"
                text: "Copy"
                visible: repoVm && repoVm.selectedCommit.length > 0
                onClicked: if (repoVm) repoVm.copyToClipboard(repoVm.selectedCommit)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy full commit hash")
            }
        }
    }
```

(If `AppButton` has no `hovered` property, drop the two `ToolTip` lines. Verify
against `ui/qml/AppButton.qml`.)

- [ ] **Step 2: Run the app and eyeball it.**

Run: `cmake --build build --parallel` then launch (see README "Build & test").
Select a commit in History → the medallion shows summary/body/author/stats/hash;
"Copy" puts the full 40-char oid on the clipboard; "Checkout" still works;
selecting a range shows the range header instead.

- [ ] **Step 3: Commit**

```bash
git add ui/qml/CommitDetail.qml
git commit -m "feat(ui): commit medallion in the detail panel"
```

---

## Task 5: Drop the hash from the History list

**Files:**
- Modify: `ui/qml/HistoryPane.qml` (remove the `model.shortOid` Label, lines 264-269)

- [ ] **Step 1: Delete the hash cell.** In the sub-line `RowLayout`
  (`HistoryPane.qml:257-277`), remove this Label entirely:

```qml
                            Label {
                                text: model.shortOid
                                color: theme.textMuted
                                font.family: "monospace"
                                font.pixelSize: 11
                            }
```

Leave the author Label (260-263) and the date Label (270-276) in place. The date
Label already has `Layout.fillWidth: true` + right alignment, so author-left /
date-right layout is preserved.

- [ ] **Step 2: Build + eyeball.**

Run: `cmake --build build --parallel`, launch, open History → rows show
author + date, no hash. Graph tab still shows its hash (unchanged).

- [ ] **Step 3: Commit**

```bash
git add ui/qml/HistoryPane.qml
git commit -m "feat(ui): drop short hash from the History list rows"
```

---

## Task 6: `GraphColumn` — top-anchored dot for variable row height

**Files:**
- Modify: `ui/include/gittide/ui/graphcolumn.hpp` (add a row-height constant)
- Modify: `ui/src/graphcolumn.cpp` (`paint`, lines 66-119)

Rationale: Task 7 makes graph rows grow when a commit has multiple stacked refs.
The dot and its incoming/outgoing edges currently key off `height()/2`, which
drifts to the middle of a tall row. Anchor them to a fixed offset (the first
line's centre) so the graph stays aligned with the top-aligned message.

- [ ] **Step 1: Add the constant.** In `graphcolumn.hpp` beside `kLaneWidth` /
  `kDotRadius`:

```cpp
static constexpr int kRowHeight = 48; // baseline row height; dot centres on the first line
```

- [ ] **Step 2: Anchor the dot in `paint`.** In `graphcolumn.cpp`, replace the
  `mid` definition (line 76) and use a fixed dot-Y throughout:

```cpp
    const int top = 0;
    const int bot = static_cast<int>(height());
    const int dotY = kRowHeight / 2; // fixed: centre of the first line, not of the row
    const int cx   = laneX(row.commit.lane);
```
Then substitute `dotY` for `mid` in the three places that used it:
```cpp
    // 2. Incoming line to the circle (top half).
    if (row.lineFromAbove)
    {
        pen(row.commit.lane);
        painter->drawLine(cx, top, cx, dotY);
    }

    // 3. Outgoing edges to parent lanes (below the dot).
    for (const auto& e : row.outEdges)
    {
        pen(e.toLane);
        painter->drawLine(laneX(e.fromLane), dotY, laneX(e.toLane), bot);
    }
```
and the dot:
```cpp
    painter->drawEllipse(QPoint(cx, dotY), kDotRadius, kDotRadius);
```
Pass-through verticals keep spanning `top`→`bot` (full height) — connectors stay
continuous across a tall row. Remove the now-unused `mid`.

- [ ] **Step 3: Build.**

Run: `cmake --build build --parallel`
Expected: compiles clean; graph looks identical while all rows are still 48px
(dotY == old mid).

- [ ] **Step 4: Commit**

```bash
git add ui/include/gittide/ui/graphcolumn.hpp ui/src/graphcolumn.cpp
git commit -m "feat(ui): anchor graph dot to the first line for tall rows"
```

---

## Task 7: Graph fixed ref column + stacked chips + dynamic height

**Files:**
- Modify: `ui/qml/GraphPane.qml` (delegate: row height 86-88, ref chips 148-167)

**Interfaces:**
- Consumes: `GraphColumn.kRowHeight` behaviour (Task 6) — dot stays on the first
  line as rows grow.

- [ ] **Step 1: Make the row height dynamic.** Replace the delegate root's fixed
  `height: 48` (`GraphPane.qml:88`) with a height that grows to fit stacked refs.
  Add a `refCount` readonly and compute height (chip = 16 tall + 2 spacing):

```qml
            delegate: Rectangle {
                width: ListView.view.width
                readonly property int refCount:
                    (typeof refLabels !== "undefined" && refLabels) ? refLabels.length : 0
                readonly property int kRefColW: 120
                readonly property int kRowH: 48
                height: Math.max(kRowH, 8 + refCount * 18) // 8 = top+bottom pad, 18 = chip+gap
                color: ListView.isCurrentItem ? theme.surfaceOverlay : "transparent"
```

- [ ] **Step 2: Top-align the content row and give it the full height.** The inner
  `RowLayout` (line 131) uses `anchors.fill: parent`, which vertically centres its
  children in a tall row. Keep `anchors.fill` but top-align each child so the first
  line stays put. Change the `RowLayout` and its children alignment:

```qml
                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 0
                    anchors.leftMargin: 8
                    anchors.rightMargin: 12
                    spacing: 8
```
Give the `GraphColumn` a fixed width equal to the reserved lane area and full
height (it already fills height); no change needed there beyond it staying first.

- [ ] **Step 3: Fixed-width, vertically-stacked ref column.** Replace the ref
  `Repeater` (lines 149-167) with a fixed-width `ColumnLayout` holding the chips
  stacked, top-aligned:

```qml
                    // Branch/tag chips, stacked vertically in a fixed-width column
                    // so the summary text starts at the same X on every row.
                    ColumnLayout {
                        Layout.preferredWidth: kRefColW
                        Layout.minimumWidth: kRefColW
                        Layout.maximumWidth: kRefColW
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: (kRowH - 16) / 2 // align first chip with the first line
                        spacing: 2
                        Repeater {
                            model: (typeof refLabels !== "undefined" && refLabels) ? refLabels : []
                            delegate: Rectangle {
                                radius: 3
                                color: theme.surfaceRaised
                                border.width: 1
                                border.color: theme.border
                                implicitHeight: 16
                                Layout.preferredWidth: Math.min(chipLabel.implicitWidth + 10, kRefColW)
                                Layout.alignment: Qt.AlignLeft
                                Label {
                                    id: chipLabel
                                    anchors.left: parent.left
                                    anchors.leftMargin: 5
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 10
                                    text: modelData
                                    elide: Text.ElideRight
                                    color: theme.textSecondary
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
```

- [ ] **Step 4: Top-align the avatar and summary column** so they sit on the first
  line rather than centring in a tall row. On the `Avatar` (line 169) change
  `Layout.alignment: Qt.AlignVCenter` → `Layout.alignment: Qt.AlignTop` and add
  `Layout.topMargin: (kRowH - height) / 2` (or a fixed `Layout.topMargin: 12` if
  `height` is not yet resolved). On the summary `ColumnLayout` (line 175) add
  `Layout.alignment: Qt.AlignTop` and `Layout.topMargin: 6`.

- [ ] **Step 5: Build + eyeball.**

Run: `cmake --build build --parallel`, launch, open Graph on a repo with several
branches/tags. Verify: summary text begins at the **same X** on every row; a
commit with multiple refs shows them **stacked vertically** and its row is
**taller**; the commit dot stays aligned with the first line; lane connectors
remain continuous through tall rows.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/GraphPane.qml
git commit -m "feat(ui): fixed ref column with stacked chips aligns graph text"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: `spec/product/2026-07-21-history-graph-commit-medallion-design.md`
>   moved to reflect shipped behaviour; `spec/product/product.md` +
>   `spec/product/2026-06-26-history-graph-tab-design.md` note the fixed-column
>   graph and the medallion.
> - Code: `GitRepo::commitDetail`, `RepoController::refreshCommitDetail`,
>   `RepoViewModel` detail props, `CommitDetail.qml` medallion, `GraphPane.qml`
>   columns, `GraphColumn` top-anchored dot.
