# Plan 18 — History range-diff & reword-tip

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.
> REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans.

| | |
|--|--|
| **Date** | 2026-06-24 |
| **Status** | `planned` |
| **Spec** | [`spec/product/history-editing.md`](../spec/product/history-editing.md); touches [`spec/product/context-menus.md`](../spec/product/context-menus.md) |
| **Depends on** | Plan 16 (context menus), Plan 9a (commit-selection history diff) |

**Goal:** Let a user see the combined diff of a contiguous range of commits and
reword the message of the `HEAD` commit, both from the History tab — the
rebase-free slices of the history-editing/rebase wishes.

**Architecture:** Three read/rewrite ops land on `core/`'s `GitRepo`
(`rangeFiles`, `rangeDiff`, `rewordHead`, plus `commitMessage` for the dialog),
each `Expected<T>`, reusing the existing `commitTrees` helper and `git_commit_amend`.
`AsyncRepo` wraps them on the thread pool; `RepoController` exposes async slots +
signals; `RepoViewModel` routes History selection into the **existing**
`commitFiles`/`commitDiff` models (range diff is per-file, mirroring single-commit
detail) and owns reword. QML adds multi-select gestures, a `RewordDialog`, and a
header/hint line on `CommitDetail`. No interactive-rebase engine — squash, reorder,
and reword-of-older stay deferred (spec §7).

**Tech stack:** C++23, libgit2 (`git_commit_amend`, `git_diff_tree_to_tree`),
Catch2 + `TempRepo` (core tests), Qt 6 Quick + QCoro, QtTest (ui tests).

## Global constraints

- **No Qt in `core/`** — `gitrepo.cpp` uses only libgit2 + `std`. Errors are
  `Expected<T>` values; no exceptions across layers
  ([`spec/engineering/engineering.md`](../spec/engineering/engineering.md)).
- **Paths via `generic_u8string()`/`toGitPath`/`fromGitPath`**, never `.string()`.
- New `core/` sources are already in `core/CMakeLists.txt` (we only add methods to
  existing files); **new tests → the matching list in `tests/CMakeLists.txt`**
  (`gittide_core_tests` for core, `gittide_ui_test_sources` for ui).
- New `ui/qml/` files → register in `ui/CMakeLists.txt`.
- **Colour from theme tokens only**, never a hex literal in QML.
- **TDD:** failing test first. Conform [code style](../spec/engineering/code-style.md)
  (`m_` members, lowercase filenames, Allman braces via `.clang-format`); fix every
  warning. Keep the existing single-commit-selection tests green.
- Build: `cmake --build build --parallel`; core tests:
  `ctest --test-dir build -R '<name>' --output-on-failure`.

---

## Task 1: Core — `rewordHead` + `commitMessage`

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp` (declare two methods)
- Modify: `core/src/gitrepo.cpp` (implement)
- Test (create): `tests/test_git_repo_reword.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces — produces:**
```cpp
// In class GitRepo, public:
Expected<std::string> rewordHead(std::string newMessage);   // returns new oid
Expected<std::string> commitMessage(std::string oid) const; // full summary+body
```

- [ ] **Step 1: Write the failing test.** Create `tests/test_git_repo_reword.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "gittide/gitrepo.hpp"
#include "gittide/pathutil.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;

TEST_CASE("rewordHead rewrites the HEAD message, keeping tree and parent", "[reword]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("first");
    tmp.writeFile("b.txt", "two\n");
    tmp.commitAll("second original");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // Capture the pre-reword tree + parent of HEAD via libgit2.
    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gittide::toGitPath(tmp.path()).c_str()) == 0);
    git_oid head;
    REQUIRE(git_reference_name_to_id(&head, r, "HEAD") == 0);
    git_commit* before = nullptr;
    REQUIRE(git_commit_lookup(&before, r, &head) == 0);
    const git_oid treeBefore = *git_commit_tree_id(before);
    git_commit_free(before);

    auto oid = repo->rewordHead("second reworded\n\nbody line\n");
    REQUIRE(oid.has_value());
    REQUIRE(oid->size() == 40);

    git_oid head2;
    REQUIRE(git_reference_name_to_id(&head2, r, "HEAD") == 0);
    git_commit* after = nullptr;
    REQUIRE(git_commit_lookup(&after, r, &head2) == 0);
    REQUIRE(std::string(git_commit_message(after)) == "second reworded\n\nbody line\n");
    REQUIRE(git_oid_equal(git_commit_tree_id(after), &treeBefore) == 1); // tree unchanged
    REQUIRE(git_commit_parentcount(after) == 1);                          // parent kept
    git_commit_free(after);
    git_repository_free(r);
}

TEST_CASE("commitMessage returns the full summary + body", "[reword]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("subject line\n\nlong body\nsecond body line\n");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto hist = repo->log();
    REQUIRE(hist.has_value());
    REQUIRE(hist->size() == 1);

    auto msg = repo->commitMessage(hist->front().oid);
    REQUIRE(msg.has_value());
    REQUIRE(*msg == "subject line\n\nlong body\nsecond body line\n");
}

TEST_CASE("rewordHead errors on an unborn branch", "[reword]")
{
    gittide::test::TempRepo tmp; // fresh init, no commit => unborn HEAD
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto oid = repo->rewordHead("nope");
    REQUIRE_FALSE(oid.has_value());
}
```

- [ ] **Step 2: Add the test to the build.** In `tests/CMakeLists.txt`, add
  `test_git_repo_reword.cpp` to the `gittide_core_tests` source list (after
  `test_git_repo_reset.cpp`).

- [ ] **Step 3: Run it — verify it fails to compile/link** (methods undefined):

Run: `cmake --build build --parallel 2>&1 | grep -i reword`
Expected: error — `rewordHead` / `commitMessage` not members of `GitRepo`.

- [ ] **Step 4: Declare the methods** in `core/include/gittide/gitrepo.hpp`,
  after `checkoutCommit` (~line 181):

```cpp
    // Rewrite HEAD's commit message via git_commit_amend, keeping the tree and
    // parents exactly (working tree/index untouched, submodule pointers preserved).
    // Errors on an unborn or detached HEAD. Returns the new commit's hex oid.
    Expected<std::string> rewordHead(std::string newMessage);

    // Full commit message (summary + body) of the 40-char hex oid. Used to
    // pre-fill the reword dialog. Errors on a bad oid.
    Expected<std::string> commitMessage(std::string oid) const;
```

