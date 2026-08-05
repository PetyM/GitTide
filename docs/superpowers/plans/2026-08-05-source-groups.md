# Source group nodes in the repository list

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-08-05 |
| **Status** | `planned` |
| **Spec** | [`specs/2026-08-05-source-groups-design.md`](../specs/2026-08-05-source-groups-design.md) |
| **Depends on** | [`2026-08-05-bulk-add-repos.md`](2026-08-05-bulk-add-repos.md) (done) |

**Goal:** Show each registered repository source as a collapsible group node in the sidebar holding the repositories beneath it, and fix three reported defaults/rendering issues in the add-from-folder dialog.

**Architecture:** The grouping is derived, not stored: `RepoListModel::setRepos` takes the project's `RepoSource` list alongside its `RepoRef` list and builds a group node per source, assigning each repository to the deepest source whose folder contains it. `projects.json` is untouched. Because group nodes break the "root row N is repository N" assumption that the poll pass and fleet fetch rely on, **all root addressing moves to paths first** (Task 1), before any group node can exist (Tasks 2-3).

**Tech stack:** C++23, Qt 6.8 Quick/QML, QCoro, QTest.

## Global constraints

- **Task 1 lands before Task 2.** Introducing group nodes while any caller still addresses a root row by position silently misroutes another repository's spinner, branch line and sync counts onto the wrong row. Nothing crashes and no existing test fails — that is why the ordering is mandatory.
- **No Qt in `core/`**; `core/` speaks `std` only. **libgit2 and nlohmann/json are PRIVATE to `core/`.**
- **Errors are values:** core returns `Expected<T>`; no exceptions across layers.
- **Paths via `toGitPath()` / `generic_u8string()`**, never `.string()`.
- **Colour comes from a `theme` token**, never a hex literal in QML.
- **Filesystem queries use `std::error_code` overloads** — no throwing calls.
- **TDD:** failing test first, then the smallest implementation.
- New `ui/` sources → `ui/CMakeLists.txt`; new QML → `ui/qml/qml.qrc`; new tests → `tests/CMakeLists.txt`, and a new `tests/ui/` class **also** needs an `#include` plus a `RUN()` line in `tests/ui/main.cpp`.
- **Code style:** Allman braces, `m_` members, lowercase file names, KISS/DRY/SOLID/YAGNI. `ui/src/projectcontroller.cpp` fails `clang-format --dry-run -Werror` in ~35 pre-existing spots — do NOT reformat it; keep changes additive and hand-match the surrounding style.
- **Build:** `cmake --build build --parallel`. Tests: `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure` — **245/245 at HEAD**, and it must stay green. Run nothing else concurrently; several `gittide_ui_tests` processes at once cause spurious signal-wait timeouts (check `pgrep -af gittide_ui_tests` before believing a failure).

---

## File structure

**UI model:**
- Modify `ui/include/gittide/ui/repolistmodel.hpp` + `ui/src/repolistmodel.cpp` — `Node::Kind`, three roles, grouping in `setRepos`, `setFetchStateByPath`, removal of the three row-indexed setters.

**UI controller:**
- Modify `ui/include/gittide/ui/projectcontroller.hpp` + `ui/src/projectcontroller.cpp` — path-addressed poll and fleet fetch, `m_authFailedRows` → `m_authFailedRefs`, sources passed to `setRepos`.

**QML:**
- Modify `ui/qml/Sidebar.qml` — source row rendering, `activate()`, auto-expansion, right-click routing.
- Create `ui/qml/SourceContextMenu.qml`; register in `ui/qml/qml.qrc`.
- Modify `ui/qml/AddFromFolderDialog.qml` — themed stepper, depth default, keep-as-source default.

**Core (defaults only):**
- Modify `core/include/gittide/reposcan.hpp` (`ScanOptions::maxDepth`) and `core/include/gittide/projectstore.hpp` (`RepoSource::maxDepth`) — both to 1.

**Tests:**
- Modify `tests/ui/test_repo_list_model.cpp`, `tests/ui/test_project_controller.cpp`, `tests/ui/test_qml_shell.cpp`, `tests/ui/test_qml_add_from_folder.cpp`, `tests/test_project_store.cpp`, `tests/test_repo_scan.cpp`.

---

## Task 1: Address every root row by path

**Files:**
- Modify: `ui/include/gittide/ui/repolistmodel.hpp`, `ui/src/repolistmodel.cpp`, `ui/include/gittide/ui/projectcontroller.hpp`, `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_repo_list_model.cpp`

**Interfaces produced:**
```cpp
bool RepoListModel::setFetchStateByPath(const QString& path, FetchState state, const QString& error = {});
// removed: setFetchState(int), setSyncCounts(int), setRepoHead(int)
```

This is a pure refactor: no behaviour changes, and the suite stays at 245/245 throughout. It exists so Task 2 cannot introduce a silent misrouting bug.

- [ ] **Step 1: Write the failing test** — in `tests/ui/test_repo_list_model.cpp`, add:

