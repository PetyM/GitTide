# Plan 14b — UI: merge action, merge-in-progress banner, inline conflict resolution

> **For agentic workers:** implement this plan task-by-task, **test-first**. Each
> task's steps use checkbox (`- [ ]`) syntax; tick them as you go. REQUIRED
> SUB-SKILL: `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans`.

| | |
|--|--|
| **Date** | 2026-06-22 |
| **Status** | `planned` |
| **Spec** | [`spec/product` §Merge](../spec/product/product.md#merge) · [`spec/engineering` §Merge & conflict resolution](../spec/engineering/engineering.md#merge--conflict-resolution) · [`spec/design` (merge banner, inline conflict, `state.incoming`)](../spec/design/design.md) · [D29](../decisions.md) · [D30](../decisions.md) · [D31](../decisions.md) |
| **Depends on** | **Plan 14a** (core merge engine), Plan 9b (GitHub-Desktop UI: WorkingPane/ChangesPane/DiffView), Plan 8 (BranchBar/BranchDropdown), Plan 13 (DiffLinesModel block rows) |

**Goal:** Surface the core merge engine in the QML client: start a merge from the
branch dropdown and the History context menu; a **merge-in-progress banner**
(Abort / Commit merge / Deinit submodules & retry) driven purely by `MergeState`
read from disk (D30); and **inline VS Code-style conflict resolution** (D29) in
the shared diff panel — `Current (ours)` / `Incoming (theirs)` bands with
per-region Accept Current / Incoming / Both, and free editing — with "resolved"
derived from "no markers remain".

**Architecture:** `AsyncRepo` wraps each new core method as a `QCoro::Task`.
`RepoController` adds merge slots + a `mergeStateChanged` signal and folds
`mergeState()` into the status refresh. `RepoController` also owns the **auto-stash
orchestration** and the **reactive submodule deinit-and-retry** (D31) — the only
new business logic in the UI layer, kept out of core. `RepoViewModel` republishes
`MergeState` as bindable properties and exposes `Q_INVOKABLE` merge actions to QML.
`ChangedFilesModel` learns the `C` letter; `DiffLinesModel` learns to parse
conflict regions from the marker-bearing file content and to rewrite a region on
Accept. New QML: `MergeBanner.qml`; `DiffView.qml` gains inline conflict rendering;
`BranchDropdown.qml` + the History context menu gain "Merge into <current>".

**Tech stack:** Qt 6 Quick/QML, QCoro, Qt Test (headless, `QT_QPA_PLATFORM=offscreen`).

## Global constraints

- Invariants ([`engineering`](../spec/engineering/engineering.md#cross-cutting-invariants)):
  Qt stays at the ViewModel boundary; controllers translate `std`↔Qt; **no
  exceptions out of slots** (errors arrive as `operationFailed`); colour only from
  theme tokens — no hex literals in QML (use `theme.stateAdded`, `theme.stateIncoming`,
  `theme.stateConflict`).
- **D30 — merge state is rendered only from `MergeState` (disk truth).** No QML or
  ViewModel boolean tracks "are we merging?" independently; every render path reads
  the published `mergeInProgress` / `conflictedCount` from the last `mergeState()`.
- New `ui/` sources → the `gittide_ui` list in `ui/CMakeLists.txt`; new QML →
  `ui/qml/qml.qrc`. New UI tests need **two** edits (the `gittide_ui_test_sources`
  list in `tests/CMakeLists.txt` **and** an `#include` + `QTest::qExec` block in
  `tests/ui/main.cpp`) — see [testing](../spec/engineering/testing.md#ui-tests-the-include-runner-read-this-before-adding-one).
- Coroutine slots take args **by value** (survive `co_await`), matching the
  existing `RepoController` style.
- Keep green: all existing UI + core tests; existing object names
  (`branchBar`, `branchChip`, `changedFilesList`, `mainTabs`, …) keep working.
- Commit style: `feat(ui): …` / `test(ui): …`; end with the
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` trailer.

---

## Task 1: `AsyncRepo` — wrap the new core methods

Add `QCoro::Task` wrappers for every Plan 14a primitive, following the existing
per-call pattern (worker lambda under the per-repo mutex via `QtConcurrent::run`).

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp` (declare wrappers; include `merge.hpp`)
- Modify: `ui/src/asyncrepo.cpp` (implement, mirroring existing wrappers)
- Test: `tests/ui/test_async_merge.cpp` (new) + the two runner edits

**Interfaces — Produces:**
```cpp
// asyncrepo.hpp (add #include "gittide/merge.hpp")
QCoro::Task<gittide::Expected<gittide::MergeOutcome>> mergeBranch(QString name);
QCoro::Task<gittide::Expected<gittide::MergeState>>   mergeState();
QCoro::Task<gittide::Expected<std::string>>           commitMerge(gittide::CommitRequest req);
QCoro::Task<gittide::Expected<void>>                  abortMerge();
QCoro::Task<gittide::Expected<bool>>                  stashSave(QString message);
QCoro::Task<gittide::Expected<void>>                  stashPop();
QCoro::Task<gittide::Expected<void>>                  deinitSubmodule(std::filesystem::path path);
QCoro::Task<gittide::Expected<void>>                  reinitSubmodule(std::filesystem::path path);
```

- [ ] **Step 1: Write the failing test** — drive a real conflict through the async
  API and assert `mergeState()` reports it.

```cpp
// tests/ui/test_async_merge.cpp
#include <QtTest>
#include <qcoro/qcorotask.h>
#include "gittide/ui/asyncrepo.hpp"
#include "support/temprepo.hpp"

using namespace gittide;

class TestAsyncMerge : public QObject {
    Q_OBJECT
private slots:
    void merge_conflict_reports_state() {
        gittide::test::TempRepo tmp;
        tmp.setIdentity("Test", "test@example.com");
        tmp.writeFile("a.txt", "base\n"); tmp.commitAll("base");
        auto sync = GitRepo::open(tmp.path());
        QVERIFY(sync.has_value());
        QVERIFY(sync->createBranch("feature", "").has_value());
        QVERIFY(sync->checkoutBranch("feature").has_value());
        tmp.writeFile("a.txt", "feature\n"); tmp.commitAll("feat");
        QVERIFY(sync->checkoutBranch("main").has_value()); // or master
        tmp.writeFile("a.txt", "main\n"); tmp.commitAll("main");

        auto async = ui::AsyncRepo::open(tmp.path());
        QVERIFY(async.has_value());
        auto out = QCoro::waitFor(async->mergeBranch("feature"));
        QVERIFY(out.has_value());
        QVERIFY(out->conflicted);
        auto ms = QCoro::waitFor(async->mergeState());
        QVERIFY(ms.has_value());
        QVERIFY(ms->inProgress);
        QCOMPARE(int(ms->conflictedPaths.size()), 1);
    }
};
#include "test_async_merge.moc"
```

- [ ] **Step 2: Run — expect FAIL** (wrappers missing).
  Run: `ctest --test-dir build -R 'ui' --output-on-failure` (after the two runner edits)

- [ ] **Step 3: Implement** each wrapper. Representative (the rest follow identically
  — copy the body shape of the existing `checkoutBranch`/`commit` wrappers in
  `asyncrepo.cpp`, swapping the inner `GitRepo` call):

```cpp
QCoro::Task<gittide::Expected<gittide::MergeOutcome>> AsyncRepo::mergeBranch(QString name)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run([impl, name = name.toStdString()]() {
        std::scoped_lock lock(impl->mutex);
        return impl->repo.mergeBranch(name);
    });
}
```

  Apply the same shape to `mergeState`, `commitMerge`, `abortMerge`, `stashSave`,
  `stashPop`, `deinitSubmodule`, `reinitSubmodule`. (`MergeOutcome`/`MergeState`
  are already `std`-only structs; no Qt conversion in the worker.)

- [ ] **Step 4: Run — expect PASS.** Register `gittide::MergeState` /
  `gittide::MergeOutcome` as metatypes if a queued signal will carry them (Task 2);
  for now the direct return is fine.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): AsyncRepo wrappers for the core merge engine"`

---

## Task 2: `RepoController` — merge slots, refresh fold-in, signals

Add the merge slots and the auto-stash + reactive-submodule orchestration (D31),
and extend the status refresh to also publish `MergeState` (D30). The controller is
where the multi-call stash lifecycle lives.

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp` (slots + signal + members)
- Modify: `ui/src/repocontroller.cpp` (implement)
- Modify: `ui/include/gittide/ui/metatypes.hpp` (register `gittide::MergeState`)
- Test: `tests/ui/test_repocontroller_merge.cpp` (new) + the two runner edits

**Interfaces — Produces:**
```cpp
// repocontroller.hpp — public slots
QCoro::Task<void> merge(QString name);              // both entry points call this
QCoro::Task<void> commitMerge(gittide::CommitRequest req);
QCoro::Task<void> abortMerge();
QCoro::Task<void> retryMergeDeinitSubmodules(QString name);

// repocontroller.hpp — signals
void mergeStateChanged(gittide::MergeState state);
void mergeFinished(QString headOid);                // FF / clean / commit success

// repocontroller.hpp — private members
bool                         m_pendingStashPop = false;
std::vector<std::filesystem::path> m_pendingSubmoduleReinit;
```
**Consumes:** all of Task 1's `AsyncRepo` wrappers; the existing `refreshStatus()`,
`refreshHistory()`, `refreshBranches()`, `refreshSyncStatus()` cascade.

- [ ] **Step 1: Write the failing test** — a dirty-tree FF merge stashes then pops,
  and `mergeStateChanged` is emitted on refresh.

```cpp
// tests/ui/test_repocontroller_merge.cpp  (skeleton — fill bodies)
#include <QtTest>
#include <qcoro/qcorotask.h>
#include "gittide/ui/repocontroller.hpp"
#include "support/temprepo.hpp"
using namespace gittide;

class TestRepoControllerMerge : public QObject {
    Q_OBJECT
private slots:
    void conflict_merge_emits_mergeState_with_conflicts() {
        // build a conflicting repo (as in 14a), open the controller, qRegister the
        // mergeStateChanged signal spy, call merge("feature"), spin the event loop,
        // assert the last MergeState.inProgress && conflictedPaths.size()==1.
    }
    void clean_ff_merge_emits_mergeFinished_not_inProgress() {
        // FF case: after merge(), mergeFinished fires and mergeState.inProgress==false.
    }
};
#include "test_repocontroller_merge.moc"
```

- [ ] **Step 2: Run — expect FAIL** (slots/signals missing).

- [ ] **Step 3: Implement.** The orchestration (D31):

```cpp
QCoro::Task<void> RepoController::merge(QString name)
{
    if (!m_repo) co_return;

    // Auto-stash a dirty tree (D31). Remember whether we owe a pop.
    auto saved = co_await m_repo->stashSave("gittide: auto-stash before merge");
    if (!saved) { emit operationFailed(QString::fromStdString(saved.error().message)); co_return; }
    m_pendingStashPop = *saved;

    auto out = co_await m_repo->mergeBranch(name);
    if (!out)
    {
        emit operationFailed(QString::fromStdString(out.error().message));
        co_await refreshStatus();          // still report true (disk) state, D30
        co_return;
    }

    using gittide::MergeAnalysis;
    if (out->analysis == MergeAnalysis::UpToDate)
    {
        co_await popPendingStash();
        emit operationFailed(tr("Already up to date."));   // informational, non-fatal
    }
    else if (out->analysis == MergeAnalysis::FastForward)
    {
        co_await popPendingStash();
        emit mergeFinished(QString::fromStdString(out->newOid));
    }
    else if (!out->conflicted)             // clean normal merge → finish immediately
    {
        const std::string msg = "Merge branch '" + name.toStdString() + "' into "
                              + currentBranchName();   // helper: HEAD shorthand or "HEAD"
        auto oid = co_await m_repo->commitMerge(gittide::CommitRequest{msg});
        if (!oid) { emit operationFailed(QString::fromStdString(oid.error().message)); }
        else { co_await popPendingStash(); emit mergeFinished(QString::fromStdString(*oid)); }
    }
    // else: conflicted → leave mid-merge; the pending pop waits for commitMerge.

    co_await refreshAfterMerge();          // status(+mergeState) + history + branches + sync
}

QCoro::Task<void> RepoController::popPendingStash()
{
    if (!m_pendingStashPop) co_return;
    m_pendingStashPop = false;
    auto r = co_await m_repo->stashPop();
    if (!r) emit operationFailed(QString::fromStdString(r.error().message)); // stash preserved
}

QCoro::Task<void> RepoController::commitMerge(gittide::CommitRequest req)
{
    if (!m_repo) co_return;
    auto oid = co_await m_repo->commitMerge(req);
    if (!oid) { emit operationFailed(QString::fromStdString(oid.error().message)); co_await refreshAfterMerge(); co_return; }
    co_await popPendingStash();            // deferred pop now safe
    emit mergeFinished(QString::fromStdString(*oid));
    co_await refreshAfterMerge();
}

QCoro::Task<void> RepoController::abortMerge()
{
    if (!m_repo) co_return;
    auto r = co_await m_repo->abortMerge();
    if (!r) { emit operationFailed(QString::fromStdString(r.error().message)); }
    co_await popPendingStash();            // restore the user's pre-merge work
    co_await reinitPendingSubmodules();    // if we had deinited any (Task 7)
    co_await refreshAfterMerge();
}

QCoro::Task<void> RepoController::retryMergeDeinitSubmodules(QString name)
{
    if (!m_repo) co_return;
    auto ms = co_await m_repo->mergeState();
    if (!ms) { emit operationFailed(QString::fromStdString(ms.error().message)); co_return; }
    // 1. abort the conflicted merge
    if (auto r = co_await m_repo->abortMerge(); !r) { emit operationFailed(QString::fromStdString(r.error().message)); co_return; }
    // 2. deinit the conflicted submodules, remembering them for re-init
    m_pendingSubmoduleReinit = ms->conflictedSubmodules;
    for (const auto& p : ms->conflictedSubmodules)
        if (auto r = co_await m_repo->deinitSubmodule(p); !r)
            { emit operationFailed(QString::fromStdString(r.error().message)); }
    // 3. re-run the merge; now the gitlinks merge as plain pointers
    co_await merge(name);
    // re-init happens on the next commitMerge/abort via reinitPendingSubmodules()
}
```

  Add private helpers `refreshAfterMerge()` (calls `refreshStatus()` — which now
  also fetches `mergeState()` and emits `mergeStateChanged` — plus history /
  branches / sync), `reinitPendingSubmodules()` (drains `m_pendingSubmoduleReinit`,
  calling `reinitSubmodule` on each, then clears it — also called at the end of
  `commitMerge`), and `currentBranchName()` (HEAD shorthand). Extend
  `refreshStatus()` to `co_await m_repo->mergeState()` and `emit
  mergeStateChanged(*ms)` after emitting `statusChanged`.

- [ ] **Step 4: Run — expect PASS.** Register `gittide::MergeState` in
  `metatypes.hpp` (`qRegisterMetaType<gittide::MergeState>()`) so the queued signal
  marshals it.

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): RepoController merge slots + auto-stash/submodule orchestration"`

---

## Task 3: `RepoViewModel` — publish MergeState + invokable actions

Republish `MergeState` as bindable QML properties and expose the merge actions QML
calls. Wire `commitFinished`/`mergeStateChanged` into the existing refresh path.

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` (properties, invokables, members)
- Modify: `ui/src/repoviewmodel.cpp` (implement)
- Test: `tests/ui/test_repoviewmodel_merge.cpp` (new) + the two runner edits

**Interfaces — Produces:**
```cpp
// repoviewmodel.hpp — properties
Q_PROPERTY(bool mergeInProgress READ mergeInProgress NOTIFY mergeStateChanged)
Q_PROPERTY(QString mergedRef READ mergedRef NOTIFY mergeStateChanged)
Q_PROPERTY(int conflictedCount READ conflictedCount NOTIFY mergeStateChanged)
Q_PROPERTY(bool hasSubmoduleConflicts READ hasSubmoduleConflicts NOTIFY mergeStateChanged)
// getters: m_merge.inProgress / fromStdString(m_merge.mergedRef) /
//          int(m_merge.conflictedPaths.size()) / !m_merge.conflictedSubmodules.empty()

// repoviewmodel.hpp — invokables
Q_INVOKABLE void startMerge(const QString& name);          // -> controller.merge
Q_INVOKABLE void commitMerge(const QString& message);      // -> controller.commitMerge
Q_INVOKABLE void abortMerge();                             // -> controller.abortMerge
Q_INVOKABLE void retryMergeDeinitSubmodules();            // uses m_mergeStartName

// repoviewmodel.hpp — signal + members
signals: void mergeStateChanged();
private: gittide::MergeState m_merge; QString m_mergeStartName;
```
**Consumes:** Task 2's controller slots + `mergeStateChanged(MergeState)` signal.

- [ ] **Step 1: Write the failing test** — after a conflicting merge, the VM
  reports `mergeInProgress == true` and `conflictedCount == 1`.

```cpp
// tests/ui/test_repoviewmodel_merge.cpp (skeleton)
//  - build conflicting repo, vm.open(path), spy on mergeStateChanged
//  - vm.startMerge("feature"); spin loop
//  - QVERIFY(vm.property("mergeInProgress").toBool());
//  - QCOMPARE(vm.property("conflictedCount").toInt(), 1);
```

- [ ] **Step 2: Run — expect FAIL** (properties missing).

- [ ] **Step 3: Implement.** Connect in the VM constructor:

```cpp
connect(m_controller, &RepoController::mergeStateChanged, this,
        [this](const gittide::MergeState& s) { m_merge = s; emit mergeStateChanged(); });
connect(m_controller, &RepoController::mergeFinished, this,
        [this](const QString&) { /* refresh is already driven by the controller cascade */ });
```
```cpp
void RepoViewModel::startMerge(const QString& name)
{
    m_mergeStartName = name;
    m_controller->merge(name);     // fire-and-forget QCoro task (matches existing calls)
}
void RepoViewModel::commitMerge(const QString& message)
{
    m_controller->commitMerge(gittide::CommitRequest{message.toStdString()});
}
void RepoViewModel::abortMerge() { m_controller->abortMerge(); }
void RepoViewModel::retryMergeDeinitSubmodules()
{
    m_controller->retryMergeDeinitSubmodules(m_mergeStartName);
}
```

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): RepoViewModel publishes MergeState + merge actions"`

---

## Task 4: `ChangedFilesModel` — the `C` (conflict) letter

A conflicted file (`StatusFlag::Conflicted`) must read its letter as `C` and kind
as `conflict`, and sort to the top of the changed-files list.

**Files:**
- Modify: `ui/src/changedfilesmodel.cpp` (`letterForFlags` / `kindForFlags`, sort)
- Test: `tests/ui/test_changedfilesmodel.cpp` (append, or new merge test)

**Interfaces — Produces:** no signature change; behaviour: `letterForFlags(flags
with Conflicted) == "C"`, `kindForFlags(...) == "conflict"`.

- [ ] **Step 1: Write the failing test.**

```cpp
void conflicted_file_reads_as_C() {
    using gittide::ui::ChangedFilesModel;
    using gittide::StatusFlag;
    QCOMPARE(ChangedFilesModel::letterForFlags(StatusFlag::Conflicted), QStringLiteral("C"));
    QCOMPARE(ChangedFilesModel::kindForFlags(StatusFlag::Conflicted), QStringLiteral("conflict"));
}
```

- [ ] **Step 2: Run — expect FAIL.**

- [ ] **Step 3: Implement.** In `letterForFlags`, check `Conflicted` **first**
  (it dominates the working/index bits):

```cpp
QString ChangedFilesModel::letterForFlags(gittide::StatusFlag flags)
{
    if (gittide::hasFlag(flags, gittide::StatusFlag::Conflicted)) return QStringLiteral("C");
    // … existing A / M / D / U / ? cases …
}
QString ChangedFilesModel::kindForFlags(gittide::StatusFlag flags)
{
    if (gittide::hasFlag(flags, gittide::StatusFlag::Conflicted)) return QStringLiteral("conflict");
    // … existing cases …
}
```
  In `setFiles`, give conflicted rows priority in the sort (stable_sort with
  conflicted-first comparator) so they head the list.

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): ChangedFilesModel shows the C letter for conflicts"`

---

## Task 5: `DiffLinesModel` — parse conflict regions + accept a side

Teach the diff model to recognise `<<<<<<< / ||||||| / ======= / >>>>>>>` regions
in a conflicted file's content and expose, per region, the three Accept actions
that rewrite the underlying file. This is the heart of D29.

**Files:**
- Modify: `ui/include/gittide/ui/difflinesmodel.hpp` (conflict rows + invokables)
- Modify: `ui/src/difflinesmodel.cpp` (parse + accept)
- Test: `tests/ui/test_difflinesmodel_conflict.cpp` (new) + the two runner edits

**Interfaces — Produces:**
```cpp
// difflinesmodel.hpp — new Row kinds: "conflict-start" | "conflict-sep" |
//   "conflict-end" | "ours" | "theirs" carried in Row::kind; a region index on each.
// New role:
ConflictRegionRole, // int: region index on conflict rows, -1 otherwise

// Load a conflicted file's raw content (with markers) for inline resolution.
// Parses regions; ours/theirs lines get the "ours"/"theirs" kind for tinting.
void setConflictContent(const QString& fileText);

// Rewrite region `region` in the file, keeping the chosen side(s), and reparse.
// Returns the new full file text (the caller writes it to disk).
Q_INVOKABLE QString acceptCurrent(int region);   // keep ours
Q_INVOKABLE QString acceptIncoming(int region);  // keep theirs
Q_INVOKABLE QString acceptBoth(int region);      // keep ours then theirs

// True when no conflict markers remain (file resolved).
Q_INVOKABLE bool isResolved() const;
```

- [ ] **Step 1: Write the failing test** (parse + each accept).

```cpp
// tests/ui/test_difflinesmodel_conflict.cpp
#include <QtTest>
#include "gittide/ui/difflinesmodel.hpp"
using gittide::ui::DiffLinesModel;

static const QString kConflict =
    "line1\n"
    "<<<<<<< HEAD\n"
    "ours\n"
    "=======\n"
    "theirs\n"
    ">>>>>>> feature\n"
    "line2\n";

class TestDiffConflict : public QObject {
    Q_OBJECT
private slots:
    void parses_one_region() {
        DiffLinesModel m;
        m.setConflictContent(kConflict);
        QVERIFY(!m.isResolved());                 // markers present
    }
    void accept_current_keeps_ours() {
        DiffLinesModel m; m.setConflictContent(kConflict);
        const QString out = m.acceptCurrent(0);
        QCOMPARE(out, QStringLiteral("line1\nours\nline2\n"));
        m.setConflictContent(out);
        QVERIFY(m.isResolved());
    }
    void accept_incoming_keeps_theirs() {
        DiffLinesModel m; m.setConflictContent(kConflict);
        QCOMPARE(m.acceptIncoming(0), QStringLiteral("line1\ntheirs\nline2\n"));
    }
    void accept_both_keeps_both() {
        DiffLinesModel m; m.setConflictContent(kConflict);
        QCOMPARE(m.acceptBoth(0), QStringLiteral("line1\nours\ntheirs\nline2\n"));
    }
};
#include "test_difflinesmodel_conflict.moc"
```

- [ ] **Step 2: Run — expect FAIL** (methods missing).

- [ ] **Step 3: Implement.** Parse line-by-line into regions; rewrite by region.
  Keep it a pure string transform so it is unit-testable without a repo:

```cpp
namespace {
struct Region { int startLine; int sepLine; int endLine;        // indices into `lines`
                std::vector<QString> ours, theirs; };
// Split into lines preserving a trailing newline shape: we re-join with '\n' and
// re-append a trailing '\n' iff the original ended with one.
}

void DiffLinesModel::setConflictContent(const QString& fileText)
{
    beginResetModel();
    m_rows.clear();
    m_conflictText = fileText;          // keep the source for accept*()
    const QStringList lines = fileText.split('\n');
    int region = 0;
    bool inOurs = false, inTheirs = false;
    for (const QString& ln : lines)
    {
        if (ln.startsWith("<<<<<<<")) { inOurs = true; inTheirs = false;
            m_rows.push_back(Row{"conflict-start", -1,-1, ln, false,false,-1,-1}); /* region tag */ continue; }
        if (ln.startsWith("=======") && (inOurs || inTheirs)) { inOurs = false; inTheirs = true;
            m_rows.push_back(Row{"conflict-sep", -1,-1, ln}); continue; }
        if (ln.startsWith(">>>>>>>")) { inOurs = inTheirs = false;
            m_rows.push_back(Row{"conflict-end", -1,-1, ln}); ++region; continue; }
        Row r; r.text = ln;
        r.kind = inOurs ? "ours" : inTheirs ? "theirs" : "context";
        m_rows.push_back(r);
    }
    endResetModel();
}

bool DiffLinesModel::isResolved() const
{
    return !m_conflictText.contains("<<<<<<<")
        && !m_conflictText.contains(">>>>>>>")
        && !m_conflictText.contains("\n=======");
}

// Rewrite region N keeping `which` ∈ {Ours, Theirs, Both}. Returns new file text.
static QString rewriteRegion(const QString& text, int target, int which)
{
    const bool trailingNl = text.endsWith('\n');
    QStringList in = text.split('\n');
    if (trailingNl && !in.isEmpty() && in.last().isEmpty()) in.removeLast();
    QStringList out; int region = 0;
    enum { Outside, Ours, Theirs } state = Outside;
    QStringList ours, theirs;
    auto flush = [&](){
        if (which == 0) out += ours;            // Current (ours)
        else if (which == 1) out += theirs;     // Incoming (theirs)
        else { out += ours; out += theirs; }    // Both
        ours.clear(); theirs.clear();
    };
    for (const QString& ln : in) {
        if (ln.startsWith("<<<<<<<")) { state = Ours; continue; }
        if (ln.startsWith("=======") && state != Outside) { state = Theirs; continue; }
        if (ln.startsWith(">>>>>>>")) {
            if (region == target) flush();
            else { /* untouched region: re-emit verbatim */
                out += "<<<<<<<"; out += ours; out += "======="; out += theirs; out += ">>>>>>>";
                ours.clear(); theirs.clear(); }
            state = Outside; ++region; continue;
        }
        if (state == Ours) ours += ln;
        else if (state == Theirs) theirs += ln;
        else out += ln;
    }
    QString joined = out.join('\n');
    if (trailingNl) joined += '\n';
    return joined;
}

QString DiffLinesModel::acceptCurrent(int region)  { return rewriteRegion(m_conflictText, region, 0); }
QString DiffLinesModel::acceptIncoming(int region) { return rewriteRegion(m_conflictText, region, 1); }
QString DiffLinesModel::acceptBoth(int region)     { return rewriteRegion(m_conflictText, region, 2); }
```

  Add `QString m_conflictText;` member, the `ConflictRegionRole` to the enum +
  `roleNames()`, and a per-row `int conflictRegion = -1` set during parse so QML can
  group the Accept buttons by region. (For the untouched-region re-emit, restore the
  original marker label text if you preserve it; the simplified markers above are
  sufficient for the single-region tests — extend to multi-region by keeping the
  original `<<<<<<< HEAD` / `>>>>>>> name` lines if a test needs them.)

- [ ] **Step 4: Run — expect PASS** (all four cases).

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): DiffLinesModel parses conflict regions + accept ours/theirs/both"`

---

## Task 6: `MergeBanner.qml` + wire into the working pane

The merge-in-progress banner above the Changes list, driven only by `MergeState`
(D30). Actions: Abort (always), Commit merge (enabled at zero conflicts), Deinit
submodules & retry (only when submodule conflicts exist).

**Files:**
- Create: `ui/qml/MergeBanner.qml`
- Modify: `ui/qml/qml.qrc` (register it)
- Modify: `ui/qml/WorkingPane.qml` (or `ChangesPane.qml` — wherever the Changes list
  header lives) to host the banner above the list when `repo.mergeInProgress`
- Modify: `ui/src/qmltheme.{hpp,cpp}` (expose `stateIncoming` token property)
- Test: `tests/ui/test_qml_merge_banner.cpp` (load headless, assert visibility +
  object names) + the two runner edits

**Interfaces — Produces:** QML object names `mergeBanner`, `mergeAbortButton`,
`mergeCommitButton`, `mergeRetryButton` for tests.

- [ ] **Step 1: Write the failing test** — with `mergeInProgress` true the banner
  is visible; the retry button shows only with submodule conflicts.

```cpp
// tests/ui/test_qml_merge_banner.cpp — load Main.qml (or a small harness that
// instantiates MergeBanner with a stub repo object exposing mergeInProgress /
// conflictedCount / hasSubmoduleConflicts), then:
//   findChild("mergeBanner")->property("visible") == true when mergeInProgress
//   findChild("mergeRetryButton")->property("visible") == hasSubmoduleConflicts
//   findChild("mergeCommitButton")->property("enabled") == (conflictedCount == 0)
```

- [ ] **Step 2: Run — expect FAIL** (component missing).

- [ ] **Step 3: Implement** `MergeBanner.qml` (tokens only — D18):

```qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    objectName: "mergeBanner"
    property var repo                       // the RepoViewModel
    visible: repo && repo.mergeInProgress
    height: visible ? 44 : 0
    color: theme.surfaceRaised
    // left accent uses the conflict token (orange) — paired with the glyph + text
    Rectangle { width: 3; height: parent.height; color: theme.stateConflict }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12; anchors.rightMargin: 12
        spacing: 12
        Label {
            text: "⚠"; color: theme.stateConflict; font.pixelSize: 16
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            color: theme.textPrimary
            text: (repo ? ("Merging " + (repo.mergedRef.length ? repo.mergedRef : "branch")
                   + " — " + repo.conflictedCount + " conflicted file"
                   + (repo.conflictedCount === 1 ? "" : "s")) : "")
        }
        Button {
            objectName: "mergeRetryButton"
            visible: repo && repo.hasSubmoduleConflicts
            text: "Deinit submodules & retry"
            onClicked: repo.retryMergeDeinitSubmodules()
        }
        Button {
            objectName: "mergeAbortButton"
            text: "Abort merge"
            onClicked: repo.abortMerge()
        }
        Button {
            objectName: "mergeCommitButton"
            text: "Commit merge"
            enabled: repo && repo.conflictedCount === 0
            onClicked: repo.commitMerge("Merge branch '" + repo.mergedRef
                       + "' into " + repo.currentBranch)
        }
    }
}
```

  Add a `stateIncoming` bindable property to `QmlTheme` (mirroring `stateConflict`),
  reading the new `state.incoming` token from `Theme` (already added to
  `theme.cpp` in design — if not, add `#388BFD` to both `darkTheme()`/`lightTheme()`
  as `t.stateIncoming`). Host the banner in `WorkingPane.qml` directly above the
  `changedFilesList`.

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): merge-in-progress banner (abort/commit/retry)"`