- [ ] **Step 5: Implement** in `core/src/gitrepo.cpp` (after `commit`, ~line 463):

```cpp
Expected<std::string> GitRepo::rewordHead(std::string newMessage)
{
    git_reference* head = nullptr;
    int rc              = git_repository_head(&head, m_repo);
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND)
        return std::unexpected(GitError{-1, "cannot reword: no commit on this branch"});
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);

    if (git_reference_is_branch(head) != 1)
        return std::unexpected(GitError{-1, "cannot reword a detached HEAD"});

    git_oid head_oid;
    rc = git_reference_name_to_id(&head_oid, m_repo, "HEAD");
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    git_commit* commit = nullptr;
    rc                 = git_commit_lookup(&commit, m_repo, &head_oid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_commit, decltype(&git_commit_free)> commit_guard(commit, git_commit_free);

    // Refresh committer from config; keep author (nullptr) and tree (nullptr).
    git_signature* committer = nullptr;
    rc                       = git_signature_default(&committer, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_signature, decltype(&git_signature_free)> sig_guard(committer, git_signature_free);

    git_oid new_oid;
    rc = git_commit_amend(&new_oid, commit, "HEAD", /*author=*/nullptr, committer,
                          /*encoding=*/nullptr, newMessage.c_str(), /*tree=*/nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), &new_oid);
    return std::string(buf);
}

Expected<std::string> GitRepo::commitMessage(std::string oidHex) const
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

    const char* msg = git_commit_message(commit);
    return std::string(msg ? msg : "");
}
```

- [ ] **Step 6: Run the tests — verify they pass:**

Run: `cmake --build build --parallel && ctest --test-dir build -R reword --output-on-failure`
Expected: all `[reword]` cases PASS.

- [ ] **Step 7: Commit.**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp \
        tests/test_git_repo_reword.cpp tests/CMakeLists.txt
git commit -m "feat(core): rewordHead + commitMessage on GitRepo"
```

---

## Task 2: Core — `rangeFiles` + `rangeDiff`

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`
- Modify: `core/src/gitrepo.cpp`
- Test (create): `tests/test_git_repo_range_diff.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces — consumes:** private `commitTrees(oidHex, outTree, outParentTree)`
(already exists, `gitrepo.cpp:1317`). **Produces:**
```cpp
Expected<std::vector<FileStatus>> rangeFiles(std::string oldOid, std::string newOid) const;
Expected<DiffResult> rangeDiff(std::string oldOid, std::string newOid,
                               const std::filesystem::path& file) const;
```

- [ ] **Step 1: Write the failing test.** Create `tests/test_git_repo_range_diff.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
bool has(const std::vector<gittide::FileStatus>& v, const std::string& p)
{
    return std::any_of(v.begin(), v.end(),
                       [&](const auto& f) { return f.path.generic_string() == p; });
}
}