```cpp
    void fetch_state_is_addressable_by_path()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        std::vector<RepoRef> repos{
            RepoRef{.path = tmp.generic_string(), .alias = "one"},
            RepoRef{.path = (tmp / "gittide-two").generic_string(), .alias = "two"},
        };

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos(repos);

        QVERIFY(m.setFetchStateByPath(QString::fromStdString((tmp / "gittide-two").generic_string()),
                                      RepoListModel::FetchState::Running));

        // The addressed row changed; its sibling did not.
        QCOMPARE(m.data(m.index(1, 0), RepoListModel::FetchStateRole).toInt(),
                 static_cast<int>(RepoListModel::FetchState::Running));
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::FetchStateRole).toInt(),
                 static_cast<int>(RepoListModel::FetchState::Idle));
    }

    void fetch_state_by_unknown_path_is_a_no_op()
    {
        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos({RepoRef{.path = "/tmp/gittide-only", .alias = "only"}});

        QVERIFY(!m.setFetchStateByPath(QStringLiteral("/no/such/repo"), RepoListModel::FetchState::Running));
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::FetchStateRole).toInt(),
                 static_cast<int>(RepoListModel::FetchState::Idle));
    }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `no member named 'setFetchStateByPath' in 'RepoListModel'`.

- [ ] **Step 3: Add the by-path setter**

In `ui/include/gittide/ui/repolistmodel.hpp`, beside the other `*ByPath` declarations:

```cpp
    /// As setFetchState, addressing the node by its exact `path`. Returns false
    /// (and changes nothing) when the path is not in the tree.
    bool setFetchStateByPath(const QString& path, FetchState state, const QString& error = {});
```

In `ui/src/repolistmodel.cpp`, refactor the existing `setFetchState(int, …)` body into a shared `applyFetchState(Node&, FetchState, const QString&)` private helper (matching the existing `applyRepoHead` / `applySyncCounts` pattern), then:

```cpp
bool RepoListModel::setFetchStateByPath(const QString& path, FetchState state, const QString& error)
{
    Node* n = findByPath(path);
    if (!n)
        return false;
    applyFetchState(*n, state, error);
    return true;
}
```

Declare `void applyFetchState(Node& n, FetchState state, const QString& error);` next to `applyRepoHead` in the private section.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS.

- [ ] **Step 5: Migrate the poll pass**

In `ui/src/projectcontroller.cpp::pollRepos`, replace the two row-indexed calls. The loop already computes `repoPath`:

```cpp
        if (st)
            m_repoModel->setSyncCountsByPath(repoPath, st->ahead, st->behind, st->hasUpstream);
```

and:

```cpp
        if (haveHead && haveStatus)
            m_repoModel->setRepoHeadByPath(repoPath, branch, detached, shortOid, dirty);
```

- [ ] **Step 6: Migrate the fleet fetch**

`fetchOne` already receives the `RepoRef`, so it needs no row at all. Change its signature in the header and the definition:

```cpp
    QCoro::Task<void> fetchOne(gittide::RepoRef ref);