---

## Task 7: `DiffView.qml` — inline conflict rendering + accept actions

When the active file is conflicted, render its regions inline with tinted
ours/theirs bands and per-region Accept actions; on accept, write the rewritten
text to the file and refresh. Reuses `DiffLinesModel`'s conflict rows from Task 5.

**Files:**
- Modify: `ui/qml/DiffView.qml` (delegate cases for `ours`/`theirs`/`conflict-*`,
  a per-region action header)
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` + `.cpp` (an
  `Q_INVOKABLE void acceptConflict(int region, int which)` that calls the model,
  writes the file via the controller's workdir, and re-selects the file so the diff
  + `MergeState` refresh)
- Test: `tests/ui/test_repoviewmodel_merge.cpp` (append: accept resolves the file)

**Interfaces — Produces:**
```cpp
// repoviewmodel.hpp
Q_INVOKABLE void acceptConflict(int region, int which); // which: 0 ours,1 theirs,2 both
```

- [ ] **Step 1: Write the failing test** — after `acceptConflict(0, 0)` on a
  single-region conflict, the file has no markers and (after refresh) the file is
  no longer conflicted.

```cpp
void accept_resolves_conflict() {
    // conflicting repo, vm.open, vm.startMerge("feature"), spin
    // vm.selectFile("a.txt");  // loads conflict content into diffLines
    // vm.acceptConflict(0, 0); // keep ours
    // read a.txt on disk -> no "<<<<<<<"; vm.conflictedCount eventually 0 after refresh
}
```

- [ ] **Step 2: Run — expect FAIL.**

- [ ] **Step 3: Implement.** In `selectFile`, when the file is conflicted (the row's
  kind is `conflict`), load raw file content into the diff model via
  `setConflictContent(readFile(path))` instead of the normal `diff()`; otherwise
  the existing path. Implement `acceptConflict`:

```cpp
void RepoViewModel::acceptConflict(int region, int which)
{
    QString out;
    switch (which) {
        case 0: out = m_diff->acceptCurrent(region);  break;
        case 1: out = m_diff->acceptIncoming(region); break;
        default: out = m_diff->acceptBoth(region);    break;
    }
    // write `out` to <workdir>/<activeFile>, then re-select to refresh the view.
    m_controller->writeWorkingFile(m_activeFile, out);   // small controller helper
    selectFile(m_activeFile);                            // re-loads content / diff
    m_controller->refreshStatus();                       // re-derives MergeState (D30)
}
```

  Add `RepoController::writeWorkingFile(QString relPath, QString content)` (writes
  UTF-8 to `workdir()/relPath`; path via the existing repo workdir — keep it in the
  controller so the VM never touches the filesystem directly). In `DiffView.qml`,
  add delegate branches: `ours` rows get a `theme.stateAdded`-tinted background,
  `theirs` rows `theme.stateIncoming`; a `conflict-start` row renders the per-region
  action header with three ghost buttons calling
  `repo.acceptConflict(model.conflictRegion, 0|1|2)`. The text body stays editable
  via the existing edit affordance (manual resolution path).

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): inline conflict rendering + accept actions in DiffView"`

