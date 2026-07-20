# Plan 37 — Credentials Phase 2: keychain secrets, SSH keys, host tokens

> **For agentic workers:** implement this plan task-by-task, test-first.

| | |
|--|--|
| **Date** | 2026-07-06 |
| **Status** | `done` |
| **Spec** | `spec/engineering/engineering.md §Network operations & credentials`, `spec/product/product.md §Identity & credentials` |
| **Depends on** | [Plan 36](2026-07-06-plan36-identity.md) |

**Goal:** Store HTTPS tokens and SSH-key passphrases in the OS keychain (so they
survive restart), support SSH keyfiles, and authenticate `clone` — secrets never
written to GitTide's own files (**D50**).

**Architecture:** A `ui/`-side `SecretStore` seam over QtKeychain
(`KeychainSecretStore` prod / `InMemorySecretStore` tests). Core `Credentials`
gains SSH-keyfile fields; `chooseCredential`/the trampoline gain a `SshKey` kind;
`clone()` takes `Credentials`. `CredentialManager` assembles a `Credentials` POD
for a remote URL from stored metadata + keychain secrets (`credentialsForRemote`).

## Global constraints

- No Qt in `core/` — QtKeychain (and later `QNetworkAccessManager`) stay in `ui/`.
- Secrets never in `credentials.json`; keys `host-token:<id>` / `ssh-passphrase:<id>`.
- New deps → `cmake/Dependencies.cmake`; CI needs `libsecret-1-dev` (Linux).

---

## Task 1: Core — SSH keyfile support + clone credentials ✅

**Files:** `core/sync.hpp`, `core/src/credentialselect.cpp`, `core/src/gitrepo.cpp`
(trampoline + `clone` + `remoteUrl`), `tests/test_credential_select.cpp`,
`tests/test_git_repo_sync.cpp`, `tests/test_git_repo_init_clone.cpp`.

- [x] `Credentials` gains `sshPublicKeyPath` / `sshPrivateKeyPath` / `sshPassphrase`;
  `CredentialKind::SshKey`; `chooseCredential` picks the keyfile when agent is off
  and a key is set (agent still preferred when on). Trampoline →
  `git_credential_ssh_key_new`. Tests: 6 cases green.
- [x] `clone()` takes `Credentials` and wires the credential trampoline (was
  progress-only). Call site + 3 clone tests updated.
- [x] `GitRepo::remoteUrl(remote)` (for URL→host matching) + test.

## Task 2: CMake — QtKeychain ✅

**Files:** `cmake/Dependencies.cmake`, `ui/CMakeLists.txt`.

- [x] `find_package(Qt6Keychain QUIET)` then FetchContent fallback
  (`frankosterfeld/qtkeychain` `v0.14.0`, `BUILD_WITH_QT6 ON`, static). Aliased to
  `Qt6Keychain::Qt6Keychain`, linked PRIVATE to `gittide_ui`; include dirs pulled
  in manually (QtKeychain doesn't export them, like libgit2). *TODO:* add
  `libsecret-1-dev` to `.github/workflows/ci.yml`.

## Task 3: SecretStore seam ✅

**Files:** `ui/include/gittide/ui/secretstore.hpp`, `ui/src/secretstore.cpp`,
`tests/ui/test_secret_store.cpp`.

- [x] Abstract `SecretStore` (`read`/`write`/`remove` → `QCoro::Task`);
  `KeychainSecretStore` (QtKeychain jobs `co_await`-ed via `qCoro(job, &Job::finished)`,
  service `"GitTide"`, degrades to empty/false on backend error);
  `InMemorySecretStore`. Test: InMemory contract green. **No automated real-keychain
  test** — a real op can block on an OS access prompt in a headless run.

## Task 4: CredentialManager — secrets + credentialsForRemote ✅

**Files:** `ui/…/credentialmanager.{hpp,cpp}`, `tests/ui/test_credential_manager.cpp`.

- [x] Constructor takes an optional `SecretStore*` (defaults to a keychain store).
  Host CRUD (`addHost`/`removeHost` — token → keychain), SSH-key CRUD
  (`addSshKey`/`removeSshKey` — passphrase → keychain), `rememberHostToken` (persist
  a dialog token to the host+keychain), and `credentialsForRemote(url)` (https →
  matched host token; ssh → first keyfile + passphrase, else agent). Tests: 4 new
  cases green (https token, ssh keyfile, ssh agent-fallback, remember-token).

## Task 5: Wire keychain-backed credentials into the live sync path ✅

**Files:** `ui/…/asyncrepo.*`, `ui/…/repocontroller.*`, `ui/…/repoviewmodel.*`,
`ui/…/projectcontroller.*`, `app/qml_main.cpp`, `ui/qml/CredentialDialog.qml`.

- [x] Remote URL up the stack: `AsyncRepo::remoteUrl` →
  `RepoController::currentRemoteUrl()` → `RepoViewModel`.
- [x] `RepoViewModel::fetch/pull/push/publish` route through `runFetch/runPull/
  runPush` → `resolveCredentials()` (session override → `credentialsForRemote(url)`
  → agent default) instead of the raw `m_sessionCred`.
- [x] `submitCredentials` calls `credentialManager->rememberHostToken(url, …)` so
  the entered token persists to the keychain; `CredentialDialog.qml` note updated
  ("Saved securely to your OS keychain for this host"). *(SSH agent/keyfile picker
  in the dialog deferred — SSH keys are managed via the central UI in Plan 38.)*
- [x] Fleet fetch (`ProjectController`) resolves `credentialsForRemote` per repo.
- [x] `clone` (`ProjectController::cloneRepo`) uses `credentialsForRemote(url)`.
- [x] Wired via `RepoViewModel::setCredentialManager` /
  `ProjectController::setCredentialManager` in `qml_main.cpp`. Full UI suite green
  (390) standalone; existing sync tests unaffected.

---

## Outcome

- **Shipped:** secrets in the OS keychain (survive restart), SSH-keyfile auth,
  authenticated `clone`, and the whole live sync path (fetch/pull/push/clone/fleet)
  resolving credentials from the keychain-backed store; the auth-dialog fallback
  now persists the entered token to the keychain for next time. All TDD-green.
- **Code:** `ui/secretstore.{hpp,cpp}`; `CredentialManager` host/ssh/token APIs +
  `credentialsForRemote`; `core` SSH-keyfile (`Credentials` + `chooseCredential` +
  trampoline) + `clone(Credentials)` + `remoteUrl`; `AsyncRepo::remoteUrl`,
  `RepoController::currentRemoteUrl`, `RepoViewModel::{runFetch,runPull,runPush,
  resolveCredentials}`; QtKeychain in the build.
- **Spec:** engineering §Network operations & credentials (keychain flow),
  product §Identity & credentials.
- **Tests:** `test_credential_select` (SSH keyfile), `test_git_repo_sync`
  (remoteUrl), clone tests (new signature), `test_secret_store`,
  `test_credential_manager` (credentialsForRemote ×4).
- **Deferred to Plan 38:** the dialog's SSH agent/keyfile picker and per-host
  token *management UI* land with the central credentials dialog + forge validation.
- **CI TODO:** add `libsecret-1-dev` to the Linux job.
