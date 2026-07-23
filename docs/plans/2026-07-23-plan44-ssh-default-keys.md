# Plan 44 — SSH auth falls back to the default `~/.ssh` identity files

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-23 |
| **Status** | `done` |
| **Spec** | `spec/engineering/engineering.md §Network operations & credentials` |
| **Depends on** | Plan 37 (keychain secrets, SSH keys) |

**Goal:** Fetch/pull/push over an SSH remote works out of the box for a user
whose CLI already works via a default key (`~/.ssh/id_ed25519`, …), even with an
empty ssh-agent and no key registered in the app — matching OpenSSH's behaviour.

**Architecture:** The bug: `credentialsForRemote` fell back to *only* the
ssh-agent when no in-app key was configured. An empty agent (the macOS default —
`ssh` reads the key straight off disk) then produced `GIT_EAUTH`. Two changes:

1. **core** learns to try *several* SSH credentials in order. libgit2's libssh2
   transport re-invokes the credential callback in a `while (error == GIT_EAUTH)`
   loop (`ssh_libssh2.c`), so a stateful trampoline can hand out ssh-agent, then
   each keyfile, one per invocation. The single-shot `chooseCredential` becomes
   an ordered `credentialAttempts()` plan; `Credentials` carries a
   `std::vector<SshKeyfile>` instead of one scalar keyfile.
2. **ui** discovers the conventional default identity files under `~/.ssh` (pure
   core helper `discoverDefaultSshKeyfiles`) when no key is registered, and tries
   the agent first, then those files.

**Tech stack:** libgit2 `git_credential_ssh_key_*`, `std::filesystem` existence
checks (pure, no Qt), QtKeychain-backed `SecretStore` (unchanged).

## Global constraints

- No Qt in `core/`; the default-key discovery is pure `std::filesystem`, home
  resolution stays in `ui/` (`QDir::homePath()`).
- Errors stay values; the trampoline returns `GIT_EAUTH` when the plan is spent.
- Editing existing test files only — no new `tests/CMakeLists.txt` entries.

---

## Task 1: Ordered credential plan + default-key discovery (core)

**Files:** Modify `core/include/gittide/sync.hpp`, `core/src/credentialselect.cpp`,
`core/src/gitrepo.cpp`; Test `tests/test_credential_select.cpp`.

**Interfaces:**
- `struct SshKeyfile { std::string privateKeyPath, publicKeyPath, passphrase; };`
- `Credentials { bool sshUseAgent; std::string username, password; std::vector<SshKeyfile> sshKeyfiles; };`
- `struct CredentialAttempt { CredentialKind kind; std::size_t keyIndex; };`
- `std::vector<CredentialAttempt> credentialAttempts(url, allowedTypes, cred);`
- `std::vector<SshKeyfile> discoverDefaultSshKeyfiles(const std::filesystem::path& sshDir);`

- [x] **Step 1: Write the failing tests** — ordering (agent then keyfiles),
      exhaustion (empty plan), userpass; discovery order + `.pub` inference.
- [x] **Step 2: Make it pass** — implement `credentialAttempts` and
      `discoverDefaultSshKeyfiles`; make the trampoline walk the plan with a
      per-operation cursor.
- [x] **Step 3: Refactor / verify** — run `test_credential_select`.

## Task 2: Discover defaults in `credentialsForRemote` (ui)

**Files:** Modify `ui/include/gittide/ui/credentialmanager.hpp`,
`ui/src/credentialmanager.cpp`, `ui/src/repoviewmodel.cpp`; Test
`tests/ui/test_credential_manager.cpp`.

**Interfaces:** `CredentialManager::setDefaultSshDir(std::filesystem::path)` (test
hook; defaults to `~/.ssh`).

- [x] **Step 1: Write the failing tests** — no in-app key + a `~/.ssh` holding
      `id_ed25519` ⇒ `sshUseAgent` true *and* the discovered key present; empty
      dir ⇒ agent only.
- [x] **Step 2: Make it pass** — configured keys ⇒ keyfiles only (agent off);
      none ⇒ agent + `discoverDefaultSshKeyfiles(m_sshDir)`.
- [x] **Step 3: Refactor / verify** — full suite.

---

## Outcome

- Shipped: SSH fetch/pull/push authenticates with the default `~/.ssh` identity
  files when no key is registered and the agent is empty, so a repo that works in
  the CLI works in the app. The credential callback now tries the agent, then
  each candidate keyfile, in order (OpenSSH-like), instead of one method only.
- Spec updated: `spec/engineering/engineering.md §Network operations &
  credentials` — the `Credentials` POD, the ordered `credentialAttempts` plan,
  and the default-key fallback.
- Code: `credentialAttempts` / `discoverDefaultSshKeyfiles`
  (`core/src/credentialselect.cpp`), the stateful trampoline
  (`core/src/gitrepo.cpp`), the discovery path in
  `CredentialManager::credentialsForRemote` (`ui/src/credentialmanager.cpp`).
