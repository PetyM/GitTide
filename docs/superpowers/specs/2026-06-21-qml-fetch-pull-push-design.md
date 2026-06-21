# Fetch / Pull / Push — design

**Date:** 2026-06-21
**Status:** designed (awaiting plan)
**Realises:** `docs/spec/product/product.md` (network ops), `docs/spec/engineering/`

## Goal

Add the first network operations beyond clone: **fetch**, **pull**, and **push**,
with **ahead/behind** tracking for the current branch. This is the next step of
the QWidgets→QML migration roadmap and a net-new backend — `core/` today has no
remote traversal, no fetch/push, and `clone()` carries **no credential
callback** (so it only works for anonymous/public URLs).

Single plan delivering all four layers (`core → ui → ViewModel → QML`), built
core-first then UI.

## Decisions (from brainstorming)

- **Auth:** SSH via ssh-agent; HTTPS via a username + Personal Access Token.
- **Pull behaviour:** a **persistent setting** choosing **fast-forward-only** vs
  **rebase**, persisted in **git config** (`pull.rebase`) — interops with CLI git,
  needs no new app-settings layer.
- **Scope:** one plan, core + UI together.

## Credential flow — the key architectural choice

libgit2's credential callback runs on the **worker thread** inside the
fetch/push call. It cannot pop a Qt dialog synchronously without risking
deadlock, and `core/` must not know about Qt.

**Chosen — pre-supply credentials (Option A).** The UI assembles a `Credentials`
POD *before* each network call and passes it down. Core's callback returns those
credentials, selecting by URL scheme and libgit2's `allowed_types`:

- `ssh://` / `git@` → `git_credential_ssh_key_from_agent` (uses the username
  from the URL).
- `https://` with a token present → `git_credential_userpass_plaintext_new`.
- otherwise → `GIT_EAUTH`.

On an auth failure the core call returns an auth error; the ViewModel prompts
for a token and **retries**. No cross-thread dialog, no blocking the worker, and
`core/` stays pure `std`.

Rejected:
- **B — blocking callback that signals the UI thread and waits.**
  Deadlock-prone; rejected.
- **C — git credential helpers.** libgit2 does not run them by default, and the
  project invariant forbids building git command strings / shelling out to git.
  Rejected.

**Token storage:** session memory only — prompt once per remote per session.
Secure persistence (OS keychain via QtKeychain) is **deferred**.

## Core (`core/`, libgit2 — pure C++23, no Qt)

New public header `core/include/gittide/sync.hpp`:

```cpp
namespace gittide {

// Ahead/behind of the current branch versus its upstream.
struct SyncStatus
{
    bool        hasUpstream = false; // false when the branch has no upstream
    int         ahead       = 0;     // local commits not on upstream
    int         behind      = 0;     // upstream commits not local
    std::string upstreamName;        // e.g. "origin/main", empty if none
    std::string remoteName;          // e.g. "origin", empty if none
};

// How pull reconciles a diverged branch. Persisted in git config (pull.rebase).
enum class PullStrategy
{
    FastForwardOnly, // pull.rebase=false: fast-forward, else error
    Rebase,          // pull.rebase=true: rebase local commits onto upstream
};

// Credentials supplied by the UI before a network call. Pure std; no Qt.
struct Credentials
{
    bool        sshUseAgent = true; // ssh:// remotes: use ssh-agent
    std::string username;           // https username
    std::string password;           // https token / password
};

} // namespace gittide
```

New `GitRepo` methods (all return `Expected<>`):

- `Expected<SyncStatus> syncStatus() const;`
  Resolve the current branch's upstream (`git_branch_upstream`); if none,
  `hasUpstream=false`. Otherwise compute ahead/behind with
  `git_graph_ahead_behind(local_oid, upstream_oid)` and fill the names.

- `Expected<void> fetch(std::string remoteName, const Credentials& cred, ProgressCallback cb);`
  `git_remote_lookup` + `git_remote_fetch` with `transfer_progress` (reusing the
  existing `transferProgressTrampoline`) and the new **credential callback**.
  Updates remote-tracking refs. Ahead/behind is recomputed by the caller via
  `syncStatus()` afterwards.

- `Expected<void> pull(const Credentials& cred, ProgressCallback cb);`
  Fetch the upstream's remote, then reconcile per `pullStrategy()`:
  - **FastForwardOnly:** `git_merge_analysis`; if `GIT_MERGE_ANALYSIS_FASTFORWARD`,
    move the branch ref to the upstream OID and checkout; else error
    "cannot fast-forward (branch has diverged)".
  - **Rebase:** `git_rebase_init` onto upstream, replay each operation with
    `git_rebase_commit`, `git_rebase_finish`. On conflict, `git_rebase_abort`
    and return an error — **conflict-resolution UI is deferred**.

