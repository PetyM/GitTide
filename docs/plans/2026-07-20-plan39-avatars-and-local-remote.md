# Plan 39 — Author avatars + local-only vs pushed commits

| | |
|--|--|
| **Date** | 2026-07-20 |
| **Status** | `done` |
| **Spec** | `spec/engineering/engineering.md §Author avatars`, `§Local-only vs pushed commits`; `spec/product/product.md §History tab`; `spec/design/design.md §QML History view` |
| **Depends on** | Plan 5 (history/graph), Plan 36–38 (identity/forge/keychain) |

**Goal:** Show each commit author's real avatar (Gravatar over an instant initials
placeholder) in History, and mark which commits are local-only (not yet pushed) vs
already on a remote.

**Architecture:** Two independent features. **A (avatars)** is `ui/`-only bar one
`core/` field: `CommitNode.email` flows to an `authorEmail` role; a new
`AvatarService` (mem+disk cache, Gravatar fetch, injectable NAM/dir/base, network
toggle) is bridged to QML by an async `image://avatar/<md5>` provider; `Avatar.qml`
overlays the network image on the initials disc. **B (local/remote)** adds one core
primitive `GitRepo::localOnlyOids()` (revwalk: push HEAD, hide `refs/remotes/*`),
bridged through `AsyncRepo` → `RepoController::localOnlyOidsReady` → `RepoViewModel`
→ a `HistoryListModel::isLocalOnly` role, cued by a row badge + dim (History) and a
hollow `GraphColumn` dot (Graph).

**Tech stack:** Qt6 Network + QCoro signal-await (as `ForgeClient`),
`QQuickAsyncImageProvider`, libgit2 revwalk, Catch2 / Qt Test.

## Global constraints

- No Qt in `core/`; core returns `Expected<T>`; avatars never touch `core/`.
- New `ui/` sources → `ui/CMakeLists.txt`; tests → the `gittide_ui_tests` /
  `gittide_core_tests` lists in `tests/CMakeLists.txt` (UI tests also wired into
  `tests/ui/main.cpp`). Never signal state by colour alone (D19).

---

## Feature A — Author avatars

- [x] **A1 — `CommitNode.email`.** `core/graph.hpp` + populate in `nodeFromCommit`
  (`gitrepo.cpp`). Test `tests/core/test_commit_email.cpp`.
- [x] **A2 — `authorEmail` role** on `HistoryListModel`. Test in `test_qml_history.cpp`.
- [x] **A3 — `AvatarService`** (`ui/src/avatarservice.cpp`): mem→disk→Gravatar chain,
  TTL + negative cache, `networkEnabled`, injectable NAM/cacheDir/gravatarBase,
  `emailHash`. Test `tests/ui/test_avatar_service.cpp` (fake HTTP + injected NAM).
- [x] **A4 — `AvatarImageProvider`** (`QQuickAsyncImageProvider`), hopping the fetch
  onto the service thread and deferring `finished()`. Tested in the same file.
- [x] **A5 — Register** the provider + `avatarService` context prop in
  `installQmlContext`; construct the service in `qml_main.cpp` (default-on). Tests
  in `test_qml_shell.cpp`.
- [x] **A6 — `Avatar.qml`** gains `email` + an `Image` layer over initials; bound in
  `HistoryPane.qml` / `GraphPane.qml`.

## Feature B — Local-only vs pushed commits

- [x] **B1 — `GitRepo::localOnlyOids()`** (revwalk hide `refs/remotes/*`). Test
  `tests/core/test_git_repo_local_only.cpp`.
- [x] **B2 — `AsyncRepo::localOnlyOids()`.** Test in `test_async_repo.cpp`.
- [x] **B3 — `RepoController`**: `localOnlyOidsReady` + `refreshLocalOnly`, emitted
  from `refreshHistory` and after fetch/push. Test in `test_repo_controller.cpp`.
- [x] **B4 — `isLocalOnly` role** + `setLocalOnlyOids`; `RepoViewModel::onLocalOnly`.
  Tests in `test_qml_history.cpp` + `test_repo_view_model.cpp`.
- [x] **B5 — `GraphColumn.localOnly`** hollow dot. Test in `test_qml_history.cpp`.
- [x] **B6 — QML**: `HistoryPane` badge + dim; `GraphPane` hollow-dot binding.

---

## Outcome

Both features shipped and green (201/201 ctest). Avatars: `CommitNode.email` →
`authorEmail` role → `AvatarService` (`ui/src/avatarservice.cpp`) + async provider
(`ui/src/avatarimageprovider.cpp`), wired via `installQmlContext` and `qml_main.cpp`;
`Avatar.qml` shows the network image over initials. Source is Gravatar-only for v1
with network-on by default (**D52**); forge sources remain an ordered follow-up.
Local/remote: `GitRepo::localOnlyOids()` → `AsyncRepo` → `RepoController`
(`localOnlyOidsReady`/`refreshLocalOnly`, on the refresh + sync cascades) →
`HistoryListModel::isLocalOnly` → row badge + dim + hollow graph dot (**D53**). The
avatar wish is shipped ([`wishlist/shipped/author-avatars.md`](../wishlist/shipped/author-avatars.md));
the new local/remote wish too
([`wishlist/shipped/local-vs-remote-commits.md`](../wishlist/shipped/local-vs-remote-commits.md)).
