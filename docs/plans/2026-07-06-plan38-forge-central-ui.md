# Plan 38 — Credentials Phase 3: forge validation + central UI

> **For agentic workers:** implement this plan task-by-task, test-first.

| | |
|--|--|
| **Date** | 2026-07-06 |
| **Status** | `done` |
| **Spec** | `spec/product/product.md §Identity & credentials`, `spec/engineering/engineering.md §Network operations & credentials` |
| **Depends on** | [Plan 36](2026-07-06-plan36-identity.md), [Plan 37](2026-07-06-plan37-keychain-secrets.md) |

**Goal:** Validate GitHub/GitLab tokens against the host API (prefilling an
identity) and give one place to manage identities, host accounts, and SSH keys —
plus per-project identity assignment.

**Architecture:** `ForgeClient` (`QNetworkAccessManager` + `QJsonDocument`, `ui/`
only — **D51**) does `GET {apiBase}/user`. `CredentialManager` gains
`validateAndAddHost` (validate → add host + keychain token → create identity from
the login/email) and owns `HostListModel` / `SshKeyListModel`. The `IdentityDialog`
becomes the central **Credentials** dialog.

## Global constraints

- Forge HTTP + `QJsonDocument` stay in `ui/`; nlohmann stays private to `core/`.
- `Qt6::Network` added to the ui link set (no QCoro network module — reuse
  `qCoro(reply, &QNetworkReply::finished)`).

---

## Task 1: ForgeClient ✅

**Files:** `ui/…/forgeclient.{hpp,cpp}`, `tests/ui/test_forge_client.cpp`,
`cmake/Dependencies.cmake`, `ui/CMakeLists.txt`.

- [x] `Qt6::Network` added; `ForgeClient::validate(kind, apiBase, token)` →
  `ForgeAccount{ok, login, name, email, error}`; GitHub `login` / GitLab
  `username`; 401/403 → not-ok with a message. Injectable NAM.
- [x] Test against a local `QTcpServer` serving canned JSON (github, gitlab, 401).
  3 cases green.

## Task 2: Host & SSH list models ✅

**Files:** `ui/…/hostlistmodel.{hpp,cpp}`, `ui/…/sshkeylistmodel.{hpp,cpp}`,
`ui/CMakeLists.txt`, `ui/src/qmlcontext.cpp`.

- [x] `HostListModel` (host/kind/username/apiBase roles) and `SshKeyListModel`
  (label/privateKeyPath/hasPassphrase roles), mirroring `IdentityListModel`; owned
  by `CredentialManager`, exposed as `hostModel` / `sshKeyModel` context props.

## Task 3: CredentialManager — validate + own models ✅

**Files:** `ui/…/credentialmanager.{hpp,cpp}`.

- [x] Owns `HostListModel` / `SshKeyListModel` / `ForgeClient`; host & SSH CRUD
  refresh their models; `validateAndAddHost(host, kind, apiBase, token)` runs the
  forge validation then adds the host (token → keychain) and creates a matching
  identity when the API returns an email; `hostValidated(ok, message)` signal
  reports the outcome to QML.

## Task 4: Central Credentials dialog ✅

**Files:** `ui/qml/IdentityDialog.qml`.

- [x] One scrollable dialog with three sections — **Identities** (add / delete;
  assign Global, **Project** (active project default), Repo override with chips),
  **Host accounts** (kind combo + host + optional API base + token → *Validate &
  add*, with a status line from `hostValidated`; delete), **SSH keys** (label +
  private/public path + passphrase → Add; delete). Reuses `OverlayCard` /
  `AppButton` / `AppComboBox` / `AppScrollBar` (no new token). Loads clean in the
  headless shell test.

---

## Outcome

- **Shipped:** forge token validation (GitHub/GitLab, self-hosted via API base) that
  confirms a token and prefills/creates an identity, and a single central
  **Credentials** dialog managing identities (global / per-project / per-repo), host
  accounts (keychain-backed tokens), and SSH keys. This completes the
  credentials-management feature (Plans 36–38).
- **Code:** `ui/forgeclient.{hpp,cpp}`, `ui/hostlistmodel.*`, `ui/sshkeylistmodel.*`,
  `CredentialManager::validateAndAddHost` + owned models, `ui/qml/IdentityDialog.qml`
  (now the central dialog), `Qt6::Network` in the build.
- **Spec:** product & engineering §Identity & credentials updated; **D51** logged.
- **Tests:** `test_forge_client.cpp` (3). Full UI suite green (395).
- **Follow-ups (not blockers):** an SSH agent/keyfile picker inside the sync auth
  dialog; forge features beyond token validation (PRs/issues) remain out of scope;
  add `libsecret-1-dev` to the Linux CI job (shared with Plan 37).