TEST_CASE("rangeFiles lists the net file set across a contiguous range", "[range]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "a1\n"); tmp.commitAll("c1");      // adds a.txt
    tmp.writeFile("b.txt", "b1\n"); tmp.commitAll("c2");      // adds b.txt
    tmp.writeFile("a.txt", "a1\na2\n"); tmp.commitAll("c3");  // modifies a.txt

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto h = repo->log();
    REQUIRE(h.has_value());
    REQUIRE(h->size() == 3);
    const std::string c3 = (*h)[0].oid; // newest
    const std::string c2 = (*h)[1].oid;
    const std::string c1 = (*h)[2].oid; // oldest

    // Range c1..c3 inclusive == parent(c1) [empty] vs tree(c3): both files present.
    auto files = repo->rangeFiles(c1, c3);
    REQUIRE(files.has_value());
    REQUIRE(has(*files, "a.txt"));
    REQUIRE(has(*files, "b.txt"));

    // Per-file diff over the range carries hunks.
    auto d = repo->rangeDiff(c1, c3, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE_FALSE(d->hunks.empty());

    // Range c2..c3 (parent(c2)=c1's tree vs c3): a.txt modified, b.txt added.
    auto sub = repo->rangeFiles(c2, c3);
    REQUIRE(sub.has_value());
    REQUIRE(has(*sub, "a.txt"));
    REQUIRE(has(*sub, "b.txt"));
}

TEST_CASE("rangeFiles of a single commit equals that commit's files", "[range]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "a1\n"); tmp.commitAll("c1");
    tmp.writeFile("b.txt", "b1\n"); tmp.commitAll("c2");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto h = repo->log();
    REQUIRE(h.has_value());
    const std::string c2 = h->front().oid;

    auto single = repo->rangeFiles(c2, c2);
    auto cf     = repo->commitFiles(c2);
    REQUIRE(single.has_value());
    REQUIRE(cf.has_value());
    REQUIRE(single->size() == cf->size());
    REQUIRE(has(*single, "b.txt"));
    REQUIRE_FALSE(has(*single, "a.txt"));
}
```

- [ ] **Step 2: Add the test to the build.** In `tests/CMakeLists.txt`, add
  `test_git_repo_range_diff.cpp` to `gittide_core_tests` (after the reword test).

- [ ] **Step 3: Run it — verify it fails to compile** (methods undefined):

Run: `cmake --build build --parallel 2>&1 | grep -i range`
Expected: error — `rangeFiles` / `rangeDiff` not members of `GitRepo`.

- [ ] **Step 4: Declare the methods** in `gitrepo.hpp`, after `commitDiff` (~line 87):

```cpp
    // Files changed across the inclusive range oldOid..newOid:
    // tree(parent(oldOid)) vs tree(newOid). Flags use Index* (added/modified/
    // deleted), matching commitFiles(). Caller guarantees a contiguous range.
    Expected<std::vector<FileStatus>> rangeFiles(std::string oldOid, std::string newOid) const;

    // Diff one file across the inclusive range oldOid..newOid (same tree pair as
    // rangeFiles). Mirrors commitDiff()'s DiffResult.
    Expected<DiffResult> rangeDiff(std::string oldOid, std::string newOid,
                                   const std::filesystem::path& file) const;
```

- [ ] **Step 5: Implement** in `gitrepo.cpp`, after `commitDiff` (~line 1418).
  Reuse `commitTrees` twice: the older endpoint's **parent** tree vs the newer
  endpoint's **own** tree.

```cpp
Expected<std::vector<FileStatus>> GitRepo::rangeFiles(std::string oldOid, std::string newOid) const
{
    // Older endpoint's first-parent tree (null for a root commit).
    git_tree* oldOwn    = nullptr;
    git_tree* oldParent = nullptr;
    if (auto r = commitTrees(oldOid, &oldOwn, &oldParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldOwn_guard(oldOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldParent_guard(oldParent, git_tree_free);

    // Newer endpoint's own tree.
    git_tree* newOwn    = nullptr;
    git_tree* newParent = nullptr;
    if (auto r = commitTrees(newOid, &newOwn, &newParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newOwn_guard(newOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newParent_guard(newParent, git_tree_free);

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, oldParent, newOwn, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);

    std::vector<FileStatus> result;
    size_t n = git_diff_num_deltas(raw);
    result.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        const git_diff_delta* d = git_diff_get_delta(raw, i);
        StatusFlag flag         = StatusFlag::IndexModified;
        const char* path        = d->new_file.path;
        switch (d->status)
        {
            case GIT_DELTA_ADDED:
                flag = StatusFlag::IndexNew;
                break;
            case GIT_DELTA_DELETED:
                flag = StatusFlag::IndexDeleted;
                path = d->old_file.path;
                break;
            default:
                flag = StatusFlag::IndexModified;
                break;
        }
        if (path)
            result.push_back(FileStatus{fromGitPath(path), flag});
    }
    return result;
}

Expected<DiffResult> GitRepo::rangeDiff(std::string oldOid, std::string newOid,
                                        const std::filesystem::path& file) const
{
    git_tree* oldOwn    = nullptr;
    git_tree* oldParent = nullptr;
    if (auto r = commitTrees(oldOid, &oldOwn, &oldParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldOwn_guard(oldOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> oldParent_guard(oldParent, git_tree_free);

    git_tree* newOwn    = nullptr;
    git_tree* newParent = nullptr;
    if (auto r = commitTrees(newOid, &newOwn, &newParent); !r)
        return std::unexpected(r.error());
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newOwn_guard(newOwn, git_tree_free);
    std::unique_ptr<git_tree, decltype(&git_tree_free)> newParent_guard(newParent, git_tree_free);

    std::string git_file  = toGitPath(file);
    char* paths[]         = {git_file.data()};
    git_diff_options opts  = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.strings = paths;
    opts.pathspec.count   = 1;

    git_diff* raw = nullptr;
    int rc        = git_diff_tree_to_tree(&raw, m_repo, oldParent, newOwn, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_diff, decltype(&git_diff_free)> diff_guard(raw, git_diff_free);
    return DiffEngine::parse(diff_guard.get());
}
```

- [ ] **Step 6: Run the tests — verify they pass:**

Run: `cmake --build build --parallel && ctest --test-dir build -R range --output-on-failure`
Expected: all `[range]` cases PASS. Re-run `ctest --test-dir build` — full suite green.

- [ ] **Step 7: Commit.**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp \
        tests/test_git_repo_range_diff.cpp tests/CMakeLists.txt
git commit -m "feat(core): rangeFiles + rangeDiff for contiguous commit ranges"
```

---

## Task 3: AsyncRepo — thread-pool wrappers

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp`
- Modify: `ui/src/asyncrepo.cpp`

**Interfaces — consumes:** Task 1 + 2 core methods. **Produces:**
```cpp
QCoro::Task<gittide::Expected<std::string>> rewordHead(QString message);
QCoro::Task<gittide::Expected<std::string>> commitMessage(QString oid);
QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> rangeFiles(QString oldOid, QString newOid);
QCoro::Task<gittide::Expected<gittide::DiffResult>> rangeDiff(QString oldOid, QString newOid, std::filesystem::path file);
```

This task has no standalone test — it is exercised by the controller tests in
Task 4/7. Keep the wrappers mechanical (mirror `commitFiles`).

- [ ] **Step 1: Declare** in `asyncrepo.hpp`, after the `commitDiff` declaration
  (~line 49):

```cpp
    QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> rangeFiles(QString oldOid, QString newOid);
    QCoro::Task<gittide::Expected<gittide::DiffResult>>              rangeDiff(QString oldOid, QString newOid, std::filesystem::path file);
    QCoro::Task<gittide::Expected<std::string>>                      rewordHead(QString message);
    QCoro::Task<gittide::Expected<std::string>>                      commitMessage(QString oid);
```

- [ ] **Step 2: Implement** in `asyncrepo.cpp`, after `AsyncRepo::commitDiff`.
  Follow the `commitFiles` body verbatim (capture `m_impl`, lock, call core):

```cpp
QCoro::Task<gittide::Expected<std::vector<gittide::FileStatus>>> AsyncRepo::rangeFiles(QString oldOid, QString newOid)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oldOid.toStdString(), n = newOid.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.rangeFiles(o, n);
        });
}

QCoro::Task<gittide::Expected<gittide::DiffResult>> AsyncRepo::rangeDiff(QString oldOid, QString newOid, std::filesystem::path file)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oldOid.toStdString(), n = newOid.toStdString(), f = std::move(file)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.rangeDiff(o, n, f);
        });
}

QCoro::Task<gittide::Expected<std::string>> AsyncRepo::rewordHead(QString message)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, m = message.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.rewordHead(m);
        });
}

QCoro::Task<gittide::Expected<std::string>> AsyncRepo::commitMessage(QString oid)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, o = oid.toStdString()]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.commitMessage(o);
        });
}
```

> If `m_impl` / `impl->repo` / `impl->mutex` names differ in your copy, match the
> existing `commitFiles` implementation exactly — it is the canonical pattern.

- [ ] **Step 3: Build — verify it compiles:**

Run: `cmake --build build --parallel`
Expected: success, no warnings.

- [ ] **Step 4: Commit.**

```bash
git add ui/include/gittide/ui/asyncrepo.hpp ui/src/asyncrepo.cpp
git commit -m "feat(ui): AsyncRepo wrappers for range diff + reword + commitMessage"
```

---

## Task 4: RepoController — reword task + commitMessage signal

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp`
- Modify: `ui/src/repocontroller.cpp`
- Test (create): `tests/ui/test_repocontroller_reword.cpp`
- Modify: `tests/CMakeLists.txt` (`gittide_ui_test_sources`)