---

## Task 8: Entry points — branch dropdown + History context menu

Add "Merge into \<current\>" to both the branch dropdown (per local branch) and the
History commit/branch-tip context menu. Both call `repo.startMerge(name)`.

**Files:**
- Modify: `ui/qml/BranchDropdown.qml` (a "Merge into <current>" action per local
  branch row, or a dedicated sentinel that merges the highlighted branch)
- Modify: `ui/qml/HistoryPane.qml` (context menu item "Merge into <current>" → the
  branch tip at that commit; for a plain commit, merge its containing branch name)
- Test: `tests/ui/test_qml_merge_entrypoints.cpp` (load headless, assert the menu
  item exists with object name `mergeIntoItem` and invokes `startMerge`) + runner edits

**Interfaces — Consumes:** `RepoViewModel::startMerge(QString)`.

- [ ] **Step 1: Write the failing test** — the dropdown exposes a `mergeIntoItem`
  for a non-current local branch; clicking it calls `startMerge` with that name
  (spy on a stub repo).

- [ ] **Step 2: Run — expect FAIL.**

- [ ] **Step 3: Implement.** In `BranchDropdown.qml`, for each **local** branch row
  that is not the current branch, add a trailing action (or a context sub-item)
  `objectName: "mergeIntoItem"` with `text: "Merge into " + repo.currentBranch`,
  `onTriggered: repo.startMerge(model.name)`. In `HistoryPane.qml`'s existing commit
  context menu (the one already hosting "New branch from here" / "Checkout"), add
  `MenuItem { objectName: "mergeIntoItem"; text: "Merge into " + repo.currentBranch;
  onTriggered: repo.startMerge(branchNameForRow) }` where `branchNameForRow` is the
  branch label already available on the graph row (skip the item when the row has no
  branch tip).