- `Expected<void> push(std::string remoteName, std::string branch, bool setUpstream, const Credentials& cred, ProgressCallback cb);`
  `git_remote_push` with refspec `refs/heads/<branch>:refs/heads/<branch>` and
  the credential callback. When `setUpstream` (no upstream yet — "Publish
  branch"), set the branch's upstream after a successful push.

- `Expected<PullStrategy> pullStrategy() const;` /
  `Expected<void> setPullStrategy(PullStrategy);`
  Read/write `pull.rebase` via `git_repository_config` — **the persistent
  setting**.

### Credential callback

A free function `credentialTrampoline(git_credential** out, const char* url,
const char* username_from_url, unsigned allowed_types, void* payload)` where
`payload` points at a `Credentials`. Selection logic is unit-testable in
isolation (split URL-scheme/allowed-type → which credential into a small pure
helper).

## UI (`ui/`)

**AsyncRepo** — new QCoro tasks mirroring core:
`syncStatus()`, `fetch(QString remote, Credentials, progress)`,
`pull(Credentials, progress)`,
`push(QString remote, QString branch, bool setUpstream, Credentials, progress)`,
`pullStrategy()`, `setPullStrategy(PullStrategy)`.

**RepoViewModel**:
- Properties: `int aheadCount`, `int behindCount`, `bool hasUpstream`,
  `QString upstreamName`, `bool syncing`, `bool pullRebase`.
- Invokables: `fetch()`, `pull()`, `push()`, `publishBranch()`,
  `submitCredentials(QString user, QString token)`, `setPullRebase(bool)`.
- Signals: `syncStatusChanged()`, `syncingChanged()`, `authRequired(QString url)`,
  `operationFailed(QString)` (existing).
- Holds a **session token map** (remote URL → Credentials). Builds `Credentials`
  before each call (ssh-agent always on; HTTPS from the map). On an auth error,
  emits `authRequired(url)`; after `submitCredentials` stores the token and
  retries the pending op.
- After any successful fetch/pull/push: refresh status, head, history, and
  `syncStatus`.

## QML

- **BranchBar** extended with a sync cluster:
  - **Fetch** button.
  - **Pull** button with a **behind-count** badge (hidden when 0).
  - **Push** button with an **ahead-count** badge (hidden when 0).
  - **Publish** button shown when `!hasUpstream`.
  - A spinner / disabled state while `syncing`.
- **CredentialDialog.qml** — HTTPS username + token fields, "remember for this
  session", triggered by `authRequired`.
- Pull-strategy toggle (FF-only / Rebase) in a menu, bound to `pullRebase`.
- Progress reuses the `CloneProgressDialog` pattern.

All colours from theme tokens; badges use existing accent/state tokens.

## Tests

**Core (Catch2)** — against **local bare `file://` remotes** so CI needs no
network or auth:
- `TempRepo::createBareRemote()` / `addRemote(name, url)` infrastructure.
- fetch updates remote-tracking refs;
- `syncStatus` ahead/behind correctness (ahead-only, behind-only, diverged, no
  upstream);
- FF pull advances the branch; FF pull on a diverged branch errors;
- rebase pull replays local commits onto upstream;
- push updates the bare remote; publish (no upstream) sets the upstream;
- `pullStrategy` config round-trip (`pull.rebase` read/write).
- credential **selection** helper unit-tested in isolation (scheme/allowed-type
  → credential kind). Real GitHub SSH/HTTPS auth is verified manually, not in CI.

**UI** — ahead/behind → `aheadCount`/`behindCount` property mapping;
`pullRebase` round-trip; `authRequired` fires on simulated auth error.

## Deferred (out of scope this plan)

- Secure token persistence (OS keychain / QtKeychain).
- **Merge** pull strategy (only FF-only and Rebase now).
- **Conflict-resolution UI** for rebase/merge — conflicts abort and surface an
  error; resolve via CLI for now.
- SSH **keyfile + passphrase** (ssh-agent only this round).
- Multi-remote management UI (operates on the upstream's remote / `origin`).
- "Fetch all / pull all" across a project (single-repo only here).

## Spec updates on completion

- `docs/spec/product/product.md`: move push/pull/fetch out of the
  "out of scope" list; document the sync UI and ahead/behind.
- `docs/spec/engineering/`: note the credential-pre-supply pattern and that
  network ops follow the AsyncRepo worker model.
- Update the `qml-ui-migration` memory.