**Interfaces — consumes:** Task 3 AsyncRepo. **Produces:**
```cpp
// slots:
QCoro::Task<void> rewordHead(QString message);
QCoro::Task<void> requestCommitMessage(QString oid);
// signals:
void commitMessageReady(QString oid, QString message);
```
(`committed` and `operationFailed` already exist and are reused.)

- [ ] **Step 1: Write the failing test.** Create
  `tests/ui/test_repocontroller_reword.cpp`. Model the harness on
  `test_repo_view_model.cpp` (a committed repo, drive the slot, wait on a spy):

```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>
#include <git2.h>

#include "gittide/ui/repocontroller.hpp"

using gittide::ui::RepoController;

namespace reword_ctrl_test {
inline std::filesystem::path make_repo_with_commit(const char* message)
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-rwc-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr; git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    { std::ofstream(dir / "a.txt") << "one\n"; }
    git_index* idx = nullptr; git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt"); git_index_write(idx);
    git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid; git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, message, tree, 0);
    git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    git_repository_free(raw); git_libgit2_shutdown();
    return dir;
}
}

class TestRepoControllerReword : public QObject
{
    Q_OBJECT
private slots:
    void reword_changes_head_message_and_emits_committed()
    {
        const auto dir = reword_ctrl_test::make_repo_with_commit("old subject\n");
        RepoController c;
        QSignalSpy opened(&c, &RepoController::repoOpened);
        c.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(opened.wait(3000));

        QSignalSpy committed(&c, &RepoController::committed);
        c.rewordHead(QStringLiteral("new subject\n"));
        QVERIFY(committed.wait(3000));

        QSignalSpy msgReady(&c, &RepoController::commitMessageReady);
        const QString head = committed.takeFirst().at(0).toString();
        c.requestCommitMessage(head);
        QVERIFY(msgReady.wait(3000));
        QCOMPARE(msgReady.takeFirst().at(1).toString(), QStringLiteral("new subject\n"));

        std::filesystem::remove_all(dir);
    }
};

QTEST_MAIN(TestRepoControllerReword)
#include "test_repocontroller_reword.moc"
```

> Check `repoOpened`'s exact signature in `repocontroller.hpp` (it is
> `repoOpened(const QString& path)`). If `open()` is synchronous and emits before
> the spy connects, replace the `opened.wait` with a direct `QVERIFY(c.isOpen())`
> after `open()`.

- [ ] **Step 2: Register the test** in `tests/CMakeLists.txt` under
  `gittide_ui_test_sources` (add the `${CMAKE_CURRENT_SOURCE_DIR}/ui/test_repocontroller_reword.cpp` line).

- [ ] **Step 3: Run it — verify it fails** (slots/signal undefined):

Run: `cmake --build build --parallel 2>&1 | grep -i reword`
Expected: error — no member `rewordHead` / `commitMessageReady` in `RepoController`.

- [ ] **Step 4: Declare** in `repocontroller.hpp`. Slots (after `refreshCommitDiff`,
  ~line 59):

```cpp
    QCoro::Task<void> rewordHead(QString message);
    QCoro::Task<void> requestCommitMessage(QString oid);
```

  Signal (after `commitDiffReady`, ~line 107):

```cpp
    void commitMessageReady(QString oid, QString message);
```

- [ ] **Step 5: Implement** in `repocontroller.cpp` (after `refreshCommitDiff`,
  ~line 396). The reword cascade mirrors `checkoutCommit` (status + history +
  branches, since `HEAD` moves) and emits `committed`:

```cpp
QCoro::Task<void> RepoController::rewordHead(QString message)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->rewordHead(message);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    emit committed(QString::fromStdString(*r));
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
}

QCoro::Task<void> RepoController::requestCommitMessage(QString oid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->commitMessage(oid);
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    emit commitMessageReady(oid, QString::fromStdString(*r));
}
```

- [ ] **Step 6: Run the test — verify it passes:**

Run: `cmake --build build --parallel && ctest --test-dir build -R Reword --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit.**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp \
        tests/ui/test_repocontroller_reword.cpp tests/CMakeLists.txt
git commit -m "feat(ui): RepoController reword task + commitMessage signal"
```

---

## Task 5: RepoViewModel — reword + message fetch

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`
- Modify: `ui/src/repoviewmodel.cpp`
- Test (modify): `tests/ui/test_repo_view_model.cpp` (add a case)

**Interfaces — consumes:** Task 4 controller slots/signal. **Produces:**
```cpp
Q_INVOKABLE void rewordHead(const QString& message);
Q_INVOKABLE void requestCommitMessage(const QString& oid);
// signal:
void commitMessageReady(const QString& oid, const QString& message);
```

- [ ] **Step 1: Write the failing test** — add to the existing test class in
  `tests/ui/test_repo_view_model.cpp` (a `make_repo_with_commit`-style repo is
  already available via the file's helpers; reuse `make_dirty_repo` then commit,
  or add a small helper). Add this slot:

```cpp
    void reword_updates_head_summary_in_history()
    {
        const auto dir = repo_view_model_test::make_dirty_repo();
        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));

        // Commit the dirty change so HEAD has a known message.
        QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
        vm.commit(QStringLiteral("before reword"), QString());
        QVERIFY(committedSpy.wait(3000));
        QTRY_VERIFY_WITH_TIMEOUT(vm.history()->rowCount() >= 1, 3000);

        const QString headOid = vm.history()->data(
            vm.history()->index(0, 0), gittide::ui::HistoryListModel::OidRole).toString();

        // Lazy-fetch the message round-trips the committed text.
        QSignalSpy msgSpy(&vm, &RepoViewModel::commitMessageReady);
        vm.requestCommitMessage(headOid);
        QVERIFY(msgSpy.wait(3000));
        QCOMPARE(msgSpy.takeFirst().at(1).toString(), QStringLiteral("before reword"));

        // Reword → the top history row's summary changes.
        vm.rewordHead(QStringLiteral("after reword"));
        QTRY_COMPARE_WITH_TIMEOUT(
            vm.history()->data(vm.history()->index(0, 0),
                               gittide::ui::HistoryListModel::SummaryRole).toString(),
            QStringLiteral("after reword"), 3000);

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run it — verify it fails** (no member `rewordHead`):

Run: `cmake --build build --parallel 2>&1 | grep -i "rewordHead\|requestCommitMessage"`
Expected: error — not a member of `RepoViewModel`.

