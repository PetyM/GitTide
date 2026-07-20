# Plan 36 — Credentials & identity management (Phase 1: identity)

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-06 |
| **Status** | `done` (per-project *UI* assignment deferred to Plan 38) |
| **Spec** | `spec/product/product.md §Identity & credentials`, `spec/engineering/engineering.md §Network operations & credentials` |
| **Depends on** | Plan 11 (fleet), the sync/credentials work in `network-sync` |

**Goal:** Let a user manage named git identities (name/email) and assign one
globally, per-project, or per-repo — so commits are made as the right person
without dropping to the CLI. Phase 1 of the larger credentials-management feature
(keychain secrets, SSH keys, and forge tokens follow in Plans 37–38).

**Architecture:** A new metadata-only JSON store `credentials.json`
(`core/CredentialsStore`, mirroring `ProjectStore`) holds identities and the
global/project/repo assignments; a pure resolver picks the effective identity.
`GitRepo` gains primitives that **materialize** that identity into git config
(global → `~/.gitconfig`; per-repo/project → local `.git/config`) guarded by a
`gittide.identity` ownership marker so GitTide never clobbers CLI-set config
(**D49**). A ui `CredentialManager` owns the store, resolves the priority order
from `ProjectStore`, and applies the identity on assignment / on repo open.

**Tech stack:** C++23 core (libgit2 `git_config_*`, nlohmann/json), Qt Quick +
QCoro ui. No keychain, no network in this phase.

## Global constraints

- Invariants in `spec/engineering/engineering.md`: no Qt in `core/`; libgit2 &
  nlohmann PRIVATE to core; errors are `Expected<T>`; paths via
  `generic_u8string()`.
- New `core/` sources → `core/CMakeLists.txt`; new `ui/` sources →
  `ui/CMakeLists.txt`; new tests → `tests/CMakeLists.txt`.
- Never overwrite/clear local `user.*` lacking the `gittide.identity` marker.

---

## Task 1: CredentialsStore + resolver (core) ✅

**Files:** `core/include/gittide/credentialsstore.hpp`,
`core/src/credentialsstore.cpp`, `tests/test_credentials_store.cpp`,
`core/CMakeLists.txt`, `tests/CMakeLists.txt`.

**Interfaces:** `struct GitIdentity/SshKeyRef/HostAccount`; `CredentialsStore`
(versioned, atomic `save`, corrupt-recovery `load`, CRUD, assignments,
`resolveIdentity(repoPath, span<candidateProjectIds>)`).

- [x] **Step 1: failing test** — `test_credentials_store.cpp`: JSON round-trip,
  version, corrupt→`.corrupt`, atomic save, CRUD, no-secret-persisted, resolution
  precedence (override > active-project default > other-project default > global >
  none) + multi-project determinism.
- [x] **Step 2: implement** — mirror `ProjectStore` exactly; `std::deque` backing
  so `add*()` references stay valid.
- [x] **Step 3: verify** — 13 cases / 50 assertions green.

## Task 2: GitRepo identity primitives (core) ✅

**Files:** `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`,
`tests/test_git_repo_identity.cpp`, `tests/CMakeLists.txt`.

**Interfaces:** `setLocalIdentity(name,email,marker)`, `clearLocalIdentity()`,
`setGlobalIdentity(name,email)`, `effectiveIdentity()`, `localIdentity()`; structs
`ConfigIdentity`, `LocalIdentityInfo`.

- [x] **Step 1: failing test** — local write + marker read via
  `git_signature_default`; commit records materialized author; clear falls back;
  CLI-set config reported unmanaged; global write creates `~/.gitconfig` (scoped
  temp global search path to keep test isolation intact).
- [x] **Step 2: implement** — `git_config_*` on the repo config (local writes),
  `git_config_open_level(LOCAL)` for local-only read/clear, `git_config_find_global`
  + search-path fallback + `git_config_open_ondisk` for global.
- [x] **Step 3: verify** — 5 cases / 43 assertions green; full core suite clean
  (one pre-existing macOS `/private/var` submodule-path failure, unrelated).

## Task 3: AsyncRepo wrappers (ui) — identity ops off the UI thread

**Files:** `ui/include/gittide/ui/asyncrepo.hpp` (+`.cpp`), test in
`tests/ui/test_async_repo.cpp`.

- [x] Wrapped `setLocalIdentity` / `clearLocalIdentity` / `localIdentity` /
  `effectiveIdentity` as `QCoro::Task`, mirroring `setPullStrategy`.
  `setGlobalIdentity` was made **static** on `GitRepo` (repo-independent) and is
  called directly from `CredentialManager` — no AsyncRepo wrapper needed.
  Exercised via `test_credential_manager.cpp` (below).