```

In the body, derive the path and display name from the ref instead of from the model, and address every model call by path:

```cpp
QCoro::Task<void> ProjectController::fetchOne(gittide::RepoRef ref)
{
    const QString path = QString::fromStdString(ref.path);
    m_repoModel->setFetchStateByPath(path, RepoListModel::FetchState::Running);

    // Alias-aware display name for any failure line (matches the tree row).
    // Taken from the ref rather than read back out of the model: root rows are
    // no longer positionally addressable once source groups exist.
    const std::filesystem::path p(ref.path);
    const std::filesystem::path base = p.has_filename() ? p.filename() : p.parent_path().filename();
    const QString name = !ref.alias.empty() ? QString::fromStdString(ref.alias)
                                            : QString::fromStdString(gittide::toGitPath(base));
```

Replace each remaining `m_repoModel->setFetchState(row, …)` with `m_repoModel->setFetchStateByPath(path, …)` and the `setSyncCounts(row, …)` with `setSyncCountsByPath(path, …)`. Add `#include "gittide/pathutil.hpp"` if it is not already included.

- [ ] **Step 7: Carry refs, not rows, through the auth retry**

`m_authFailedRows` stores indices into `activeRepos()` and re-resolves them later — the same positional coupling. In `ui/include/gittide/ui/projectcontroller.hpp` replace:

```cpp
    std::vector<int>     m_authFailedRows;             // rows that failed on auth (retried in submitFleetCredentials)
```

with:

```cpp
    /// Repos that failed on auth, retried in submitFleetCredentials. Stored as
    /// refs rather than row indices: rows are no longer positional once source
    /// groups exist, and the project's repo list may have changed meanwhile.
    std::vector<gittide::RepoRef> m_authFailedRefs;
```

Update the three uses: `m_authFailedRows.clear()` → `m_authFailedRefs.clear()`; `m_authFailedRows.push_back(row)` → `m_authFailedRefs.push_back(ref)`; and in `submitFleetCredentials`:

```cpp
    if (m_authFailedRefs.empty() || m_fetchingAll)
        return;
    …
    const std::vector<gittide::RepoRef> retry = std::move(m_authFailedRefs);
    m_authFailedRefs.clear();

    m_fetchFailed -= static_cast<int>(retry.size());

    m_fetchPending = static_cast<int>(retry.size());
    m_fetchTotal   = m_fetchPending;
    m_fetchingAll  = true;
    emit fetchingAllChanged();
    emit fetchProgressChanged();

    for (const auto& ref : retry)
        QCoro::connect(fetchOne(ref), this, [] {});
```

The `const auto& repos = activeRepos();` line in that function becomes unused — remove it. In `fetchAll`, the dispatch loop becomes:

```cpp
    for (int row : rows)
        QCoro::connect(fetchOne(repos[row]), this, [] {});
```

(`rows` still selects which repos are fetchable; it is only the *model* addressing that changes.)

- [ ] **Step 8: Remove the row-indexed setters and migrate their tests**

Delete `setFetchState(int, …)`, `setSyncCounts(int, …)` and `setRepoHead(int, …)` from both the header and `ui/src/repolistmodel.cpp` — leaving them is leaving the trap. In `tests/ui/test_repo_list_model.cpp`, rewrite the six call sites to the by-path form, keeping each assertion's intent. The two out-of-range no-crash cases (`setFetchState(5, …)`, `setRepoHead(9, …)`) become unknown-path cases:

```cpp
        QVERIFY(!m.setFetchStateByPath(QStringLiteral("/no/such/repo"), RepoListModel::FetchState::Running));
```

```cpp
        QVERIFY(!m.setRepoHeadByPath(QStringLiteral("/no/such/repo"), QStringLiteral("x"), false, QString(), 0));
```

- [ ] **Step 9: Run the full suite**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
Expected: PASS, 245/245 — this task changes no behaviour.

- [ ] **Step 10: Commit**

```bash
git add ui/include/gittide/ui/repolistmodel.hpp ui/src/repolistmodel.cpp ui/include/gittide/ui/projectcontroller.hpp ui/src/projectcontroller.cpp tests/ui/test_repo_list_model.cpp
git commit -m "refactor(ui): address repo rows by path, not row index"
```

---

## Task 2: Source group nodes in the model

**Files:**
- Modify: `ui/include/gittide/ui/repolistmodel.hpp`, `ui/src/repolistmodel.cpp`
- Test: `tests/ui/test_repo_list_model.cpp`

**Interfaces consumed:** none from Task 1 (that task only removed positional setters).

**Interfaces produced:**
```cpp
// new roles: RepoListModel::IsSourceRole, RepoCountRole, AvailableRole
void RepoListModel::setRepos(const std::vector<gittide::RepoRef>& repos,
                             const std::vector<gittide::RepoSource>& sources = {});
```

- [ ] **Step 1: Write the failing tests** — add to `tests/ui/test_repo_list_model.cpp`:

```cpp
    void sources_become_groups_holding_the_repos_beneath_them()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        const auto root = (tmp / "gittide-src").generic_string();

        std::vector<RepoRef> repos{
            RepoRef{.path = root + "/api"},
            RepoRef{.path = root + "/web"},
            RepoRef{.path = (tmp / "gittide-loose").generic_string()},
        };
        std::vector<gittide::RepoSource> sources{gittide::RepoSource{.path = root, .maxDepth = 1}};

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos(repos, sources);

        // Group first, loose repo after it.
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::IsSourceRole).toBool(), true);
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::RepoCountRole).toInt(), 2);
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::PathRole).toString(), QString::fromStdString(root));
        QCOMPARE(m.rowCount(m.index(0, 0)), 2);
        QCOMPARE(m.data(m.index(1, 0), RepoListModel::IsSourceRole).toBool(), false);
    }

    void a_repo_joins_the_deepest_containing_source()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        const auto outer = (tmp / "gittide-outer").generic_string();
        const auto inner = outer + "/team";

        std::vector<RepoRef> repos{RepoRef{.path = inner + "/api"}};
        std::vector<gittide::RepoSource> sources{
            gittide::RepoSource{.path = outer, .maxDepth = 3},
            gittide::RepoSource{.path = inner, .maxDepth = 1},
        };

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos(repos, sources);

        QCOMPARE(m.rowCount(), 2);                                   // both groups, in store order
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::RepoCountRole).toInt(), 0); // outer is empty
        QCOMPARE(m.data(m.index(1, 0), RepoListModel::RepoCountRole).toInt(), 1); // inner owns it
    }

    void a_sibling_prefix_folder_does_not_capture_a_repo()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        std::vector<RepoRef> repos{RepoRef{.path = (tmp / "gittide-projects" / "api").generic_string()}};
        std::vector<gittide::RepoSource> sources{
            gittide::RepoSource{.path = (tmp / "gittide-proj").generic_string(), .maxDepth = 1}};

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos(repos, sources);

        QCOMPARE(m.data(m.index(0, 0), RepoListModel::RepoCountRole).toInt(), 0); // group empty
        QCOMPARE(m.rowCount(), 2);                                                // repo stayed loose
        QCOMPARE(m.data(m.index(1, 0), RepoListModel::IsSourceRole).toBool(), false);
    }

    void a_source_that_is_itself_a_repo_holds_that_repo()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        const auto repo = (tmp / "gittide-self").generic_string();

        std::vector<RepoRef> repos{RepoRef{.path = repo}};
        std::vector<gittide::RepoSource> sources{gittide::RepoSource{.path = repo, .maxDepth = 1}};

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos(repos, sources);

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::RepoCountRole).toInt(), 1);
    }

    void an_empty_source_is_still_shown_and_reports_availability()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        std::vector<gittide::RepoSource> sources{
            gittide::RepoSource{.path = tmp.generic_string(), .maxDepth = 1},
            gittide::RepoSource{.path = "/no/such/folder/gittide", .maxDepth = 1},
        };

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos({}, sources);

        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::RepoCountRole).toInt(), 0);
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::AvailableRole).toBool(), true);
        QCOMPARE(m.data(m.index(1, 0), RepoListModel::AvailableRole).toBool(), false);
    }

    void no_sources_yields_todays_flat_list()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos({RepoRef{.path = tmp.generic_string(), .alias = "one"}});

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::IsSourceRole).toBool(), false);
    }
```

Add `#include "gittide/projectstore.hpp"` if the file does not already have it (it does — it uses `RepoRef`).

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build --parallel`
Expected: FAIL — `no member named 'IsSourceRole'` / no two-argument `setRepos`.

- [ ] **Step 3: Extend the node and the roles**

In `ui/include/gittide/ui/repolistmodel.hpp`, add to the `Roles` enum after `OwnerRepoPathRole`:

```cpp
        IsSourceRole,
        RepoCountRole,
        AvailableRole,
```

Add to `struct Node`, next to `isSubmodule`:

```cpp
        /// A source node is a registered folder, not a repository: it has no git
        /// state of its own and its children are the repos that live beneath it.
        bool isSource = false;
        /// Source nodes only: false when the folder no longer exists on disk.
        bool available = true;
```

Change the `setRepos` declaration:

```cpp
    /// Rebuild the top-level rows from `repos`, grouped by `sources`. Each
    /// source becomes a collapsible group node — in store order, before any
    /// ungrouped repo — holding the repositories that live beneath its folder;
    /// a repo joins the *deepest* source containing it, and repos under no
    /// source follow as ordinary top-level rows. Passing no sources yields a
    /// flat list. Does **no** git I/O: it runs on the UI thread on every project
    /// switch, so it only fills in display name, path and on-disk presence.
    /// Branch, dirty count, sync counts and the submodule subtree are hydrated
    /// afterwards, off-thread, by ProjectController's poll pass.
    void setRepos(const std::vector<gittide::RepoRef>& repos,
                  const std::vector<gittide::RepoSource>& sources = {});
```

- [ ] **Step 4: Implement the grouping**

`Node` is a private nested type, so the per-repo node builder must be a **private member function**, not a free function. Declare it in the header's private section next to `appendSubmodules`:

```cpp
    // Build one top-level repo node (display name, path, on-disk presence) from
    // a RepoRef. No git I/O — see the note on setRepos.
    std::unique_ptr<Node> makeRepoNode(const gittide::RepoRef& ref) const;
```

and move the existing per-repo body of `setRepos` into it verbatim.

Then add a file-local helper in `ui/src/repolistmodel.cpp`, next to the other anonymous-namespace helpers (create the namespace block above `setRepos` if the file has none):

```cpp
namespace {

/// True when `repoPath` is the folder `sourcePath` itself, or lies inside it.
/// The boundary test matters: "/home/u/proj" must not capture
/// "/home/u/projects/api". Both are generic (forward-slash) paths.
bool containsRepo(const QString& sourcePath, const QString& repoPath)
{
    if (sourcePath.isEmpty())
        return false;
    if (repoPath == sourcePath)
        return true; // a source registered on a folder that is itself a repo
    if (!repoPath.startsWith(sourcePath))
        return false;
    return sourcePath.endsWith(QLatin1Char('/')) || repoPath.at(sourcePath.size()) == QLatin1Char('/');
}

} // namespace
```

Rewrite `setRepos`. Extract the existing per-repo node construction into a `makeRepoNode(const gittide::RepoRef&)` static/file-local helper so the body stays readable, then:

```cpp
void RepoListModel::setRepos(const std::vector<gittide::RepoRef>& repos,
                             const std::vector<gittide::RepoSource>& sources)
{
    beginResetModel();
    m_roots.clear();

    // One group per source, in store order, before any ungrouped repo.
    std::vector<Node*> groups;
    groups.reserve(sources.size());
    for (const auto& s : sources)
    {
        const std::filesystem::path sp(s.path);
        std::error_code             ec;

        auto g = std::make_unique<Node>();
        std::filesystem::path base = sp.has_filename() ? sp.filename() : sp.parent_path().filename();
        g->displayName = base.generic_string().empty() ? QString::fromStdString(s.path)
                                                       : QString::fromStdString(base.generic_string());
        g->path      = QString::fromStdString(s.path);
        g->isSource  = true;
        g->available = std::filesystem::is_directory(sp, ec) && !ec;

        groups.push_back(g.get());
        m_roots.push_back(std::move(g));
    }

    for (const auto& r : repos)
    {
        auto node = makeRepoNode(r);

        // Deepest containing source wins, so a source nested inside another
        // takes its repos rather than both listing them.
        Node* owner = nullptr;
        for (Node* g : groups)
        {
            if (!containsRepo(g->path, node->path))
                continue;
            if (!owner || g->path.size() > owner->path.size())
                owner = g;
        }

        if (owner)
        {
            node->parent = owner;
            owner->children.push_back(std::move(node));
        }
        else
        {
            m_roots.push_back(std::move(node));
        }
    }
    endResetModel();
}
```

In `data()`, serve the three new roles (source nodes only for two of them):

```cpp
    case IsSourceRole:
        return n->isSource;
    case RepoCountRole:
        return n->isSource ? static_cast<int>(n->children.size()) : 0;
    case AvailableRole:
        return n->isSource ? n->available : true;
```

And in `roleNames()`:

```cpp
    roles[IsSourceRole]  = "isSource";
    roles[RepoCountRole] = "repoCount";
    roles[AvailableRole] = "available";
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS, including every pre-existing `TestRepoListModel` case (they pass no sources, so they see today's flat list).

- [ ] **Step 6: Add the misrouting regression test**

This is the test that would fail if Task 1 had not landed. Add to `tests/ui/test_repo_list_model.cpp`:

```cpp
    void a_group_node_does_not_shift_state_onto_the_wrong_repo()
    {
        const auto tmp  = std::filesystem::temp_directory_path();
        const auto root = (tmp / "gittide-shift").generic_string();

        std::vector<RepoRef> repos{
            RepoRef{.path = root + "/api", .alias = "api"},
            RepoRef{.path = root + "/web", .alias = "web"},
        };
        std::vector<gittide::RepoSource> sources{gittide::RepoSource{.path = root, .maxDepth = 1}};

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos(repos, sources);

        // Root row 0 is now the GROUP, not "api" — a positional update would
        // have landed here. The by-path update must reach the repo itself.
        QVERIFY(m.setSyncCountsByPath(QString::fromStdString(root + "/api"), 4, 0, true));

        const QModelIndex group = m.index(0, 0);
        QCOMPARE(m.data(m.index(0, 0, group), RepoListModel::AheadRole).toInt(), 4);
        QCOMPARE(m.data(m.index(1, 0, group), RepoListModel::AheadRole).toInt(), 0);
        QCOMPARE(m.data(group, RepoListModel::AheadRole).toInt(), 0);
    }
```

- [ ] **Step 7: Run the suite**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add ui/include/gittide/ui/repolistmodel.hpp ui/src/repolistmodel.cpp tests/ui/test_repo_list_model.cpp
git commit -m "feat(ui): group repositories under their source folder"
```

---

## Task 3: Feed the model the active project's sources

**Files:**
- Modify: `ui/src/projectcontroller.cpp`
- Test: `tests/ui/test_project_controller.cpp`

**Interfaces consumed:** `setRepos(repos, sources)` (Task 2).

- [ ] **Step 1: Write the failing test** — add to `TestProjectController`:

```cpp
    void refreshRepoModel_groups_repos_under_their_source()
    {
        const auto root = makeScanRoot({"api"});

        ProjectStore store;
        store.projects().push_back(Project{.id = "id-a", .name = "Work"});
        store.projects()[0].sources.push_back(
            gittide::RepoSource{.path = root.generic_string(), .maxDepth = 1});
        store.projects()[0].repos.push_back(
            RepoRef{.path = (root / "api").generic_string()});

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        // One root row — the source group — with the repo nested inside it.
        QCOMPARE(controller.repos()->rowCount(), 1);
        const QModelIndex group = controller.repos()->index(0, 0);
        QCOMPARE(controller.repos()->data(group, RepoListModel::IsSourceRole).toBool(), true);
        QCOMPARE(controller.repos()->rowCount(group), 1);
    }
```

Add `#include "gittide/ui/repolistmodel.hpp"` if it is not already included (it is).

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — `rowCount()` is 1 but the row is the repo, so `IsSourceRole` is false.

- [ ] **Step 3: Pass sources at all three call sites**

In `ui/src/projectcontroller.cpp`, the three places that call `setRepos` with a project's repos become:

```cpp
            m_repoModel->setRepos(p.repos, p.sources);
```

(one in `refreshRepoModel`, one in `activate`). The two `setRepos({})` calls that clear the model stay as they are — an empty project has no groups either.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
Expected: PASS, 245/245 + the new cases.

- [ ] **Step 5: Commit**

```bash
git add ui/src/projectcontroller.cpp tests/ui/test_project_controller.cpp
git commit -m "feat(ui): show the active project's sources in the repo tree"
```

---

## Task 4: The source row in the sidebar

**Files:**
- Modify: `ui/qml/Sidebar.qml`
- Test: `tests/ui/test_qml_shell.cpp`

**Interfaces consumed:** `isSource`, `repoCount`, `available` roles (Task 2).

**Interfaces produced:** `Q_INVOKABLE bool RepoListModel::isSourceRow(int row) const` (added in Step 4, so QML can expand source rows without a numeric role literal).

- [ ] **Step 1: Write the failing test** — add a slot to `TestQmlShell`, following the setup of the neighbouring slots (they build a `ProjectStore`, a `ProjectController`, and load `Main.qml` via `installQmlContext`):

```cpp
    void sidebar_shows_a_source_row_with_its_repo_count()
    {
        // A project whose single repo lives under a registered source folder.
        const auto root = std::filesystem::temp_directory_path() / "gittide-qml-src";
        std::filesystem::create_directories(root / "api");

        gittide::ProjectStore store;
        gittide::Project p{.id = "id-a", .name = "Work"};
        p.sources.push_back(gittide::RepoSource{.path = root.generic_string(), .maxDepth = 1});
        p.repos.push_back(gittide::RepoRef{.path = (root / "api").generic_string()});
        store.projects().push_back(std::move(p));

        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QQmlApplicationEngine engine;
        installQmlContext(engine, &controller);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        auto* model = controller.repos();
        QCOMPARE(model->rowCount(), 1);
        const QModelIndex group = model->index(0, 0);
        QCOMPARE(model->data(group, RepoListModel::IsSourceRole).toBool(), true);
        QCOMPARE(model->data(group, RepoListModel::RepoCountRole).toInt(), 1);

        // The sidebar must offer a menu for it — the repo menu is not right here.
        QObject* root_ = engine.rootObjects().first();
        QVERIFY(root_->findChild<QObject*>(QStringLiteral("sourceContextMenu")) != nullptr);

        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
```

> **Note for the implementer:** copy the context-property setup verbatim from the neighbouring `TestQmlShell` slot that loads `Main.qml`; any context property it needs must be provided here too or the load emits warnings.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — `sourceContextMenu` not found (Task 5 adds it; this step only pins the model wiring, and the menu assertion is what still fails).

- [ ] **Step 3: Render the source row**

In `ui/qml/Sidebar.qml`'s `TreeViewDelegate`, add next to the existing `isSub` property:

```qml
                readonly property bool isSource: model.isSource === true
```

Make the row height single-line for a source (it has no branch line):

```qml
                implicitHeight: (row.uninit || row.isSource) ? 30 : 46
```

`activate()` must not try to open a source — there is no repository behind it:

```qml
                function activate() {
                    // A source row is a folder, not a repo: activating it toggles
                    // the group rather than opening anything.
                    if (row.isSource) {
                        repoTree.toggleExpanded(row.row)
                        return
                    }
                    if (!repoVm || row.uninit)
                        return
                    repoVm.open(model.repoPath)
                    if (row.hasChildren)
                        repoTree.expand(row.row)
                }
```

In the delegate's `contentItem`, the existing name/branch `ColumnLayout` gains a source variant. Keep the chevron block unchanged; for a source render one line plus a trailing count:

```qml
                    // Source group: folder name, then either its repo count or a
                    // "folder not found" note when the folder has gone.
                    Label {
                        visible: row.isSource
                        Layout.fillWidth: true
                        text: model.display
                        color: theme.textPrimary
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                        ToolTip.visible: sourceHover.hovered
                        ToolTip.text: model.repoPath
                        HoverHandler { id: sourceHover }
                    }
                    Label {
                        visible: row.isSource
                        text: model.available ? model.repoCount : "folder not found"
                        color: model.available ? theme.textMuted : theme.stateDeleted
                        font.pixelSize: 11
                    }
```

and give the existing repo `ColumnLayout` `visible: !row.isSource` so the two variants never both render.

- [ ] **Step 4: Expand source groups after a reset**

Source rows exist to be looked into, so expand them when the model repopulates. Add to the `TreeView`:

```qml
            // Expansion is view state, so it is driven here rather than by the
            // model: a registered folder is expanded on load because seeing its
            // repos is the point of grouping. Repos and submodules keep their
            // collapsed default.
            Connections {
                target: repoTree.model
                function onModelReset() {
                    for (var r = 0; r < repoTree.rows; ++r)
                        if (repoTree.model.isSourceRow(r))
                            repoTree.expand(r)
                }
            }
```

Reaching the role from QML would mean a numeric literal (`Qt::UserRole + n`) that silently breaks the day someone reorders the enum, so expose an intention-revealing accessor on the model instead. In `ui/include/gittide/ui/repolistmodel.hpp`:

```cpp
    /// True when top-level row `row` is a source group. Lets QML expand source
    /// rows on a model reset without hard-coding a numeric role value.
    Q_INVOKABLE bool isSourceRow(int row) const;
```

and in `ui/src/repolistmodel.cpp`:

```cpp
bool RepoListModel::isSourceRow(int row) const
{
    return row >= 0 && row < static_cast<int>(m_roots.size()) && m_roots[row]->isSource;
}
```

Cover it in `tests/ui/test_repo_list_model.cpp` alongside the grouping cases: true for a group row, false for a loose repo row, false for an out-of-range row.

- [ ] **Step 5: Run the suite**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
Expected: the new slot still fails only on the `sourceContextMenu` assertion; everything else passes.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/Sidebar.qml tests/ui/test_qml_shell.cpp
git commit -m "feat(ui): render repository sources as sidebar groups"
```

---

## Task 5: Source context menu

**Files:**
- Create: `ui/qml/SourceContextMenu.qml`
- Modify: `ui/qml/qml.qrc`, `ui/qml/Sidebar.qml`
- Test: `tests/ui/test_qml_shell.cpp` (the slot from Task 4 goes green here)

**Interfaces consumed:** `projectController.rescanSources()`, `clearIgnoredForSource(path)`, `removeSource(path)` — all shipped.

- [ ] **Step 1: Write the menu**

`ui/qml/SourceContextMenu.qml`, modelled on `RepoContextMenu.qml`:

```qml
import QtQuick
import QtQuick.Controls.Basic

// Right-click context menu for a repository-source (folder) row in the Sidebar.
// Mirrors the Sources section of the Project Options dialog so the common
// actions do not require opening a dialog.
AppMenu {
    id: menu
    objectName: "sourceContextMenu"

    property string sourcePath: ""

    signal rescanRequested()
    signal clearIgnoredRequested()
    signal removeRequested()

    AppMenuItem {
        text: "Rescan now"
        onTriggered: menu.rescanRequested()
    }
    AppMenuItem {
        text: "Clear ignored"
        onTriggered: menu.clearIgnoredRequested()
    }

    AppMenuSeparator {}

    AppMenuItem {
        text: "Remove source"
        destructive: true
        onTriggered: menu.removeRequested()
    }
}
```

Register it in `ui/qml/qml.qrc` next to `RepoContextMenu.qml`:

```xml
    <file>SourceContextMenu.qml</file>
```

- [ ] **Step 2: Wire it up**

In `ui/qml/Sidebar.qml`, host the menu next to `repoContextMenu`:

```qml
    SourceContextMenu {
        id: sourceContextMenu
        onRescanRequested:      if (projectController) projectController.rescanSources()
        onClearIgnoredRequested: if (projectController && sourceContextMenu.sourcePath.length > 0)
                                    projectController.clearIgnoredForSource(sourceContextMenu.sourcePath)
        onRemoveRequested:      if (projectController && sourceContextMenu.sourcePath.length > 0)
                                    projectController.removeSource(sourceContextMenu.sourcePath)
    }
```

and route right-clicks in the delegate's `MouseArea` (the block with `acceptedButtons: Qt.RightButton`) — a source row opens the source menu, everything else keeps today's behaviour:

```qml
                        if (row.isSource) {
                            sourceContextMenu.sourcePath = model.repoPath
                            sourceContextMenu.popup()
                        } else if (row.isSub) {
```

- [ ] **Step 3: Run the test to verify it now passes**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: PASS — `sidebar_shows_a_source_row_with_its_repo_count` is now fully green.

- [ ] **Step 4: Verify by hand**

Run the app, register a folder as a source, confirm the sidebar shows the folder with its repos nested and expanded, right-click it and confirm all three actions work: *Rescan now* picks up a repo cloned into the folder, *Clear ignored* followed by a rescan brings a removed repo back, and *Remove source* drops the group while leaving its repositories in the project.

- [ ] **Step 5: Commit**

```bash
git add ui/qml/SourceContextMenu.qml ui/qml/qml.qrc ui/qml/Sidebar.qml
git commit -m "feat(ui): context menu for a repository source row"
```

---

## Task 6: Dialog stepper and defaults

**Files:**
- Modify: `ui/qml/AddFromFolderDialog.qml`, `core/include/gittide/reposcan.hpp`, `core/include/gittide/projectstore.hpp`
- Test: `tests/ui/test_qml_add_from_folder.cpp`, `tests/test_repo_scan.cpp`, `tests/test_project_store.cpp`

- [ ] **Step 1: Write the failing test** — add to `TestQmlAddFromFolder`:

```cpp
    void dialog_opens_at_depth_one_with_keep_as_source_checked()
    {
        gittide::ProjectStore store;
        store.projects().push_back(gittide::Project{.id = "id-a", .name = "Work"});
        ProjectController controller(&store);
        controller.activate(QStringLiteral("id-a"));

        QQmlApplicationEngine engine;
        installQmlContext(engine, &controller);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());

        QObject* root = engine.rootObjects().first();
        QObject* dlg  = root->findChild<QObject*>(QStringLiteral("addFromFolderDialog"));
        QVERIFY(dlg);
        QMetaObject::invokeMethod(dlg, "openDialog");

        QObject* depth = root->findChild<QObject*>(QStringLiteral("addFromFolderDepth"));
        QVERIFY(depth);
        QCOMPARE(depth->property("value").toInt(), 1);

        QObject* keep = root->findChild<QObject*>(QStringLiteral("addFromFolderKeepSource"));
        QVERIFY(keep);
        QCOMPARE(keep->property("checked").toBool(), true);
    }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests`
Expected: FAIL — value is 2, checked is false.

- [ ] **Step 3: Change the defaults**

In `ui/qml/AddFromFolderDialog.qml`, the `SpinBox` gets `value: 1`, and `openDialog()`'s `keepSource.checked = false` becomes:

```qml
        // A folder the user points at is usually somewhere repos keep appearing,
        // so registering it as a source is the default, not the opt-in.
        keepSource.checked = true
```

In `core/include/gittide/reposcan.hpp` and `core/include/gittide/projectstore.hpp`, both `maxDepth` defaults become `1`, and their Doxygen comments say `1 = direct children only` where they currently say the default is 2. Update the two core test expectations that assert the old default: the `[scan]` case relying on the default argument, and the `[store][sources]` malformed-entry case asserting `maxDepth == 2` — change it to `1`, since it verifies the *default*, not the number.

- [ ] **Step 4: Theme the stepper**

Replace the `SpinBox`'s stock indicators. Keep the existing `contentItem` and `background`, and add:

```qml
                // The Basic style's default indicators are unthemed and overlap a
                // custom background — draw both from theme tokens instead.
                component Step: Rectangle {
                    property string glyph: ""
                    property bool   armed: false
                    implicitWidth: 28
                    implicitHeight: 28
                    radius: 6
                    color: armed ? theme.surfaceRaised : "transparent"
                    border.color: theme.border
                    border.width: 1
                    Label {
                        anchors.centerIn: parent
                        text: parent.glyph
                        color: depthBox.enabled ? (parent.armed ? theme.accent : theme.textSecondary)
                                                : theme.textMuted
                        font.pixelSize: 13
                    }
                }

                up.indicator: Step {
                    x: depthBox.width - width
                    height: depthBox.height
                    glyph: "+"
                    armed: depthBox.up.hovered
                }
                down.indicator: Step {
                    x: 0
                    height: depthBox.height
                    glyph: "−"
                    armed: depthBox.down.hovered
                }
```

and give the `background` `implicitWidth: 120` so the value sits between the two cells rather than under them. Verify by eye that at every value 1–5 the digit is centred and neither indicator overlaps it.

- [ ] **Step 5: Run the suite**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Verify by hand**

Run the app, open *Add repositories from folder…*, and confirm: the stepper renders as one control with themed − / + cells, the depth starts at 1, "keep this folder as a source" starts ticked, and the Add button becomes enabled as soon as a folder is chosen.

- [ ] **Step 7: Commit**

```bash
git add ui/qml/AddFromFolderDialog.qml core/include/gittide/reposcan.hpp core/include/gittide/projectstore.hpp tests/ui/test_qml_add_from_folder.cpp tests/test_repo_scan.cpp tests/test_project_store.cpp
git commit -m "fix(ui): theme the depth stepper, default depth 1, source by default"
```

---

## Task 7: Close-out

**Files:**
- Modify: `docs/spec/product/product.md`, `docs/spec/design/design.md`, `docs/spec/engineering/engineering.md`, `docs/plans/index.md`, this plan

- [ ] **Step 1: Run the whole suite**

Run: `cmake --build build --parallel && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`
Expected: green. Do not proceed until it is.

- [ ] **Step 2: Update the living spec**

- `spec/product`: a registered source appears in the repository list as a group holding its repositories; the source row's actions; the new defaults (depth 1, keep-as-source on).
- `spec/design`: the source row (single line, count, unavailable state) and the themed depth stepper; correct the depth default where the dialog is described.
- `spec/engineering`: grouping is derived from source paths, not stored — `projects.json` stays flat — and the invariant that **no caller addresses a root row by position**, with the reason (group nodes make row index and repo index diverge).

- [ ] **Step 3: Register the plan**

Add a row for this plan to `docs/plans/index.md`, following the conventions of the existing rows.

- [ ] **Step 4: Fill in this plan's Outcome and flip its Status to `done`.**

- [ ] **Step 5: Commit**

```bash
git add docs
git commit -m "docs: source groups in the repository list"
```

---

## Outcome

> Fill in when the plan reaches `done`.
>
> - Shipped: <summary>.
> - Spec updated: <which `spec/` sections now describe this>.
> - Code: <the main files/types that resulted>.
