# Branch management — design (local-only)

| | |
|--|--|
| **Date** | 2026-06-18 |
| **Status** | approved |
| **Wish** | [`docs/wishlist/branch-management.md`](../../wishlist/branch-management.md) |
| **Scope** | local branches only: list · create · switch · delete · rename, plus detached-commit checkout |

## Summary

Let the user work with a repo's **local branches** from inside GitTide: see the
current branch and the local-branch list, switch (checkout), create (from `HEAD`
or a selected commit), delete (with not-fully-merged guard), and rename — without
dropping to a terminal. Checking out a bare commit (detached `HEAD`) is supported.

Deliberately **local-only**: no fetch/push, no tracking-branch setup, no merge.
Fits the no-network milestone (Decision D13).

## Decisions taken during brainstorming

- **Checkout safety: stash + auto-pop.** On a dirty switch, stash the working
  changes, checkout the target, then immediately pop the stash onto the target so
  the changes "follow" the user. A pop conflict is the only non-clean exit:
  stop, keep the stash, report. Never clobber silently.
  - Rejects: clean-tree-only (blocks the common case), block-with-message,
    leave-stash-parked (no stash-list UI → changes get lost). Log in `decisions.md`.
  - Note: this uses libgit2 `git_stash_*` **internally**; it does **not** add a
    user-facing stash feature (still out of scope).
- **UI: header picker + graph context menu** (both). A persistent current-branch
  picker in a repo header bar, plus commit-context actions on the History graph.
- **Detached HEAD: supported.** The graph offers both "New branch from here" and
  "Checkout this commit"; the header renders a detached state.

## Architecture

`app → ui → core → libgit2`, dependencies downward only. Branch enumeration and
mutation are pure git operations → **`core/`** on `GitRepo`, returning
`Expected<T>`. Surfaced async through `AsyncRepo` (QtConcurrent + per-repo mutex)
and `RepoController` signals. UI in Qt Widgets, colours from theme tokens only.

### 1. Core layer (`core/`, pure C++23, no Qt)

New data types — `core/include/gittide/branchinfo.hpp`:

```cpp
struct BranchInfo {
    std::string name;        // short name, e.g. "main"
    bool isHead = false;     // currently checked out
};
struct HeadState {
    std::string branch;      // branch name, empty if detached
    std::string oid;         // current HEAD commit (full hex)
    bool detached = false;
};
```

New `GitRepo` methods — all `Expected<T>`, libgit2 `git_branch_*` /
`git_checkout_tree` / `git_repository_set_head` / `git_repository_set_head_detached`
/ `git_stash_*`. No git command strings; paths via `generic_u8string()`.

```cpp
Expected<std::vector<BranchInfo>> branches() const;   // git_branch_iterator, LOCAL only
Expected<HeadState>               head() const;        // resolve HEAD + detached flag
Expected<void> createBranch(std::string name, std::string fromOid /*empty=HEAD*/, bool checkout);
Expected<void> checkoutBranch(std::string name);      // safe-switch (see §4)
Expected<void> checkoutCommit(std::string oid);       // detached, safe-switch
Expected<void> deleteBranch(std::string name, bool force); // force = delete unmerged
Expected<void> renameBranch(std::string oldName, std::string newName, bool force);
```

Name validation via `git_branch_name_is_valid` before any mutate →
`GitError{-1, "invalid branch name"}`.

### 2. Async + ViewModel (`ui/`)

`AsyncRepo` — one coroutine wrapper per core op (args **by value**, per-repo mutex
held in the worker lambda):

```cpp
QCoro::Task<Expected<std::vector<BranchInfo>>> branches();
QCoro::Task<Expected<HeadState>>               head();
QCoro::Task<Expected<void>> createBranch(QString name, QString fromOid, bool checkout);
QCoro::Task<Expected<void>> checkoutBranch(QString name);
QCoro::Task<Expected<void>> checkoutCommit(QString oid);
QCoro::Task<Expected<void>> deleteBranch(QString name, bool force);
QCoro::Task<Expected<void>> renameBranch(QString oldName, QString newName, bool force);
```

`RepoController` — coroutine slots + signals:

```cpp
public slots:
  QCoro::Task<void> refreshBranches();   // emits branchesChanged + headChanged
  QCoro::Task<void> createBranch(QString name, QString fromOid, bool checkout);
  QCoro::Task<void> switchBranch(QString name);
  QCoro::Task<void> checkoutCommit(QString oid);
  QCoro::Task<void> deleteBranch(QString name, bool force);
  QCoro::Task<void> renameBranch(QString oldName, QString newName);
signals:
  void branchesChanged(std::vector<gittide::BranchInfo>);
  void headChanged(gittide::HeadState);  // drives header: branch name OR "detached @ abc1234"
  // operationFailed reused → status bar
```