## Task 4: CredentialManager + IdentityListModel (ui)

**Files:** `ui/include/gittide/ui/credentialmanager.hpp` +
`ui/src/credentialmanager.cpp`, `ui/include/gittide/ui/identitylistmodel.hpp` +
`ui/src/identitylistmodel.cpp`, `ui/CMakeLists.txt`,
`tests/ui/test_credential_manager.cpp`, `tests/ui/test_identity_list_model.cpp`,
`tests/CMakeLists.txt`.

**Interfaces:** `CredentialManager` owns `CredentialsStore` (+ `credentials.json`
path) and a `ProjectStore&` (for candidate project ids in priority order —
active project first). Q_INVOKABLE CRUD (add/update/remove identity, set global,
set project default, set repo override) that mutate → `save()` → emit change;
`applyIdentityToRepo(repoPath)` (resolve → open via AsyncRepo → compare local vs
global → `setLocalIdentity` or `clearLocalIdentity`, marker-guarded);
`effectiveIdentityFor(repoPath)` for display. `IdentityListModel` exposes id /
name / email / isGlobal roles.

- [x] `test_credential_manager.cpp`: assign override → `applyIdentityToRepo`
  writes local `user.*` + marker; **never-clobber-unmarked** (CLI-set identity is
  left untouched); clearing the override re-clears the managed local identity.
  `test_identity_list_model.cpp`: roles + refresh. Both green (5 cases).
- [x] `CredentialManager` + `IdentityListModel` implemented and registered in
  `ui/CMakeLists.txt`.

## Task 5: Wiring + apply-on-open ✅

**Files:** `app/qml_main.cpp`, `ui/src/qmlcontext.cpp` (+header).

- [x] Construct `CredentialsStore` + `CredentialManager` in `qml_main.cpp`
  (`credentials.json` under `QStandardPaths::AppConfigLocation`, next to
  `projects.json`); expose `credentialManager` + `identityModel` context
  properties via `installQmlContext`.
- [x] Apply-on-open, decoupled: `CredentialManager::onActiveRepoChanged` is
  connected to `RepoViewModel::changed` (reads `repoPath`, de-duplicated), so the
  active repo's identity is materialized without coupling `RepoController` to
  credentials.
- [x] Shell test still loads Main.qml without errors.

## Task 6: Identity settings UI ✅

**Files:** `ui/qml/IdentityDialog.qml` (new), `ui/qml/OptionsDialog.qml`,
`ui/qml/Main.qml`, `ui/qml/qml.qrc`.

- [x] `IdentityDialog.qml`: lists identities (name/email, Global + This-repo
  chips), add identity, **Set global**, **Use here** (per-repo override), and
  **use global default**; reuses `OverlayCard`/`AppButton` (no new token).
  Opened from an OptionsDialog "Manage identities…" button via a new
  `identityRequested()` signal.
- [x] Loads clean in the headless shell test.
- [ ] (Deferred) per-project default assignment UI + a dedicated shell test for
  the dialog's object names — Phase 3's central UI absorbs this.

---

## Outcome

- **Shipped:** named git-identity management. Users manage identities and assign a
  **global** one (→ `~/.gitconfig`) or a **per-repo override** (→ local
  `.git/config`) from an Identity dialog; the resolved identity is materialized
  into git config on assignment and on repo open, guarded by a `gittide.identity`
  ownership marker so CLI-set config is never clobbered (**D49**). Per-project
  defaults are fully supported by the engine (`resolveIdentity` + apply-on-open);
  only the *UI to assign them* is deferred to the central credentials dialog
  (Plan 38).
- **Spec:** `product.md` §Identity & credentials; `engineering.md` §Network
  operations & credentials (identity materialization + `CredentialsStore`).
- **Code:**
  - core: `core/credentialsstore.{hpp,cpp}` (`credentials.json`, resolver);
    `GitRepo::{setLocalIdentity,clearLocalIdentity,setGlobalIdentity(static),effectiveIdentity,localIdentity}`.
  - ui: `CredentialManager`, `IdentityListModel`, AsyncRepo identity wrappers,
    `qmlcontext` wiring, `IdentityDialog.qml` (from OptionsDialog).
- **Tests:** `test_credentials_store.cpp` (14), `test_git_repo_identity.cpp` (5),
  `test_identity_list_model.cpp` (2), `test_credential_manager.cpp` (3).
- **Deferred to later phases:** OS-keychain secrets, SSH keyfiles, HTTPS host
  tokens (Plan 37); forge API validation + central credentials UI incl.
  per-project identity assignment (Plan 38).