- [ ] **Step 3: Declare** in `repoviewmodel.hpp`. Q_INVOKABLEs (near
  `checkoutCommit`, ~line 118):

```cpp
    Q_INVOKABLE void rewordHead(const QString& message);
    Q_INVOKABLE void requestCommitMessage(const QString& oid);
```

  Signal (after `activeCommitFileChanged`, ~line 166):

```cpp
    void commitMessageReady(const QString& oid, const QString& message);
```

- [ ] **Step 4: Wire the controller signal** in the `RepoViewModel` constructor
  (`repoviewmodel.cpp`, near the other `connect(m_controller, ...)` lines ~35):

```cpp
    connect(m_controller, &RepoController::commitMessageReady,
            this, &RepoViewModel::commitMessageReady);
```

- [ ] **Step 5: Implement** the two methods in `repoviewmodel.cpp` (near
  `checkoutCommit`, ~line 524):

```cpp
void RepoViewModel::rewordHead(const QString& message)
{
    QCoro::connect(m_controller->rewordHead(message), this, [] {});
}

void RepoViewModel::requestCommitMessage(const QString& oid)
{
    QCoro::connect(m_controller->requestCommitMessage(oid), this, [] {});
}
```

- [ ] **Step 6: Run the test — verify it passes:**

Run: `cmake --build build --parallel && ctest --test-dir build -R RepoViewModel --output-on-failure`
Expected: PASS (including the new case and all existing ones).

- [ ] **Step 7: Commit.**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp \
        tests/ui/test_repo_view_model.cpp
git commit -m "feat(ui): RepoViewModel rewordHead + commit-message fetch"
```

---

## Task 6: QML — RewordDialog + context-menu item + pane wiring

**Files:**
- Create: `ui/qml/RewordDialog.qml`
- Modify: `ui/qml/CommitContextMenu.qml`
- Modify: `ui/qml/HistoryPane.qml`
- Modify: `ui/CMakeLists.txt` (register `RewordDialog.qml`)
- Test (modify): `tests/ui/test_qml_history.cpp` — assert the menu exposes a
  `reword` signal / item and the dialog instantiates.

**Interfaces — consumes:** Task 5 VM (`rewordHead`, `requestCommitMessage`,
`commitMessageReady`). **Produces:** a `reword()` signal on `CommitContextMenu`.

- [ ] **Step 1: Write the failing test.** Inspect `tests/ui/test_qml_history.cpp`
  for its loading pattern (it loads `HistoryPane.qml` / components into a
  `QQmlApplicationEngine` or `QQuickView`). Add a case that loads
  `RewordDialog.qml` and verifies a `summary` property + an `accepted`/`reworded`
  signal exists. Concretely, append:

```cpp
    void reword_dialog_loads_and_exposes_summary()
    {
        QQmlEngine engine;
        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qt/qml/GitTide/qml/RewordDialog.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QVERIFY(obj->property("summary").isValid());
    }
```

> Match the QML import URL to how `test_qml_history.cpp` already references
> components (it may use a `file://` path or the `qrc:/qt/qml/GitTide/...`
> resource root — copy the exact prefix it uses).

- [ ] **Step 2: Run it — verify it fails** (component missing):

Run: `cmake --build build --parallel 2>&1 | grep -i reword` then run the test.
Expected: FAIL — `RewordDialog.qml` does not exist.

- [ ] **Step 3: Create `ui/qml/RewordDialog.qml`.** Model it on the existing
  dialog pattern (open `ui/qml/NewBranchDialog.qml` or `DeleteBranchDialog.qml`
  and follow its `OverlayCard` structure, button styles, and theme tokens — do
  **not** hard-code colours). Minimum shape:

```qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Reword the HEAD commit message. Pre-filled via repoVm.commitMessageReady.
OverlayCard {            // match the base component used by NewBranchDialog
    id: dialog
    objectName: "rewordDialog"

    property string oid: ""
    property alias summary: summaryField.text

    signal reworded(string message)

    // Open for a commit and request its full message to pre-fill.
    function openFor(commitOid) {
        dialog.oid = commitOid
        summaryField.text = ""
        bodyField.text = ""
        dialog.open()                       // OverlayCard's show method
        if (repoVm) repoVm.requestCommitMessage(commitOid)
    }

    Connections {
        target: repoVm
        function onCommitMessageReady(oid, message) {
            if (oid !== dialog.oid) return
            var nl = message.indexOf("\n")
            if (nl < 0) { summaryField.text = message; bodyField.text = "" }
            else {
                summaryField.text = message.substring(0, nl)
                // skip the blank line that conventionally separates subject/body
                bodyField.text = message.substring(message.charAt(nl + 1) === "\n" ? nl + 2 : nl + 1)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Label { text: "Reword commit"; color: theme.textPrimary }
        TextField {
            id: summaryField
            Layout.fillWidth: true
            placeholderText: "Summary"
        }
        TextArea {
            id: bodyField
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            placeholderText: "Extended description (optional)"
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Button { text: "Cancel"; onClicked: dialog.close() }
            Button {
                text: "Save"
                enabled: summaryField.text.trim().length > 0
                onClicked: {
                    var msg = summaryField.text.trim()
                    if (bodyField.text.trim().length > 0)
                        msg += "\n\n" + bodyField.text.trim() + "\n"
                    else
                        msg += "\n"
                    dialog.reworded(msg)
                    dialog.close()
                }
            }
        }
    }
}
```

> Adapt `OverlayCard`, `open()`/`close()`, and `Button` styling to the actual base
> components/method names used by `NewBranchDialog.qml`. The contract this task
> must satisfy: a `summary` property, an `openFor(oid)` function, and a
> `reworded(message)` signal.

- [ ] **Step 4: Register** `RewordDialog.qml` in `ui/CMakeLists.txt` (the
  `qt_add_qml_module` QML_FILES list, alongside `NewBranchDialog.qml`).

- [ ] **Step 5: Add the menu item** to `ui/qml/CommitContextMenu.qml`. Add the
  signal and a HEAD-gated item (after the Checkout item, before the merge
  separator):

```qml
    signal reword()
```
```qml
    AppMenuItem {
        text: "Reword…"
        visible: menu.isHead
        onTriggered: menu.reword()
    }
```