**Refresh cascade.** A successful switch / checkout / create-with-checkout fires the
existing cascade scoped to the repo: `refreshStatus()` + `refreshHistory()` +
`refreshBranches()`. Delete / rename (HEAD unchanged) → `refreshBranches()` only.
Repo open also fires `refreshBranches()`.

### 3. UI surface

**Repo header bar** — new `BranchBar` widget above the tab widget (central stack
index 2):

- Current-branch button: label = branch name, or `detached @ abc1234` paired with
  an icon (never colour alone, D19). Click → dropdown.
- Dropdown: local-branch list (current marked with `accent` + check icon), row
  click = switch; footer rows "New branch…", "Rename current…", "Delete…".
- Tokens: `surface.raised` dropdown, `border` outline, `accent` selection.

**History graph context menu** — `HistoryView` QTableView gains
`Qt::CustomContextMenu`:

- Right-click commit → "New branch from here…", "Checkout this commit" (detached).
  OID read from `HistoryModel` via `GraphRowRole`.

**Dialogs** — token-styled (`surface.raised` card, `border`, `accent` focus):

- New-branch: name field + "switch to it" checkbox; live name validation.
- Rename: prefilled name field.
- Delete confirm: if unmerged, a second-step "delete anyway (not fully merged)"
  sets `force`.

### 4. Safe-switch flow (stash + auto-pop)

Both `checkoutBranch` and `checkoutCommit` route through one internal core helper
`safeSwitch(setHeadFn)`:

```
1. status() → tree dirty? (any Wt* or Index* flag)
2. clean  → checkout target (GIT_CHECKOUT_SAFE) → set HEAD → done.
3. dirty  → git_stash_save(INCLUDE_UNTRACKED)
          → checkout target + set HEAD
          → git_stash_pop(index 0)
               ├─ pop clean    → done (changes followed to target)
               └─ pop CONFLICT → STOP. Return GitError{-1,
                    "Switched to X, but your changes conflict and are kept in stash"}.
                    HEAD already moved; stash retained (not dropped). Manual resolve.
```

`GIT_CHECKOUT_SAFE` (not FORCE): step 2 still refuses on an edge race — defensive.
The pop conflict is the single non-clean exit; surfaced via `operationFailed` →
status bar.

### 5. Edge cases

| Case | Handling |
|--|--|
| Invalid name | `git_branch_name_is_valid` pre-check → error, dialog stays open |
| Duplicate branch name | libgit2 `GIT_EEXISTS` → mapped message |
| Delete current branch | Block: `GitError{-1,"cannot delete the current branch"}` |
| Delete unmerged | libgit2 refuses without force → UI "delete anyway" → `force=true` |
| Rename to existing | refuse unless `force` (UI: plain error, no force path) |
| Unborn HEAD (no commits) | `branches()` empty; `head()` reports unborn; create-from-HEAD disabled |
| Already detached | `head().detached=true` → header detached; switching to a branch re-attaches |
| Switch to current branch | no-op, skip cascade |

### 6. Testing (TDD, test-first)

**Core (Catch2 + `TempRepo`):**

- create branch (from HEAD / from oid); list shows it; `isHead` correct
- switch clean tree; switch dirty → changes follow (stash+pop); switch dirty →
  pop-conflict returns error + stash kept
- checkout commit → detached `head()`; re-attach by switching to a branch
- delete merged ok; delete current blocked; delete unmerged needs force
- rename; invalid name rejected; duplicate rejected
- `TempRepo` likely needs a `createBranch` / make-unmerged helper

**UI (QTest + QSignalSpy + `QCoro::waitFor`):**

- `RepoController::refreshBranches` emits `branchesChanged` + `headChanged`
- switch emits the cascade (status + history + branches)
- `BranchBar` renders current-branch / detached label
- `HistoryView` context menu emits create / checkout signals with the right OID

## Spec sync (on build)

- `spec/product/` — branch flows + safe-switch policy
- `spec/engineering/` — new core ops, `AsyncRepo`/`RepoController` additions, cascade
- `spec/design/` — `BranchBar`, dialogs, detached-HEAD affordance
- `decisions.md` — stash-and-switch checkout policy (rejected alternatives logged)

## Out of scope (resist)

Remote/tracking branches, branch-from-remote, merge, rebase, a user-facing stash
feature, and bulk multi-repo branch ops. Each is its own later wish.