- [ ] **Step 4: Run — expect PASS.**

- [ ] **Step 5: Commit.**
  `git commit -am "feat(ui): merge entry points in branch dropdown + history menu"`

---

## Task 9: Close-out

- [ ] **Step 1:** Build + full suite (core + UI headless) green; no new warnings.
  Run: `cmake --build build --parallel && ctest --test-dir build --output-on-failure`
- [ ] **Step 2:** Manual smoke (per [run](../../README.md)): create a conflict, merge
  from the dropdown, resolve inline (Accept Current / Incoming / Both + a manual
  edit), commit; repeat and Abort; verify the banner appears/clears and Abort is
  reachable after an app restart mid-merge (D30). If a submodule fixture is handy,
  exercise deinit-and-retry.
- [ ] **Step 3:** Confirm spec sections (product §Merge, engineering §Merge &
  conflict resolution, design merge components) match the shipped UI; fix drift.
- [ ] **Step 4:** Tick this plan's boxes, fill **Outcome**, set `Status` to `done`
  here and in [`plans/index.md`](index.md); flip the merge wish row to `shipped` and
  move `docs/wishlist/merge.md` into `docs/wishlist/shipped/` if that is the
  convention (check `docs/wishlist/shipped/`).
- [ ] **Step 5: Commit.**
  `git commit -am "docs: close Plan 14b — UI merge + inline conflict resolution"`

---

## Outcome

> Fill in when the plan reaches `done`. Expected: AsyncRepo merge wrappers;
> RepoController merge slots + auto-stash/submodule orchestration + `mergeStateChanged`;
> RepoViewModel merge properties/actions; `ChangedFilesModel` C letter;
> `DiffLinesModel` conflict parse + accept; `MergeBanner.qml`; inline conflict
> rendering in `DiffView.qml`; entry points in `BranchDropdown.qml` + `HistoryPane.qml`.
> Spec: product §Merge, engineering §Merge & conflict resolution, design merge
> components. Code: `ui/...` as above; tests `tests/ui/test_*merge*.cpp`,
> `tests/ui/test_difflinesmodel_conflict.cpp`.