- [ ] **Step 6: Wire it in `ui/qml/HistoryPane.qml`.** Add the dialog instance
  next to `commitNewBranchDialog` and connect the menu signal:

```qml
    RewordDialog {
        id: rewordDialog
        onReworded: function(message) { if (repoVm) repoVm.rewordHead(message) }
    }
```
  And in the `CommitContextMenu { ... }` block add:
```qml
        onReword: rewordDialog.openFor(commitMenu.oid)
```

- [ ] **Step 7: Build + run the QML history test — verify it passes:**

Run: `cmake --build build --parallel && ctest --test-dir build -R history --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit.**

```bash
git add ui/qml/RewordDialog.qml ui/qml/CommitContextMenu.qml ui/qml/HistoryPane.qml \
        ui/CMakeLists.txt tests/ui/test_qml_history.cpp
git commit -m "feat(ui): Reword… menu item + RewordDialog wired in HistoryPane"
```

---

## Task 7: Range plumbing — Controller + ViewModel routing

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp` + `ui/src/repocontroller.cpp`
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp`
- Test (modify): `tests/ui/test_repo_view_model.cpp` (add a range case)

**Interfaces — consumes:** Task 3 AsyncRepo `rangeFiles`/`rangeDiff`.
**Produces:**
```cpp
// RepoController slots:
QCoro::Task<void> refreshRangeFiles(QString oldOid, QString newOid);
QCoro::Task<void> refreshRangeDiff(QString oldOid, QString newOid, QString path);
// RepoController signals:
void rangeFilesReady(QString oldOid, QString newOid, std::vector<gittide::FileStatus> files);
void rangeDiffReady(QString oldOid, QString newOid, QString path, gittide::DiffResult result);
// RepoViewModel:
Q_INVOKABLE void selectCommitRows(const QVariantList& rows); // resolves + routes
Q_PROPERTY(QString historyDetailHeader ...); // range label, empty otherwise
Q_PROPERTY(QString historyDetailHint ...);   // non-contiguous hint, empty otherwise
```

`selectCommitRows` is the single entry point: it sorts the row indices, resolves
each to an oid via `m_history`, tests contiguity, and dispatches to the existing
single-commit flow, the range flow (`refreshRangeFiles`/`refreshRangeDiff` + a
header), or the hint (clears panes, sets the hint string).

- [ ] **Step 1: Write the failing test** — add to `test_repo_view_model.cpp`:

```cpp
    void range_selection_shows_combined_files_and_header()
    {
        // Build a repo with 3 commits adding a.txt, b.txt, c.txt.
        const auto dir = repo_view_model_test::make_dirty_repo(); // has 1 commit + dirty a.txt
        RepoViewModel vm;
        QSignalSpy filesSpy(vm.changedFiles(), &QAbstractItemModel::modelReset);
        vm.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(filesSpy.wait(3000));
        QSignalSpy committedSpy(&vm, &RepoViewModel::committedOk);
        vm.commit(QStringLiteral("c2"), QString());           // commit the dirty a.txt
        QVERIFY(committedSpy.wait(3000));
        QTRY_VERIFY_WITH_TIMEOUT(vm.history()->rowCount() >= 2, 3000);

        auto oidAt = [&](int row) {
            return vm.history()->data(vm.history()->index(row, 0),
                                      gittide::ui::HistoryListModel::OidRole).toString();
        };
        const QString newest = oidAt(0);
        const QString oldest = oidAt(1);

        Q_UNUSED(oldest); Q_UNUSED(newest);

        // Contiguous range rows {0,1} → combined files + header.
        QSignalSpy cfReset(vm.commitFiles(), &QAbstractItemModel::modelReset);
        vm.selectCommitRows(QVariantList{0, 1});
        QVERIFY(cfReset.wait(3000));
        QVERIFY(vm.commitFiles()->rowCount() >= 1);
        QVERIFY(vm.property("historyDetailHeader").toString().contains(QStringLiteral("2 commits")));
        QVERIFY(vm.property("historyDetailHint").toString().isEmpty());

        // Non-contiguous rows {0,2} → hint, no files (needs ≥3 commits; if the
        // repo has only 2, assert the single-row path clears the header instead).
        if (vm.history()->rowCount() >= 3)
        {
            vm.selectCommitRows(QVariantList{0, 2});
            QTRY_VERIFY_WITH_TIMEOUT(!vm.property("historyDetailHint").toString().isEmpty(), 3000);
        }

        // Single row → header + hint both cleared.
        vm.selectCommitRows(QVariantList{0});
        QTRY_VERIFY_WITH_TIMEOUT(vm.property("historyDetailHeader").toString().isEmpty(), 3000);

        std::filesystem::remove_all(dir);
    }
```

- [ ] **Step 2: Run it — verify it fails** (no `selectCommitRows`):

Run: `cmake --build build --parallel 2>&1 | grep -i "selectCommitRows\|historyDetailHeader"`
Expected: error — members missing.

- [ ] **Step 3: Controller — declare** in `repocontroller.hpp` (slots after
  `refreshCommitDiff`; signals after `commitDiffReady`):

```cpp
    QCoro::Task<void> refreshRangeFiles(QString oldOid, QString newOid);
    QCoro::Task<void> refreshRangeDiff(QString oldOid, QString newOid, QString path);
```
```cpp
    void rangeFilesReady(QString oldOid, QString newOid, std::vector<gittide::FileStatus> files);
    void rangeDiffReady(QString oldOid, QString newOid, QString path, gittide::DiffResult result);
```

- [ ] **Step 4: Controller — implement** in `repocontroller.cpp` (mirror
  `refreshCommitFiles`/`refreshCommitDiff`):

```cpp
QCoro::Task<void> RepoController::refreshRangeFiles(QString oldOid, QString newOid)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto files = co_await m_repo->rangeFiles(oldOid, newOid);
    if (!self)
        co_return;
    if (!files)
    {
        emit operationFailed(QString::fromStdString(files.error().message));
        co_return;
    }
    emit rangeFilesReady(oldOid, newOid, *files);
}

QCoro::Task<void> RepoController::refreshRangeDiff(QString oldOid, QString newOid, QString path)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto d = co_await m_repo->rangeDiff(oldOid, newOid, qstringToPath(path));
    if (!self)
        co_return;
    if (!d)
    {
        emit operationFailed(QString::fromStdString(d.error().message));
        co_return;
    }
    emit rangeDiffReady(oldOid, newOid, path, *d);
}
```

> `qstringToPath` is the helper `refreshCommitDiff` already uses — match its
> include/usage.

- [ ] **Step 5: ViewModel — declare** in `repoviewmodel.hpp`. Properties (near the
  other commit-detail properties, ~line 46):

```cpp
    Q_PROPERTY(QString historyDetailHeader READ historyDetailHeader NOTIFY historyDetailChanged)
    Q_PROPERTY(QString historyDetailHint READ historyDetailHint NOTIFY historyDetailChanged)
```
  Getters + the single public invokable + signal + private helpers/handlers/state.
  Add `#include <QVariantList>` to the header. Public:
```cpp
    QString historyDetailHeader() const { return m_detailHeader; }
    QString historyDetailHint() const { return m_detailHint; }
    // Single entry point from HistoryPane: rows is the selected row-index set.
    Q_INVOKABLE void selectCommitRows(const QVariantList& rows);
```
```cpp
    void historyDetailChanged();
```
  Private helpers + handlers + state:
```cpp
    // routing helpers (called by selectCommitRows):
    void applyRange(const QString& oldOid, const QString& newOid, int count);
    void applyRangeHint();
    // async handlers:
    void onRangeFiles(const QString& oldOid, const QString& newOid, const std::vector<gittide::FileStatus>& files);
    void onRangeDiff(const QString& oldOid, const QString& newOid, const QString& path, const gittide::DiffResult& result);
    // state:
    QString m_rangeOld;        // non-empty only in range mode
    QString m_rangeNew;
    QString m_detailHeader;
    QString m_detailHint;
```

- [ ] **Step 6: ViewModel — wire signals** in the constructor (near line 36):

```cpp
    connect(m_controller, &RepoController::rangeFilesReady, this, &RepoViewModel::onRangeFiles);
    connect(m_controller, &RepoController::rangeDiffReady, this, &RepoViewModel::onRangeDiff);
```

- [ ] **Step 7: ViewModel — implement** in `repoviewmodel.cpp`. Range mode reuses
  the `m_commitFiles`/`m_commitDiff` models; `selectCommit`/`selectCommitFile`
  must clear/respect range mode:

```cpp
void RepoViewModel::selectCommitRows(const QVariantList& rows)
{
    if (!m_history)
        return;

    // Sort row indices ascending; drop out-of-range entries.
    std::vector<int> idx;
    idx.reserve(rows.size());
    for (const auto& v : rows)
    {
        bool ok = false;
        const int r = v.toInt(&ok);
        if (ok && r >= 0 && r < m_history->rowCount())
            idx.push_back(r);
    }
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    if (idx.empty())
        return;

    auto oidAt = [&](int row) {
        return m_history->data(m_history->index(row, 0),
                               HistoryListModel::OidRole).toString();
    };

    if (idx.size() == 1)
    {
        selectCommit(oidAt(idx.front())); // single-commit flow; clears range mode
        return;
    }

    const bool contiguous = (idx.back() - idx.front()) == int(idx.size()) - 1;
    if (!contiguous)
    {
        applyRangeHint();
        return;
    }

    // History is newest-first: the largest row index is the oldest commit.
    applyRange(oidAt(idx.back()), oidAt(idx.front()), int(idx.size()));
}

void RepoViewModel::applyRange(const QString& oldOid, const QString& newOid, int count)
{
    m_rangeOld = oldOid;
    m_rangeNew = newOid;
    m_selectedCommit = newOid;          // anchor for per-file diff matching
    m_activeCommitFile.clear();
    m_commitFiles->setFiles({});
    m_commitDiff->clear();
    m_detailHint.clear();
    m_detailHeader = QStringLiteral("Changes across %1 commits (%2…%3)")
                         .arg(count)
                         .arg(oldOid.left(7), newOid.left(7));
    emit selectedCommitChanged();
    emit activeCommitFileChanged();
    emit historyDetailChanged();
    QCoro::connect(m_controller->refreshRangeFiles(oldOid, newOid), this, [] {});
}

void RepoViewModel::applyRangeHint()
{
    m_rangeOld.clear();
    m_rangeNew.clear();
    m_selectedCommit.clear();
    m_activeCommitFile.clear();
    m_commitFiles->setFiles({});
    m_commitDiff->clear();
    m_detailHeader.clear();
    m_detailHint = QStringLiteral("Select a contiguous range to see combined changes.");
    emit selectedCommitChanged();
    emit activeCommitFileChanged();
    emit historyDetailChanged();
}
```

> Add `#include <algorithm>` to `repoviewmodel.cpp` if not already present (used
> for `std::sort`/`std::unique`).

```cpp

void RepoViewModel::onRangeFiles(const QString& oldOid, const QString& newOid,
                                 const std::vector<gittide::FileStatus>& files)
{
    if (oldOid != m_rangeOld || newOid != m_rangeNew)
        return;
    m_commitFiles->setFiles(files);
}

void RepoViewModel::onRangeDiff(const QString& oldOid, const QString& newOid,
                                const QString& path, const gittide::DiffResult& result)
{
    if (oldOid != m_rangeOld || newOid != m_rangeNew || path != m_activeCommitFile)
        return;
    m_commitDiff->setDiff(result, {}, false);
}
```

  Update `selectCommit` (clear range mode + detail strings) — add at its top:

```cpp
    m_rangeOld.clear();
    m_rangeNew.clear();
    m_detailHeader.clear();
    m_detailHint.clear();
    emit historyDetailChanged();
```

  Update `selectCommitFile` to route by mode:

```cpp
void RepoViewModel::selectCommitFile(const QString& path)
{
    m_activeCommitFile = path;
    emit activeCommitFileChanged();
    if (!m_rangeOld.isEmpty())
        QCoro::connect(m_controller->refreshRangeDiff(m_rangeOld, m_rangeNew, path), this, [] {});
    else
        QCoro::connect(m_controller->refreshCommitDiff(m_selectedCommit, path), this, [] {});
}
```

- [ ] **Step 8: Run the test — verify it passes:**

Run: `cmake --build build --parallel && ctest --test-dir build -R RepoViewModel --output-on-failure`
Expected: PASS (new case + existing single-commit cases still green).

- [ ] **Step 9: Commit.**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp \
        ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp \
        tests/ui/test_repo_view_model.cpp
git commit -m "feat(ui): range-diff routing in controller + view model"
```

---

## Task 8: QML — multi-select gestures + CommitDetail header/hint

**Files:**
- Modify: `ui/qml/HistoryPane.qml`
- Modify: `ui/qml/CommitDetail.qml`
- Test (modify): `tests/ui/test_qml_history.cpp`

**Interfaces — consumes:** Task 7 VM (`selectCommitRows`, `historyDetailHeader`,
`historyDetailHint`).

- [ ] **Step 1: Write the failing test.** In `test_qml_history.cpp`, add a check
  that `CommitDetail.qml` renders a header item bound to
  `repoVm.historyDetailHeader` (find the object by `objectName` after load). For
  example, after loading the pane with a stub `repoVm`:

```cpp
    void commit_detail_has_range_header_item()
    {
        // load CommitDetail.qml (match the file's existing load helper)
        QQmlEngine engine;
        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qt/qml/GitTide/qml/CommitDetail.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QVERIFY(obj->findChild<QObject*>("rangeHeaderLabel") != nullptr);
    }
```

- [ ] **Step 2: Run it — verify it fails** (no such child):

Run the `history` test; Expected: FAIL — `rangeHeaderLabel` not found.

- [ ] **Step 3: Add the header/hint to `ui/qml/CommitDetail.qml`.** At the top of
  its layout, add a label shown only when the VM provides header or hint text
  (use theme tokens, no hex):

```qml
    Label {
        objectName: "rangeHeaderLabel"
        Layout.fillWidth: true
        visible: repoVm && (repoVm.historyDetailHeader.length > 0 || repoVm.historyDetailHint.length > 0)
        text: repoVm ? (repoVm.historyDetailHint.length > 0
                        ? repoVm.historyDetailHint
                        : repoVm.historyDetailHeader) : ""
        color: repoVm && repoVm.historyDetailHint.length > 0 ? theme.textMuted : theme.textPrimary
        wrapMode: Text.WordWrap
        padding: 8
    }
```

  When `historyDetailHint` is non-empty the existing files/diff sub-views are
  empty (the VM cleared them), so the hint stands alone. Adjust the surrounding
  layout container if `CommitDetail.qml` is not already a `ColumnLayout` — wrap
  the existing content so the label sits above it.

- [ ] **Step 4: Add multi-select gestures to `ui/qml/HistoryPane.qml`.** Track a
  selection set of **row indices** on the `ListView` and hand the whole set to the
  VM — no oid resolution in QML (that lives in `selectCommitRows`). Add the
  property + helper on `historyList`:

```qml
            // Selected row indices. Always includes currentIndex.
            property var selectedRows: []

            function applySelection() {
                if (repoVm) repoVm.selectCommitRows(selectedRows)
            }
```

  Replace the left-click branch of the delegate's `MouseArea.onClicked` (currently
  `historyList.currentIndex = index; if (repoVm) repoVm.selectCommit(model.oid)`)
  with modifier-aware handling:

```qml
                        } else {
                            if (mouse.modifiers & Qt.ShiftModifier) {
                                var anchor = historyList.currentIndex
                                var lo = Math.min(anchor, index)
                                var hi = Math.max(anchor, index)
                                var range = []
                                for (var r = lo; r <= hi; ++r) range.push(r)
                                historyList.selectedRows = range
                                historyList.currentIndex = index
                            } else if (mouse.modifiers & Qt.ControlModifier) {
                                var set = historyList.selectedRows.slice()
                                var at = set.indexOf(index)
                                if (at >= 0) set.splice(at, 1); else set.push(index)
                                if (set.indexOf(historyList.currentIndex) < 0)
                                    set.push(historyList.currentIndex)
                                historyList.selectedRows = set
                                historyList.currentIndex = index
                            } else {
                                historyList.selectedRows = [index]
                                historyList.currentIndex = index
                            }
                            if (repoVm) historyList.applySelection()
                        }
```

  Extend the delegate highlight so set members (not just `currentItem`) show the
  selection background. Change the delegate `Rectangle.color` and the accent
  border `visible` to also test membership:

```qml
                color: (ListView.isCurrentItem
                        || historyList.selectedRows.indexOf(index) >= 0)
                       ? theme.surfaceOverlay : "transparent"
```
```qml
                    visible: parent.ListView.isCurrentItem
                             || historyList.selectedRows.indexOf(index) >= 0
```

  Keep the keyboard `Up`/`Down` handlers as-is but reset the set to the single
  current row when they fire (so arrow-nav returns to single-commit detail):

```qml
            Keys.onUpPressed: {
                if (currentIndex > 0) {
                    currentIndex--
                    selectedRows = [currentIndex]
                    if (repoVm) repoVm.selectCommitAtRow(currentIndex)
                }
            }
            Keys.onDownPressed: {
                if (currentIndex < count - 1) {
                    currentIndex++
                    selectedRows = [currentIndex]
                    if (repoVm) repoVm.selectCommitAtRow(currentIndex)
                }
            }
```

- [ ] **Step 5: Build + run the QML history test — verify it passes:**

Run: `cmake --build build --parallel && ctest --test-dir build -R history --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Manual smoke (optional but recommended).** Launch the app, open a
  repo, History tab: Shift-click two commits → combined diff + "Changes across N
  commits" header; Ctrl-click a non-adjacent commit → hint; right-click HEAD →
  "Reword…" → edit → Save → top commit summary updates.

- [ ] **Step 7: Commit.**

```bash
git add ui/qml/HistoryPane.qml ui/qml/CommitDetail.qml tests/ui/test_qml_history.cpp
git commit -m "feat(ui): history multi-select gestures + combined-diff header/hint"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: combined range diff over a contiguous commit selection + reword of
>   the HEAD commit, from the History tab.
> - Spec updated: [`spec/product/history-editing.md`](../spec/product/history-editing.md)
>   (this plan realises it), with cross-refs in `context-menus.md` and `product.md`;
>   decision **D32**.
> - Code: `GitRepo::{rangeFiles,rangeDiff,rewordHead,commitMessage}`; AsyncRepo /
>   RepoController / RepoViewModel range + reword plumbing; `RewordDialog.qml`,
>   multi-select `HistoryPane.qml`, header/hint on `CommitDetail.qml`.
> - Deferred (unchanged): reword-of-older, squash, reorder → interactive-rebase
>   engine (spec §7).
