# Fetch / Pull / Push Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add fetch, pull (fast-forward-only or rebase), push (incl. publish), and ahead/behind tracking for the current branch, across all layers (`core → AsyncRepo → RepoController → RepoViewModel → QML`), with SSH-agent + HTTPS-token auth.

**Architecture:** `core/GitRepo` gains pure-libgit2 sync methods returning `Expected<>`. A `Credentials` POD is supplied by the UI before each network call; a libgit2 credential callback selects ssh-agent vs userpass by URL scheme — no cross-thread dialogs. `AsyncRepo` wraps each method as a QCoro task; `RepoController` orchestrates refresh chains and emits Qt signals; `RepoViewModel` exposes Q_PROPERTY/Q_INVOKABLE for QML and holds a session token map with auth-retry. QML adds a sync cluster to `BranchBar` plus a `CredentialDialog`.

**Tech Stack:** C++23, libgit2, Qt 6.8 Quick/QML, QCoro, Catch2.

## Global Constraints

- **No Qt in `core/`.** `core/` compiles without Qt on the include path; speaks `std` only (`std::string` UTF-8, `std::filesystem::path`, `std::expected`).
- **libgit2 and nlohmann/json are PRIVATE to `core/`** — no public header includes them; `gitrepo.hpp` forward-declares libgit2 structs.
- **Errors are values:** core returns `Expected<T> = std::expected<T, GitError>`; no exceptions across layers.
- **One owner per `GitRepo`** — move-only, not thread-safe; concurrency via per-worker instances. `AsyncRepo::Impl` serializes pool access with a `std::mutex`.
- **Paths via `generic_u8string()`/`toGitPath`**, never `.string()`. Never build git command strings — use the libgit2 API.
- **Colour from a theme token**, never a hex literal in QML/widgets.
- **TDD:** failing test first. New `ui/` sources → `ui/CMakeLists.txt`; new tests → the matching list in `tests/CMakeLists.txt`.
- **Code style:** Allman braces via `.clang-format`; `m_` members; lowercase file names; KISS/DRY/SOLID/YAGNI. Coroutine slots take args **by value** (survive `co_await`). Guard `this` across suspension with `QPointer`.
- **Build:** `cmake -S . -B build`, `cmake --build build --parallel`. Fresh configure needs `-DCMAKE_PREFIX_PATH=/home/michal/Qt/6.8.3/gcc_64`. QML target behind `-DGITGUI_BUILD_QML=ON`.
- **Catch2 runs by tag:** `ctest -R gittide_core_tests` matches nothing (cases register individually). Run `./build/tests/gittide_core_tests "[tag]"`.

---

## File Structure

**Core (new/modified):**
- Create `core/include/gittide/sync.hpp` — `SyncStatus`, `PullStrategy`, `Credentials`, and `chooseCredential` declaration.
- Create `core/src/credentialselect.cpp` — pure `chooseCredential` helper (testable without a repo).
- Modify `core/include/gittide/gitrepo.hpp` — new method declarations.
- Modify `core/src/gitrepo.cpp` — `syncStatus`, `fetch`, `pull`, `push`, `pullStrategy`, `setPullStrategy`, credential trampoline.

**Tests (core):**
- Create `tests/test_git_repo_sync.cpp`.
- Create `tests/test_credential_select.cpp`.
- Modify `tests/support/temprepo.hpp` / `temprepo.cpp` — `addBareRemote`, `pushBranch`, `resetBranchTo`.
- Modify `tests/CMakeLists.txt`.

**UI (modified):**
- Modify `ui/include/gittide/ui/asyncrepo.hpp` + `ui/src/asyncrepo.cpp`.
- Modify `ui/include/gittide/ui/repocontroller.hpp` + `ui/src/repocontroller.cpp`.
- Modify `ui/include/gittide/ui/repoviewmodel.hpp` + `ui/src/repoviewmodel.cpp`.
- Modify `ui/src/repocontroller.cpp` (metatype registration).

**QML (new/modified):**
- Modify `ui/qml/BranchBar.qml` — sync cluster.
- Create `ui/qml/CredentialDialog.qml`.
- Modify `ui/qml/Main.qml` — host the dialog, wire `authRequired`.
- Modify `ui/qml/qml.qrc` — register the new file.

**Tests (UI):**
- Modify `tests/ui/test_qml_*` (new `tests/ui/test_qml_sync.cpp`) + `tests/CMakeLists.txt`.

---

## Task 1: Sync value types + credential-selection helper

**Files:**
- Create: `core/include/gittide/sync.hpp`
- Create: `core/src/credentialselect.cpp`
- Test: `tests/test_credential_select.cpp`
- Modify: `core/CMakeLists.txt` (add `src/credentialselect.cpp`), `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct gittide::SyncStatus { bool hasUpstream; int ahead; int behind; std::string upstreamName; std::string remoteName; };`
  - `enum class gittide::PullStrategy { FastForwardOnly, Rebase };`
  - `struct gittide::Credentials { bool sshUseAgent = true; std::string username; std::string password; };`
  - `enum class gittide::CredentialKind { SshAgent, UserPass, None };`
  - `CredentialKind gittide::chooseCredential(std::string_view url, unsigned allowedTypes, const Credentials& cred);`
    - `allowedTypes` is libgit2's `git_credential_t` bitmask. Returns `SshAgent` when `url` is SSH-style (`ssh://` or `user@host:` scptish) and SSH agent is allowed; `UserPass` when userpass is allowed and a non-empty username/password is present; else `None`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_credential_select.cpp
#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "gittide/sync.hpp"

using gittide::chooseCredential;
using gittide::CredentialKind;
using gittide::Credentials;

TEST_CASE("chooseCredential picks ssh-agent for ssh urls", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = true;
    REQUIRE(chooseCredential("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::SshAgent);
    REQUIRE(chooseCredential("ssh://git@host/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::SshAgent);
}

TEST_CASE("chooseCredential picks userpass for https with a token", "[sync][cred]")
{
    Credentials c;
    c.username = "me";
    c.password = "ghp_token";
    REQUIRE(chooseCredential("https://github.com/me/repo.git", GIT_CREDENTIAL_USERPASS_PLAINTEXT, c) == CredentialKind::UserPass);
}

TEST_CASE("chooseCredential returns None when https has no token", "[sync][cred]")
{
    Credentials c; // empty username/password
    REQUIRE(chooseCredential("https://github.com/me/repo.git", GIT_CREDENTIAL_USERPASS_PLAINTEXT, c) == CredentialKind::None);
}

TEST_CASE("chooseCredential respects sshUseAgent=false", "[sync][cred]")
{
    Credentials c;
    c.sshUseAgent = false;
    REQUIRE(chooseCredential("git@github.com:me/repo.git", GIT_CREDENTIAL_SSH_KEY, c) == CredentialKind::None);
}
```

- [ ] **Step 2: Create the header**

```cpp
// core/include/gittide/sync.hpp
#pragma once
#include <string>
#include <string_view>

namespace gittide {

// Ahead/behind of the current branch versus its upstream remote-tracking ref.
struct SyncStatus
{
    bool        hasUpstream = false; // false when the current branch has no upstream
    int         ahead       = 0;     // local commits not on the upstream
    int         behind      = 0;     // upstream commits not present locally
    std::string upstreamName;        // e.g. "origin/main"; empty when no upstream
    std::string remoteName;          // e.g. "origin"; empty when no upstream
};

// How pull reconciles a diverged branch. Persisted in git config (pull.rebase).
enum class PullStrategy
{
    FastForwardOnly, // fast-forward if possible, else error
    Rebase,          // rebase local commits onto the upstream
};

// Credentials supplied by the UI before a network call. Pure std; no Qt.
struct Credentials
{
    bool        sshUseAgent = true; // ssh remotes: authenticate via ssh-agent
    std::string username;           // https username
    std::string password;           // https token / password
};

// Which credential kind a callback should produce for a given remote URL.
enum class CredentialKind
{
    SshAgent,
    UserPass,
    None,
};

// Pure selection logic used by the libgit2 credential callback. allowedTypes is
// a git_credential_t bitmask. Kept free-standing so it is unit-testable without
// a repository or a live remote.
CredentialKind chooseCredential(std::string_view url, unsigned allowedTypes, const Credentials& cred);

} // namespace gittide
```

- [ ] **Step 3: Implement the helper**

```cpp
// core/src/credentialselect.cpp
#include "gittide/sync.hpp"

#include <git2.h>

namespace gittide {

namespace {
// SSH-style: explicit scheme, or scp-like "user@host:path" with no "://".
bool isSshUrl(std::string_view url)
{
    if (url.rfind("ssh://", 0) == 0)
        return true;
    if (url.find("://") != std::string_view::npos)
        return false; // some other explicit scheme (https/git/file)
    auto at = url.find('@');
    auto colon = url.find(':');
    return at != std::string_view::npos && colon != std::string_view::npos && at < colon;
}
} // namespace

CredentialKind chooseCredential(std::string_view url, unsigned allowedTypes, const Credentials& cred)
{
    if (isSshUrl(url) && cred.sshUseAgent && (allowedTypes & GIT_CREDENTIAL_SSH_KEY))
        return CredentialKind::SshAgent;
    if ((allowedTypes & GIT_CREDENTIAL_USERPASS_PLAINTEXT) && !cred.username.empty() && !cred.password.empty())
        return CredentialKind::UserPass;
    return CredentialKind::None;
}

} // namespace gittide
```

- [ ] **Step 4: Register the source and test**

In `core/CMakeLists.txt`, add `src/credentialselect.cpp` to the core library sources (the list that already contains `src/gitrepo.cpp`).

In `tests/CMakeLists.txt`, add `test_credential_select.cpp` to the `gittide_core_tests` source list (the same list as `test_git_repo_branches.cpp`).

- [ ] **Step 5: Build and run the test**

Run:
```bash
cmake --build build --parallel
./build/tests/gittide_core_tests "[cred]"
```
Expected: all 4 cases PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/sync.hpp core/src/credentialselect.cpp core/CMakeLists.txt tests/test_credential_select.cpp tests/CMakeLists.txt
git commit -m "feat(core): sync value types + credential-selection helper"
```

---

## Task 2: TempRepo remote test infrastructure + GitRepo::syncStatus

**Files:**
- Modify: `tests/support/temprepo.hpp`, `tests/support/temprepo.cpp`
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_sync.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `gittide::SyncStatus` (Task 1).
- Produces:
  - `Expected<SyncStatus> GitRepo::syncStatus() const;`
  - TempRepo helpers:
    - `std::filesystem::path TempRepo::addBareRemote(std::string_view name);` — creates a bare repo at `<tmp>/<name>.git`, runs `git_remote_create(name -> that path's file:// url)`, returns the bare path.
    - `void TempRepo::pushBranch(std::string_view remote, std::string_view branch);` — pushes `refs/heads/<branch>` to the remote (file://, no auth) and sets the branch's upstream to `<remote>/<branch>`.
    - `void TempRepo::resetBranchTo(std::string_view branch, std::string_view oidHex);` — moves the branch ref to the given commit (hard), updating the working tree.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_git_repo_sync.cpp
#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::test::TempRepo;

TEST_CASE("syncStatus reports no upstream when none is set", "[sync][status]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE_FALSE(st->hasUpstream);
}

TEST_CASE("syncStatus reports ahead after a local-only commit", "[sync][status]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    repo.writeFile("a.txt", "two");
    repo.commitAll("c2"); // local-only

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->hasUpstream);
    REQUIRE(st->ahead == 1);
    REQUIRE(st->behind == 0);
    REQUIRE(st->remoteName == "origin");
    REQUIRE(st->upstreamName == "origin/master");
}
```

> Note: the default branch name created by libgit2 `init` is `master` in this codebase; keep `"master"` consistent across sync tests.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL to compile — `addBareRemote`/`pushBranch`/`syncStatus` not declared.

- [ ] **Step 3: Add the TempRepo helpers**

In `tests/support/temprepo.hpp`, add to the public section:

```cpp
    // Create a bare repo at <tmp>/<name>.git and register it as remote `name`
    // (file:// url). Returns the bare repo path.
    std::filesystem::path addBareRemote(std::string_view name);

    // Push refs/heads/<branch> to the remote (no auth) and set the branch's
    // upstream to <remote>/<branch>.
    void pushBranch(std::string_view remote, std::string_view branch);

    // Move the branch ref to oidHex and hard-reset the working tree to it.
    void resetBranchTo(std::string_view branch, std::string_view oidHex);
```

In `tests/support/temprepo.cpp` (include `<git2.h>`):

```cpp
std::filesystem::path TempRepo::addBareRemote(std::string_view name)
{
    std::filesystem::path bare = m_dir.parent_path() / (std::string(name) + ".git");
    git_repository* bare_repo  = nullptr;
    git_repository_init(&bare_repo, bare.generic_u8string().c_str() /*see note*/, /*is_bare=*/1);
    git_repository_free(bare_repo);

    std::string url = "file://" + bare.generic_string();
    git_remote* remote = nullptr;
    git_remote_create(&remote, m_repo, std::string(name).c_str(), url.c_str());
    git_remote_free(remote);
    return bare;
}

void TempRepo::pushBranch(std::string_view remote, std::string_view branch)
{
    git_remote* r = nullptr;
    git_remote_lookup(&r, m_repo, std::string(remote).c_str());

    std::string ref     = "refs/heads/" + std::string(branch);
    std::string refspec = ref + ":" + ref;
    char* specs[]       = {refspec.data()};
    git_strarray arr    = {specs, 1};

    git_push_options opts = GIT_PUSH_OPTIONS_INIT;
    git_remote_push(r, &arr, &opts);
    git_remote_free(r);

    // Set upstream so syncStatus has an upstream to compare against.
    git_reference* branch_ref = nullptr;
    git_branch_lookup(&branch_ref, m_repo, std::string(branch).c_str(), GIT_BRANCH_LOCAL);
    std::string upstream = std::string(remote) + "/" + std::string(branch);
    git_branch_set_upstream(branch_ref, upstream.c_str());
    git_reference_free(branch_ref);
}

void TempRepo::resetBranchTo(std::string_view branch, std::string_view oidHex)
{
    git_oid oid;
    git_oid_fromstr(&oid, std::string(oidHex).c_str());
    git_object* obj = nullptr;
    git_object_lookup(&obj, m_repo, &oid, GIT_OBJECT_COMMIT);
    git_reset(m_repo, obj, GIT_RESET_HARD, nullptr);
    git_object_free(obj);
}
```

> Path note: `git_repository_init` takes a UTF-8 C path. Use the same `toGitPath`/`generic_u8string` convention already used in `temprepo.cpp` for `m_dir`; match the existing call style in that file rather than the placeholder comment above.

- [ ] **Step 4: Declare syncStatus**

In `core/include/gittide/gitrepo.hpp`, add `#include "gittide/sync.hpp"` and, after `head()`:

```cpp
    // Ahead/behind of the current branch versus its upstream remote-tracking
    // ref. hasUpstream is false (ahead/behind 0) when the branch has no upstream
    // or HEAD is unborn/detached. See SyncStatus.
    Expected<SyncStatus> syncStatus() const;
```

- [ ] **Step 5: Implement syncStatus**

In `core/src/gitrepo.cpp` add `#include <git2/graph.h>` (already present) and implement:

```cpp
Expected<SyncStatus> GitRepo::syncStatus() const
{
    SyncStatus out;

    git_reference* head = nullptr;
    int rc = git_repository_head(&head, m_repo);
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND)
        return out; // unborn => no upstream
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);

    if (git_reference_is_branch(head) != 1)
        return out; // detached HEAD => no upstream

    git_reference* upstream = nullptr;
    rc = git_branch_upstream(&upstream, head);
    if (rc == GIT_ENOTFOUND)
        return out; // no upstream configured
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> up_guard(upstream, git_reference_free);

    const git_oid* localOid    = git_reference_target(head);
    const git_oid* upstreamOid = git_reference_target(upstream);
    if (!localOid || !upstreamOid)
        return out;

    size_t ahead = 0, behind = 0;
    rc = git_graph_ahead_behind(&ahead, &behind, m_repo, localOid, upstreamOid);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    out.hasUpstream  = true;
    out.ahead        = static_cast<int>(ahead);
    out.behind       = static_cast<int>(behind);
    const char* up   = git_reference_shorthand(upstream);
    out.upstreamName = up ? up : "";

    git_buf remote_buf = GIT_BUF_INIT;
    if (git_branch_remote_name(&remote_buf, m_repo, git_reference_name(upstream)) == 0)
        out.remoteName = std::string(remote_buf.ptr, remote_buf.size);
    git_buf_dispose(&remote_buf);

    return out;
}
```

- [ ] **Step 6: Register the test, build, run**

Add `test_git_repo_sync.cpp` to the `gittide_core_tests` source list in `tests/CMakeLists.txt`.

Run:
```bash
cmake --build build --parallel
./build/tests/gittide_core_tests "[status]"
```
Expected: both `[status]` cases PASS.

- [ ] **Step 7: Commit**

```bash
git add tests/support/temprepo.hpp tests/support/temprepo.cpp core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_sync.cpp tests/CMakeLists.txt
git commit -m "feat(core): GitRepo::syncStatus + bare-remote test infra"
```

---

## Task 3: GitRepo::fetch + credential trampoline

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_sync.cpp`

**Interfaces:**
- Consumes: `Credentials`, `chooseCredential` (Task 1), `ProgressCallback` (existing).
- Produces: `Expected<void> GitRepo::fetch(std::string remoteName, Credentials cred, ProgressCallback cb);`
  - After a successful fetch, remote-tracking refs (`refs/remotes/<remote>/*`) are updated; `syncStatus().behind` reflects new upstream commits.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("fetch updates remote-tracking and reports behind", "[sync][fetch]")
{
    // origin starts at c1; a *second* clone pushes c2; our repo fetches and is behind 1.
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    // Second working clone of the bare, adds c2, pushes it.
    TempRepo other;
    other.cloneFrom(bare);                 // helper added below
    other.setIdentity("Other", "o@example.com");
    other.writeFile("a.txt", "two");
    other.commitAll("c2");
    other.pushBranch("origin", "master");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto fr = gr->fetch("origin", gittide::Credentials{}, [](unsigned, unsigned) { return true; });
    REQUIRE(fr);

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->hasUpstream);
    REQUIRE(st->behind == 1);
    REQUIRE(st->ahead == 0);
}
```

Add a `cloneFrom` helper to `TempRepo` (header + cpp). It replaces the empty temp dir's repo with a clone of `bare`:

```cpp
// temprepo.hpp (public)
    // Replace this TempRepo's repository with a clone of the bare repo at
    // barePath (file://). Registers origin automatically (libgit2 clone does).
    void cloneFrom(const std::filesystem::path& barePath);
```
```cpp
// temprepo.cpp
void TempRepo::cloneFrom(const std::filesystem::path& barePath)
{
    if (m_repo) { git_repository_free(m_repo); m_repo = nullptr; }
    std::filesystem::remove_all(m_dir);
    std::string url = "file://" + barePath.generic_string();
    git_clone(&m_repo, url.c_str(), m_dir.generic_u8string().c_str() /*match file's path style*/, nullptr);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `fetch` and `cloneFrom` not declared.

- [ ] **Step 3: Add the credential trampoline (anonymous namespace in gitrepo.cpp)**

Near `transferProgressTrampoline`, add:

```cpp
int credentialTrampoline(git_credential** out, const char* url, const char* username_from_url,
                         unsigned int allowed_types, void* payload)
{
    const auto& cred = *static_cast<gittide::Credentials*>(payload);
    switch (gittide::chooseCredential(url ? url : "", allowed_types, cred))
    {
    case gittide::CredentialKind::SshAgent:
        return git_credential_ssh_key_from_agent(out, username_from_url ? username_from_url : "git");
    case gittide::CredentialKind::UserPass:
        return git_credential_userpass_plaintext_new(out, cred.username.c_str(), cred.password.c_str());
    case gittide::CredentialKind::None:
    default:
        return GIT_EAUTH;
    }
}
```

Add includes at top of `gitrepo.cpp`: `#include <git2/credential.h>`, `#include <git2/remote.h>`, and `#include "gittide/sync.hpp"`.

- [ ] **Step 4: Declare and implement fetch**

`gitrepo.hpp` (after `syncStatus`):

```cpp
    // Fetch the named remote, updating remote-tracking refs. cred is supplied by
    // the caller (ssh-agent / https token); cb reports transfer progress. The
    // credential callback selects ssh-agent vs userpass by URL scheme.
    Expected<void> fetch(std::string remoteName, Credentials cred, ProgressCallback cb);
```

`gitrepo.cpp`:

```cpp
Expected<void> GitRepo::fetch(std::string remoteName, Credentials cred, ProgressCallback cb)
{
    git_remote* raw = nullptr;
    int rc = git_remote_lookup(&raw, m_repo, remoteName.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_remote, decltype(&git_remote_free)> remote(raw, git_remote_free);

    git_fetch_options opts            = GIT_FETCH_OPTIONS_INIT;
    opts.callbacks.transfer_progress  = transferProgressTrampoline;
    opts.callbacks.payload            = &cb;       // for transfer_progress
    opts.callbacks.credentials        = credentialTrampoline;
    // credentials callback reads payload too; libgit2 passes the same payload to
    // both. Wrap so each gets the right object:
    struct CbPayload { ProgressCallback* cb; Credentials* cred; } pl{&cb, &cred};
    opts.callbacks.payload = &pl;

    rc = git_remote_fetch(remote.get(), nullptr, &opts, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}
```

> **Important — shared payload:** libgit2 passes ONE `payload` to all callbacks. Update both trampolines to take a `CbPayload*`:
> - define `struct CbPayload { gittide::ProgressCallback* cb; gittide::Credentials* cred; };` in the anonymous namespace;
> - `transferProgressTrampoline` casts payload to `CbPayload*` and calls `*(pl->cb)`;
> - `credentialTrampoline` casts to `CbPayload*` and uses `*(pl->cred)`.
> Update the existing `clone()` call site: it sets only `transfer_progress`; wrap its `cb` in a `CbPayload{&cb, nullptr}` and point `payload` at it so the trampoline signature stays uniform.

Rewrite the two trampolines accordingly:

```cpp
struct CbPayload
{
    gittide::ProgressCallback* cb   = nullptr;
    gittide::Credentials*      cred = nullptr;
};

int transferProgressTrampoline(const git_indexer_progress* stats, void* payload)
{
    auto* pl = static_cast<CbPayload*>(payload);
    return (*pl->cb)(stats->received_objects, stats->total_objects) ? 0 : -1;
}

int credentialTrampoline(git_credential** out, const char* url, const char* username_from_url,
                         unsigned int allowed_types, void* payload)
{
    const auto& cred = *static_cast<CbPayload*>(payload)->cred;
    switch (gittide::chooseCredential(url ? url : "", allowed_types, cred)) { /* as above */ }
}
```

And in `clone()` change:
```cpp
    CbPayload pl{&cb, nullptr};
    opts.fetch_opts.callbacks.transfer_progress = transferProgressTrampoline;
    opts.fetch_opts.callbacks.payload           = &pl;
```

- [ ] **Step 5: Build and run**

Run:
```bash
cmake --build build --parallel
./build/tests/gittide_core_tests "[fetch]"
./build/tests/gittide_core_tests "[status]"   # ensure clone() change didn't regress
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/support/temprepo.hpp tests/support/temprepo.cpp tests/test_git_repo_sync.cpp
git commit -m "feat(core): GitRepo::fetch with shared-payload credential callback"
```

---

## Task 4: pullStrategy / setPullStrategy (git config persistence)

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_sync.cpp`

**Interfaces:**
- Consumes: `PullStrategy` (Task 1).
- Produces:
  - `Expected<PullStrategy> GitRepo::pullStrategy() const;` — reads `pull.rebase`; `true` → `Rebase`, absent/`false` → `FastForwardOnly`.
  - `Expected<void> GitRepo::setPullStrategy(PullStrategy);` — writes `pull.rebase` (`Rebase`→true, `FastForwardOnly`→false).

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("pullStrategy round-trips through git config", "[sync][strategy]")
{
    TempRepo repo;
    repo.writeFile("a.txt", "one");
    repo.setIdentity("Test", "test@example.com");
    repo.commitAll("c1");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);

    // Default (unset) => FastForwardOnly.
    auto s0 = gr->pullStrategy();
    REQUIRE(s0);
    REQUIRE(*s0 == gittide::PullStrategy::FastForwardOnly);

    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::Rebase));
    auto s1 = gr->pullStrategy();
    REQUIRE(s1);
    REQUIRE(*s1 == gittide::PullStrategy::Rebase);

    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::FastForwardOnly));
    auto s2 = gr->pullStrategy();
    REQUIRE(s2);
    REQUIRE(*s2 == gittide::PullStrategy::FastForwardOnly);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `pullStrategy`/`setPullStrategy` not declared.

- [ ] **Step 3: Declare + implement**

`gitrepo.hpp`:

```cpp
    // Read/write the pull reconciliation strategy, persisted in git config
    // (pull.rebase: true => Rebase, absent/false => FastForwardOnly).
    Expected<PullStrategy> pullStrategy() const;
    Expected<void>         setPullStrategy(PullStrategy strategy);
```

`gitrepo.cpp` (add `#include <git2/config.h>`):

```cpp
Expected<PullStrategy> GitRepo::pullStrategy() const
{
    git_config* cfg = nullptr;
    int rc = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    int rebase = 0;
    rc = git_config_get_bool(&rebase, cfg, "pull.rebase");
    if (rc == GIT_ENOTFOUND)
        return PullStrategy::FastForwardOnly;
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return rebase ? PullStrategy::Rebase : PullStrategy::FastForwardOnly;
}

Expected<void> GitRepo::setPullStrategy(PullStrategy strategy)
{
    git_config* cfg = nullptr;
    int rc = git_repository_config(&cfg, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_config, decltype(&git_config_free)> guard(cfg, git_config_free);

    rc = git_config_set_bool(cfg, "pull.rebase", strategy == PullStrategy::Rebase ? 1 : 0);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}
```

- [ ] **Step 4: Build and run**

Run:
```bash
cmake --build build --parallel
./build/tests/gittide_core_tests "[strategy]"
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_sync.cpp
git commit -m "feat(core): pull strategy persisted via git config (pull.rebase)"
```

---

## Task 5: GitRepo::pull (fast-forward + rebase)

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_sync.cpp`

**Interfaces:**
- Consumes: `fetch` (Task 3), `pullStrategy` (Task 4), `Credentials`, `ProgressCallback`.
- Produces: `Expected<void> GitRepo::pull(Credentials cred, ProgressCallback cb);`
  - Fetches the current branch's upstream remote, then per `pullStrategy()`:
    - **FastForwardOnly:** if analysis says fast-forwardable, advance the branch ref + checkout; else error.
    - **Rebase:** rebase local commits onto the upstream; on conflict, abort and error.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("pull fast-forwards a behind branch", "[sync][pull]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    TempRepo other;
    other.cloneFrom(bare);
    other.setIdentity("Other", "o@example.com");
    other.writeFile("b.txt", "two");
    other.commitAll("c2");
    other.pushBranch("origin", "master");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::FastForwardOnly));
    REQUIRE(gr->pull(gittide::Credentials{}, [](unsigned, unsigned) { return true; }));

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->behind == 0);
    REQUIRE(st->ahead == 0);
}

TEST_CASE("pull fast-forward fails on a diverged branch", "[sync][pull]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    TempRepo other;
    other.cloneFrom(bare);
    other.setIdentity("Other", "o@example.com");
    other.writeFile("b.txt", "remote");
    other.commitAll("remote-c2");
    other.pushBranch("origin", "master");

    // Local diverges with its own c2.
    repo.writeFile("c.txt", "local");
    repo.commitAll("local-c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::FastForwardOnly));
    auto r = gr->pull(gittide::Credentials{}, [](unsigned, unsigned) { return true; });
    REQUIRE_FALSE(r); // cannot fast-forward
}

TEST_CASE("pull rebase replays local commits onto upstream", "[sync][pull]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    TempRepo other;
    other.cloneFrom(bare);
    other.setIdentity("Other", "o@example.com");
    other.writeFile("b.txt", "remote");
    other.commitAll("remote-c2");
    other.pushBranch("origin", "master");

    repo.writeFile("c.txt", "local");
    repo.commitAll("local-c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->setPullStrategy(gittide::PullStrategy::Rebase));
    REQUIRE(gr->pull(gittide::Credentials{}, [](unsigned, unsigned) { return true; }));

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->behind == 0);
    REQUIRE(st->ahead == 1); // local-c2 replayed on top of remote-c2
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `pull` not declared.

- [ ] **Step 3: Declare + implement**

`gitrepo.hpp`:

```cpp
    // Fetch the current branch's upstream remote, then reconcile per
    // pullStrategy(): fast-forward (error if not fast-forwardable) or rebase
    // local commits onto the upstream (abort + error on conflict). HEAD must be
    // on a branch with an upstream.
    Expected<void> pull(Credentials cred, ProgressCallback cb);
```

`gitrepo.cpp` (add `#include <git2/annotated_commit.h>`, `#include <git2/merge.h>`, `#include <git2/rebase.h>`):

```cpp
Expected<void> GitRepo::pull(Credentials cred, ProgressCallback cb)
{
    // Resolve upstream + remote name from current branch.
    auto st = syncStatus();
    if (!st)
        return std::unexpected(st.error());
    if (!st->hasUpstream)
        return std::unexpected(GitError{GitError::Code::Generic, "current branch has no upstream"});

    auto fr = fetch(st->remoteName, cred, cb);
    if (!fr)
        return std::unexpected(fr.error());

    // Recompute after fetch; the upstream ref now points at the fetched tip.
    git_reference* head = nullptr;
    int rc = git_repository_head(&head, m_repo);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> head_guard(head, git_reference_free);

    git_reference* upstream = nullptr;
    rc = git_branch_upstream(&upstream, head);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_reference, decltype(&git_reference_free)> up_guard(upstream, git_reference_free);

    git_annotated_commit* upstream_ac = nullptr;
    rc = git_annotated_commit_from_ref(&upstream_ac, m_repo, upstream);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_annotated_commit, decltype(&git_annotated_commit_free)> ac_guard(upstream_ac, git_annotated_commit_free);

    auto strat = pullStrategy();
    if (!strat)
        return std::unexpected(strat.error());

    if (*strat == PullStrategy::FastForwardOnly)
    {
        git_merge_analysis_t analysis = GIT_MERGE_ANALYSIS_NONE;
        git_merge_preference_t pref   = GIT_MERGE_PREFERENCE_NONE;
        const git_annotated_commit* heads[] = {upstream_ac};
        rc = git_merge_analysis(&analysis, &pref, m_repo, heads, 1);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE)
            return {};
        if (!(analysis & GIT_MERGE_ANALYSIS_FASTFORWARD))
            return std::unexpected(GitError{GitError::Code::Generic, "cannot fast-forward: branch has diverged"});

        // Move HEAD's branch ref to the upstream OID and checkout.
        const git_oid* target = git_annotated_commit_id(upstream_ac);
        git_object* target_obj = nullptr;
        rc = git_object_lookup(&target_obj, m_repo, target, GIT_OBJECT_COMMIT);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_object, decltype(&git_object_free)> obj_guard(target_obj, git_object_free);

        git_checkout_options co = GIT_CHECKOUT_OPTIONS_INIT;
        co.checkout_strategy    = GIT_CHECKOUT_SAFE;
        rc = git_checkout_tree(m_repo, target_obj, &co);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));

        git_reference* new_ref = nullptr;
        rc = git_reference_set_target(&new_ref, head, target, "pull: fast-forward");
        if (new_ref)
            git_reference_free(new_ref);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        return {};
    }

    // Rebase local commits onto the upstream.
    git_rebase* rebase = nullptr;
    git_rebase_options ropts = GIT_REBASE_OPTIONS_INIT;
    rc = git_rebase_init(&rebase, m_repo, /*branch=*/nullptr, upstream_ac, /*onto=*/nullptr, &ropts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_rebase, decltype(&git_rebase_free)> rebase_guard(rebase, git_rebase_free);

    git_rebase_operation* op = nullptr;
    while ((rc = git_rebase_next(&op, rebase)) == 0)
    {
        git_index* idx = nullptr;
        if (git_repository_index(&idx, m_repo) == 0)
        {
            bool conflicts = git_index_has_conflicts(idx) == 1;
            git_index_free(idx);
            if (conflicts)
            {
                git_rebase_abort(rebase);
                return std::unexpected(GitError{GitError::Code::Generic, "pull rebase hit conflicts; resolve via CLI"});
            }
        }
        git_oid commit_id;
        git_signature* sig = nullptr;
        if (git_signature_default(&sig, m_repo) < 0)
        {
            git_rebase_abort(rebase);
            return std::unexpected(GitError{GitError::Code::Generic, "no committer identity (set user.name/user.email)"});
        }
        rc = git_rebase_commit(&commit_id, rebase, nullptr, sig, nullptr, nullptr);
        git_signature_free(sig);
        if (rc < 0 && rc != GIT_EAPPLIED)
        {
            git_rebase_abort(rebase);
            return std::unexpected(lastGitError(rc));
        }
    }
    if (rc != GIT_ITEROVER)
    {
        git_rebase_abort(rebase);
        return std::unexpected(lastGitError(rc));
    }
    rc = git_rebase_finish(rebase, nullptr);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    return {};
}
```

> **GitError construction note:** match `GitError`'s actual constructor/fields. Inspect `core/include/gittide/giterror.hpp` and use the same idiom the rest of `gitrepo.cpp` uses to build a synthetic (non-libgit2) error. If there is no `Code::Generic`, use whatever generic/unknown code the enum defines. Do **not** invent fields.

- [ ] **Step 4: Build and run**

Run:
```bash
cmake --build build --parallel
./build/tests/gittide_core_tests "[pull]"
```
Expected: all 3 `[pull]` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_sync.cpp
git commit -m "feat(core): GitRepo::pull (fast-forward + rebase)"
```

---

## Task 6: GitRepo::push (+ publish / set upstream)

**Files:**
- Modify: `core/include/gittide/gitrepo.hpp`, `core/src/gitrepo.cpp`
- Test: `tests/test_git_repo_sync.cpp`

**Interfaces:**
- Consumes: `Credentials`, `ProgressCallback`, credential trampoline (Task 3).
- Produces: `Expected<void> GitRepo::push(std::string remoteName, std::string branch, bool setUpstream, Credentials cred, ProgressCallback cb);`
  - Pushes `refs/heads/<branch>:refs/heads/<branch>`. When `setUpstream`, sets the branch upstream to `<remote>/<branch>` after success.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("push sends local commits to the remote and clears ahead", "[sync][push]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master"); // baseline + upstream

    repo.writeFile("a.txt", "two");
    repo.commitAll("c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    REQUIRE(gr->push("origin", "master", /*setUpstream=*/false, gittide::Credentials{},
                     [](unsigned, unsigned) { return true; }));

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->ahead == 0);
}

TEST_CASE("push with setUpstream publishes a branch with no upstream", "[sync][push]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin"); // remote exists, but no upstream set, nothing pushed

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);

    auto before = gr->syncStatus();
    REQUIRE(before);
    REQUIRE_FALSE(before->hasUpstream);

    REQUIRE(gr->push("origin", "master", /*setUpstream=*/true, gittide::Credentials{},
                     [](unsigned, unsigned) { return true; }));

    auto after = gr->syncStatus();
    REQUIRE(after);
    REQUIRE(after->hasUpstream);
    REQUIRE(after->ahead == 0);
    REQUIRE(after->behind == 0);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --parallel`
Expected: FAIL — `push` not declared.

- [ ] **Step 3: Declare + implement**

`gitrepo.hpp`:

```cpp
    // Push refs/heads/<branch> to remoteName. When setUpstream is true, set the
    // branch's upstream to <remoteName>/<branch> after a successful push
    // ("publish"). cred supplied by the caller; cb reports progress.
    Expected<void> push(std::string remoteName, std::string branch, bool setUpstream,
                        Credentials cred, ProgressCallback cb);
```

`gitrepo.cpp` (add `#include <git2/push.h>` if not already via `<git2.h>`):

```cpp
Expected<void> GitRepo::push(std::string remoteName, std::string branch, bool setUpstream,
                             Credentials cred, ProgressCallback cb)
{
    git_remote* raw = nullptr;
    int rc = git_remote_lookup(&raw, m_repo, remoteName.c_str());
    if (rc < 0)
        return std::unexpected(lastGitError(rc));
    std::unique_ptr<git_remote, decltype(&git_remote_free)> remote(raw, git_remote_free);

    std::string ref     = "refs/heads/" + branch;
    std::string refspec = ref + ":" + ref;
    char* specs[]       = {refspec.data()};
    git_strarray arr    = {specs, 1};

    CbPayload pl{&cb, &cred};
    git_push_options opts          = GIT_PUSH_OPTIONS_INIT;
    opts.callbacks.credentials     = credentialTrampoline;
    opts.callbacks.push_transfer_progress = nullptr; // progress reported via transfer hook on fetch; push uses sideband only
    opts.callbacks.payload         = &pl;

    rc = git_remote_push(remote.get(), &arr, &opts);
    if (rc < 0)
        return std::unexpected(lastGitError(rc));

    if (setUpstream)
    {
        git_reference* branch_ref = nullptr;
        rc = git_branch_lookup(&branch_ref, m_repo, branch.c_str(), GIT_BRANCH_LOCAL);
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
        std::unique_ptr<git_reference, decltype(&git_reference_free)> br_guard(branch_ref, git_reference_free);
        std::string upstream = remoteName + "/" + branch;
        rc = git_branch_set_upstream(branch_ref, upstream.c_str());
        if (rc < 0)
            return std::unexpected(lastGitError(rc));
    }
    return {};
}
```

- [ ] **Step 4: Build and run**

Run:
```bash
cmake --build build --parallel
./build/tests/gittide_core_tests "[push]"
./build/tests/gittide_core_tests "[sync]"   # full sync suite green
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/include/gittide/gitrepo.hpp core/src/gitrepo.cpp tests/test_git_repo_sync.cpp
git commit -m "feat(core): GitRepo::push with publish/set-upstream"
```

---

## Task 7: AsyncRepo wrappers

**Files:**
- Modify: `ui/include/gittide/ui/asyncrepo.hpp`, `ui/src/asyncrepo.cpp`

**Interfaces:**
- Consumes: `GitRepo::syncStatus/fetch/pull/push/pullStrategy/setPullStrategy` (Tasks 2–6), `SyncStatus`, `PullStrategy`, `Credentials`.
- Produces (all `QCoro::Task<...>`):
  - `syncStatus() -> Expected<SyncStatus>`
  - `fetch(QString remote, Credentials cred) -> Expected<void>`
  - `pull(Credentials cred) -> Expected<void>`
  - `push(QString remote, QString branch, bool setUpstream, Credentials cred) -> Expected<void>`
  - `pullStrategy() -> Expected<PullStrategy>`
  - `setPullStrategy(PullStrategy) -> Expected<void>`
  - (Progress callback: pass a no-op `[](unsigned,unsigned){return true;}` for now — progress UI reuses clone dialog later; not wired here.)

- [ ] **Step 1: Add declarations**

In `asyncrepo.hpp`, add `#include "gittide/sync.hpp"` and, in the public section:

```cpp
    QCoro::Task<gittide::Expected<gittide::SyncStatus>> syncStatus();
    QCoro::Task<gittide::Expected<void>>                fetch(QString remote, gittide::Credentials cred);
    QCoro::Task<gittide::Expected<void>>                pull(gittide::Credentials cred);
    QCoro::Task<gittide::Expected<void>>                push(QString remote, QString branch, bool setUpstream, gittide::Credentials cred);
    QCoro::Task<gittide::Expected<gittide::PullStrategy>> pullStrategy();
    QCoro::Task<gittide::Expected<void>>                setPullStrategy(gittide::PullStrategy strategy);
```

- [ ] **Step 2: Implement (follow the existing QtConcurrent::run pattern)**

In `asyncrepo.cpp`:

```cpp
QCoro::Task<gittide::Expected<gittide::SyncStatus>> AsyncRepo::syncStatus()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.syncStatus();
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::fetch(QString remote, gittide::Credentials cred)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, remote = remote.toStdString(), cred = std::move(cred)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.fetch(remote, cred, [](unsigned, unsigned) { return true; });
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::pull(gittide::Credentials cred)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, cred = std::move(cred)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.pull(cred, [](unsigned, unsigned) { return true; });
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::push(QString remote, QString branch, bool setUpstream, gittide::Credentials cred)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, remote = remote.toStdString(), branch = branch.toStdString(), setUpstream, cred = std::move(cred)]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.push(remote, branch, setUpstream, cred, [](unsigned, unsigned) { return true; });
        });
}

QCoro::Task<gittide::Expected<gittide::PullStrategy>> AsyncRepo::pullStrategy()
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.pullStrategy();
        });
}

QCoro::Task<gittide::Expected<void>> AsyncRepo::setPullStrategy(gittide::PullStrategy strategy)
{
    auto impl = m_impl;
    co_return co_await QtConcurrent::run(
        [impl, strategy]()
        {
            std::scoped_lock lock(impl->mutex);
            return impl->repo.setPullStrategy(strategy);
        });
}
```

- [ ] **Step 3: Build (no new test; exercised via RepoController tests later)**

Run: `cmake --build build --parallel -DGITGUI_BUILD_QML=ON` (or reuse existing configured build dir).
Expected: compiles clean.

- [ ] **Step 4: Commit**

```bash
git add ui/include/gittide/ui/asyncrepo.hpp ui/src/asyncrepo.cpp
git commit -m "feat(ui): AsyncRepo wrappers for sync ops"
```

---

## Task 8: RepoController orchestration + signals + metatypes

**Files:**
- Modify: `ui/include/gittide/ui/repocontroller.hpp`, `ui/src/repocontroller.cpp`

**Interfaces:**
- Consumes: AsyncRepo sync methods (Task 7).
- Produces (slots):
  - `QCoro::Task<void> refreshSyncStatus();`
  - `QCoro::Task<void> fetch(gittide::Credentials cred);`
  - `QCoro::Task<void> pull(gittide::Credentials cred);`
  - `QCoro::Task<void> push(QString branch, bool setUpstream, gittide::Credentials cred);`
  - `QCoro::Task<void> loadPullStrategy();`
  - `QCoro::Task<void> setPullStrategy(gittide::PullStrategy strategy);`
- Produces (signals):
  - `void syncStatusChanged(gittide::SyncStatus status);`
  - `void pullStrategyChanged(gittide::PullStrategy strategy);`
  - `void syncBusyChanged(bool busy);`
  - `void authFailed(QString remoteUrl);` — emitted when a network op returns an auth error.
- Behaviour: each network op emits `syncBusyChanged(true)` at entry, `(false)` at exit; on success it chains `refreshStatus` + `refreshHistory` + `refreshBranches` + `refreshSyncStatus`; on failure emits `operationFailed` (or `authFailed` when the error is an auth error).

- [ ] **Step 1: Declare in the header**

Add the slots and signals above to `repocontroller.hpp` (include `gittide/sync.hpp`). The `fetch`/`pull`/`push` slots take `Credentials` **by value** (coroutine-safe).

- [ ] **Step 2: Register metatypes**

In `repocontroller.cpp`'s metatype registration block (alongside `qRegisterMetaType<gittide::HeadState>()`), add:

```cpp
    qRegisterMetaType<gittide::SyncStatus>();
    qRegisterMetaType<gittide::PullStrategy>();
    qRegisterMetaType<gittide::Credentials>();
```

- [ ] **Step 3: Implement the slots**

```cpp
QCoro::Task<void> RepoController::refreshSyncStatus()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->syncStatus();
    if (!self)
        co_return;
    if (!r)
    {
        emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    emit syncStatusChanged(*r);
}

QCoro::Task<void> RepoController::fetch(gittide::Credentials cred)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    emit syncBusyChanged(true);
    auto r = co_await m_repo->fetch(QStringLiteral("origin"), cred);
    if (!self)
        co_return;
    emit syncBusyChanged(false);
    if (!r)
    {
        if (isAuthError(r.error()))
            emit authFailed(QString()); // url not surfaced by libgit2 here; UI prompts generically
        else
            emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::pull(gittide::Credentials cred)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    emit syncBusyChanged(true);
    auto r = co_await m_repo->pull(cred);
    if (!self)
        co_return;
    emit syncBusyChanged(false);
    if (!r)
    {
        if (isAuthError(r.error()))
            emit authFailed(QString());
        else
            emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshStatus();
    co_await refreshHistory();
    co_await refreshBranches();
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::push(QString branch, bool setUpstream, gittide::Credentials cred)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    emit syncBusyChanged(true);
    auto r = co_await m_repo->push(QStringLiteral("origin"), branch, setUpstream, cred);
    if (!self)
        co_return;
    emit syncBusyChanged(false);
    if (!r)
    {
        if (isAuthError(r.error()))
            emit authFailed(QString());
        else
            emit operationFailed(QString::fromStdString(r.error().message));
        co_return;
    }
    co_await refreshBranches();
    co_await refreshSyncStatus();
}

QCoro::Task<void> RepoController::loadPullStrategy()
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->pullStrategy();
    if (!self || !r)
        co_return;
    emit pullStrategyChanged(*r);
}

QCoro::Task<void> RepoController::setPullStrategy(gittide::PullStrategy strategy)
{
    if (!m_repo)
        co_return;
    QPointer<RepoController> self = this;
    auto r = co_await m_repo->setPullStrategy(strategy);
    if (!self || !r)
        co_return;
    emit pullStrategyChanged(strategy);
}
```

Add a small helper near the top of `repocontroller.cpp`:

```cpp
namespace {
bool isAuthError(const gittide::GitError& e)
{
    // libgit2 maps auth failures to GIT_EAUTH; match whatever GitError exposes.
    // Fall back to a substring check on the message if no code is available.
    return e.message.find("authentication") != std::string::npos
        || e.message.find("401") != std::string::npos
        || e.message.find("403") != std::string::npos;
}
} // namespace
```

> **GitError note:** prefer a real error-code comparison if `GitError` carries the libgit2 class/code (inspect `giterror.hpp`). Only use the substring fallback if no code field exists. Adjust to the actual type.

Also: in `RepoController::open` (or wherever the repo is set up after open), kick `loadPullStrategy()` and `refreshSyncStatus()` so the UI starts with correct state. Match the existing post-open refresh pattern.

- [ ] **Step 4: Build**

Run: `cmake --build build --parallel`
Expected: compiles clean.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gittide/ui/repocontroller.hpp ui/src/repocontroller.cpp
git commit -m "feat(ui): RepoController sync orchestration, signals, metatypes"
```

---

## Task 9: RepoViewModel — properties, invokables, session credentials, auth retry

**Files:**
- Modify: `ui/include/gittide/ui/repoviewmodel.hpp`, `ui/src/repoviewmodel.cpp`

**Interfaces:**
- Consumes: RepoController sync slots + signals (Task 8).
- Produces (Q_PROPERTY, all NOTIFY):
  - `int aheadCount`, `int behindCount`, `bool hasUpstream`, `QString upstreamName`, `bool syncing`, `bool pullRebase`.
- Produces (Q_INVOKABLE):
  - `void fetch()`, `void pull()`, `void push()`, `void publishBranch()`,
  - `void submitCredentials(const QString& username, const QString& token)`,
  - `void setPullRebase(bool rebase)`.
- Produces (signals): `void syncStatusChanged()`, `void syncingChanged()`, `void pullRebaseChanged()`, `void authRequired()`.

- [ ] **Step 1: Add properties + signals + invokables to the header**

```cpp
    Q_PROPERTY(int aheadCount READ aheadCount NOTIFY syncStatusChanged)
    Q_PROPERTY(int behindCount READ behindCount NOTIFY syncStatusChanged)
    Q_PROPERTY(bool hasUpstream READ hasUpstream NOTIFY syncStatusChanged)
    Q_PROPERTY(QString upstreamName READ upstreamName NOTIFY syncStatusChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(bool pullRebase READ pullRebase NOTIFY pullRebaseChanged)
    // ... getters ...
    int aheadCount() const { return m_sync.ahead; }
    int behindCount() const { return m_sync.behind; }
    bool hasUpstream() const { return m_sync.hasUpstream; }
    QString upstreamName() const { return QString::fromStdString(m_sync.upstreamName); }
    bool syncing() const { return m_syncing; }
    bool pullRebase() const { return m_pullRebase; }

    Q_INVOKABLE void fetch();
    Q_INVOKABLE void pull();
    Q_INVOKABLE void push();
    Q_INVOKABLE void publishBranch();
    Q_INVOKABLE void submitCredentials(const QString& username, const QString& token);
    Q_INVOKABLE void setPullRebase(bool rebase);

signals:
    // ... existing ...
    void syncStatusChanged();
    void syncingChanged();
    void pullRebaseChanged();
    void authRequired();
```

Add members:
```cpp
    gittide::SyncStatus m_sync;
    bool                m_syncing    = false;
    bool                m_pullRebase = false;
    gittide::Credentials m_sessionCred;   // session-only token cache
    enum class PendingOp { None, Fetch, Pull, Push, Publish } m_pendingOp = PendingOp::None;
```

- [ ] **Step 2: Connect controller signals in the constructor**

Where the constructor already `connect`s `m_controller` signals (e.g. `headChanged` → `onHead`), add:

```cpp
    connect(m_controller, &RepoController::syncStatusChanged, this,
            [this](gittide::SyncStatus s) { m_sync = s; emit syncStatusChanged(); });
    connect(m_controller, &RepoController::syncBusyChanged, this,
            [this](bool b) { m_syncing = b; emit syncingChanged(); });
    connect(m_controller, &RepoController::pullStrategyChanged, this,
            [this](gittide::PullStrategy s) { m_pullRebase = (s == gittide::PullStrategy::Rebase); emit pullRebaseChanged(); });
    connect(m_controller, &RepoController::authFailed, this,
            [this](QString) { emit authRequired(); });
```

- [ ] **Step 3: Implement the invokables**

```cpp
void RepoViewModel::fetch()
{
    m_pendingOp = PendingOp::Fetch;
    QCoro::connect(m_controller->fetch(m_sessionCred), this, [] {});
}

void RepoViewModel::pull()
{
    m_pendingOp = PendingOp::Pull;
    QCoro::connect(m_controller->pull(m_sessionCred), this, [] {});
}

void RepoViewModel::push()
{
    m_pendingOp = PendingOp::Push;
    QCoro::connect(m_controller->push(m_branch, /*setUpstream=*/false, m_sessionCred), this, [] {});
}

void RepoViewModel::publishBranch()
{
    m_pendingOp = PendingOp::Publish;
    QCoro::connect(m_controller->push(m_branch, /*setUpstream=*/true, m_sessionCred), this, [] {});
}

void RepoViewModel::submitCredentials(const QString& username, const QString& token)
{
    m_sessionCred.username    = username.toStdString();
    m_sessionCred.password    = token.toStdString();
    m_sessionCred.sshUseAgent = true;
    // Retry the operation that triggered authRequired.
    switch (m_pendingOp)
    {
    case PendingOp::Fetch:   fetch(); break;
    case PendingOp::Pull:    pull(); break;
    case PendingOp::Push:    push(); break;
    case PendingOp::Publish: publishBranch(); break;
    case PendingOp::None:    break;
    }
}

void RepoViewModel::setPullRebase(bool rebase)
{
    QCoro::connect(m_controller->setPullStrategy(rebase ? gittide::PullStrategy::Rebase
                                                        : gittide::PullStrategy::FastForwardOnly),
                   this, [] {});
}
```

> `m_branch` is the existing current-branch member used elsewhere in the ViewModel. Confirm its name in `repoviewmodel.hpp` and reuse it; do not add a duplicate.

- [ ] **Step 4: Build**

Run: `cmake --build build --parallel`
Expected: compiles clean.

- [ ] **Step 5: Commit**

```bash
git add ui/include/gittide/ui/repoviewmodel.hpp ui/src/repoviewmodel.cpp
git commit -m "feat(ui): RepoViewModel sync props, invokables, session creds + auth retry"
```

---

## Task 10: UI test — ahead/behind property mapping + pullRebase round-trip

**Files:**
- Create: `tests/ui/test_qml_sync.cpp`
- Modify: `tests/CMakeLists.txt` (add to the `gittide_ui_tests` HEADER_FILE_ONLY-included list per the existing convention; see Plan 5 CMake note)

**Interfaces:**
- Consumes: RepoViewModel + RepoController (Tasks 8–9), TempRepo bare-remote infra (Task 2).

> **CMake convention (from Plan 5):** UI test slot files are `#include`d into `ui/main.cpp` and listed `HEADER_FILE_ONLY`. Follow the same pattern this file's siblings (`test_qml_history.cpp`) use. `tests/support/temprepo.cpp` is already a compiled source in `add_executable(gittide_ui_tests)`.

- [ ] **Step 1: Write the failing test (Qt Test slot class, mirroring TestQmlHistory)**

```cpp
// tests/ui/test_qml_sync.cpp
#include <QSignalSpy>
#include <QtTest>

#include "gittide/ui/repoviewmodel.hpp"
#include "support/temprepo.hpp"

class TestQmlSync : public QObject
{
    Q_OBJECT
private slots:
    void aheadBehindMapToProperties();
    void pullRebaseRoundTrips();
};

void TestQmlSync::aheadBehindMapToProperties()
{
    using namespace gittide::test;
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");
    repo.writeFile("a.txt", "two");
    repo.commitAll("c2"); // ahead 1

    gittide::ui::RepoViewModel vm;
    QSignalSpy spy(&vm, &gittide::ui::RepoViewModel::syncStatusChanged);
    vm.open(QString::fromStdString(repo.path().generic_string()));
    QVERIFY(spy.wait(5000));
    QCOMPARE(vm.aheadCount(), 1);
    QCOMPARE(vm.behindCount(), 0);
    QVERIFY(vm.hasUpstream());
}

void TestQmlSync::pullRebaseRoundTrips()
{
    using namespace gittide::test;
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");

    gittide::ui::RepoViewModel vm;
    vm.open(QString::fromStdString(repo.path().generic_string()));
    QSignalSpy spy(&vm, &gittide::ui::RepoViewModel::pullRebaseChanged);
    vm.setPullRebase(true);
    QVERIFY(spy.wait(5000));
    QVERIFY(vm.pullRebase());
}

QTEST_MAIN_WRAPPER_OR_INCLUDE(TestQmlSync) // match the macro/registration style used by test_qml_history.cpp
#include "test_qml_sync.moc"
```

> The final registration line is a placeholder for the exact mechanism `test_qml_history.cpp` uses (it is `#include`d into `ui/main.cpp`). Copy that file's footer/registration verbatim, substituting `TestQmlSync`.

- [ ] **Step 2: Register and run to verify it fails**

Add `test_qml_sync.cpp` to the UI test list in `tests/CMakeLists.txt` (same place as `test_qml_history.cpp`).

Run:
```bash
cmake --build build --parallel
ctest --test-dir build -R TestQmlSync --output-on-failure
```
Expected: FAIL (or build error) until Tasks 8–9 are correctly wired — then iterate.

- [ ] **Step 3: Make it pass**

Fix any wiring gaps surfaced (e.g. `open` must trigger `refreshSyncStatus`; `loadPullStrategy` connected). Re-run until green.

- [ ] **Step 4: Run full suites**

Run:
```bash
./build/tests/gittide_core_tests "[sync]"
ctest --test-dir build --output-on-failure
```
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/test_qml_sync.cpp tests/CMakeLists.txt
git commit -m "test(ui): sync property mapping + pullRebase round-trip"
```

---

## Task 11: QML — BranchBar sync cluster

**Files:**
- Modify: `ui/qml/BranchBar.qml`

**Interfaces:**
- Consumes: `repoVm` context property — `aheadCount`, `behindCount`, `hasUpstream`, `syncing`, `pullRebase`, `fetch()`, `pull()`, `push()`, `publishBranch()`, `setPullRebase()`.

- [ ] **Step 1: Add the sync cluster to BranchBar's RowLayout**

After the branch chip (and any spacer that pushes content right — add `Item { Layout.fillWidth: true }` if not present), add a right-aligned cluster. All colours via `theme.*` tokens; badges hidden when count is 0; buttons disabled while `repoVm.syncing`.

```qml
        // ---- right-aligned sync cluster ----
        Item { Layout.fillWidth: true }

        // Fetch
        ToolButton {
            text: "Fetch"
            enabled: repoVm && !repoVm.syncing
            onClicked: repoVm.fetch()
        }

        // Pull (with behind badge) — shown only when there is an upstream
        ToolButton {
            visible: repoVm && repoVm.hasUpstream
            enabled: repoVm && !repoVm.syncing
            text: "Pull"
            onClicked: repoVm.pull()
            Rectangle {
                visible: repoVm && repoVm.behindCount > 0
                anchors.right: parent.right
                anchors.top: parent.top
                width: badgeLabel1.implicitWidth + 8; height: 16; radius: 8
                color: theme.accent
                Label { id: badgeLabel1; anchors.centerIn: parent
                        text: repoVm ? repoVm.behindCount : 0
                        color: theme.textOnAccent; font.pixelSize: 10 }
            }
        }

        // Push (with ahead badge) — shown only when there is an upstream
        ToolButton {
            visible: repoVm && repoVm.hasUpstream
            enabled: repoVm && !repoVm.syncing
            text: "Push"
            onClicked: repoVm.push()
            Rectangle {
                visible: repoVm && repoVm.aheadCount > 0
                anchors.right: parent.right
                anchors.top: parent.top
                width: badgeLabel2.implicitWidth + 8; height: 16; radius: 8
                color: theme.accent
                Label { id: badgeLabel2; anchors.centerIn: parent
                        text: repoVm ? repoVm.aheadCount : 0
                        color: theme.textOnAccent; font.pixelSize: 10 }
            }
        }

        // Publish — shown only when the branch has no upstream
        ToolButton {
            visible: repoVm && !repoVm.hasUpstream
            enabled: repoVm && !repoVm.syncing
            text: "Publish branch"
            onClicked: repoVm.publishBranch()
        }

        // Busy spinner
        BusyIndicator {
            running: repoVm && repoVm.syncing
            visible: running
            implicitWidth: 20; implicitHeight: 20
        }
```

> Use the actual accent-on-text token name from `qmltheme.hpp` (e.g. `theme.textOnAccent` or `theme.background`); confirm the token and substitute. If `ToolButton` is not the established button style in this codebase, mirror the button pattern already used in `BranchBar.qml` / dialogs (it uses `QtQuick.Controls.Basic`).

- [ ] **Step 2: Lint + load**

Run:
```bash
/home/michal/Qt/6.8.3/gcc_64/bin/qmllint ui/qml/BranchBar.qml
```
Expected: only the known unqualified-access notes for `theme`/`repoVm` context props.

- [ ] **Step 3: Visual smoke (manual)**

```bash
DISPLAY=:1 ./build/app/gittide_qml_app
```
Confirm Fetch/Pull/Push/Publish render, badges show counts, spinner appears during an op. (Network ops need a real remote; at minimum Publish/Fetch buttons must render and be clickable.)

- [ ] **Step 4: Commit**

```bash
git add ui/qml/BranchBar.qml
git commit -m "feat(ui): BranchBar sync cluster — fetch/pull/push/publish + badges"
```

---

## Task 12: QML — CredentialDialog + pull-strategy toggle + wiring

**Files:**
- Create: `ui/qml/CredentialDialog.qml`
- Modify: `ui/qml/Main.qml`, `ui/qml/qml.qrc`, `ui/qml/BranchBar.qml`

**Interfaces:**
- Consumes: `repoVm.authRequired` signal, `repoVm.submitCredentials(user, token)`, `repoVm.pullRebase`, `repoVm.setPullRebase(bool)`.

- [ ] **Step 1: Create CredentialDialog.qml**

```qml
// ui/qml/CredentialDialog.qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    title: "Authentication required"
    standardButtons: Dialog.Ok | Dialog.Cancel

    property alias username: userField.text
    property alias token: tokenField.text

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Label { text: "HTTPS username"; color: theme.textPrimary }
        TextField { id: userField; Layout.fillWidth: true }
        Label { text: "Personal access token"; color: theme.textPrimary }
        TextField { id: tokenField; Layout.fillWidth: true; echoMode: TextInput.Password }
        Label {
            text: "Stored for this session only."
            color: theme.textMuted
            font.pixelSize: 11
        }
    }

    onAccepted: repoVm.submitCredentials(userField.text, tokenField.text)
}
```

- [ ] **Step 2: Register in qml.qrc**

Add `<file>CredentialDialog.qml</file>` to `ui/qml/qml.qrc` alongside the other dialogs.

- [ ] **Step 3: Host + wire in Main.qml**

In `Main.qml`, instantiate the dialog and open it on `authRequired`:

```qml
    CredentialDialog { id: credentialDialog }

    Connections {
        target: repoVm
        function onAuthRequired() { credentialDialog.open() }
    }
```

- [ ] **Step 4: Add the pull-strategy toggle**

In `BranchBar.qml`, add a small menu button near the sync cluster:

```qml
        ToolButton {
            text: "⋯"
            onClicked: pullMenu.open()
            Menu {
                id: pullMenu
                MenuItem {
                    text: "Pull: rebase"
                    checkable: true
                    checked: repoVm && repoVm.pullRebase
                    onToggled: repoVm.setPullRebase(checked)
                }
            }
        }
```

- [ ] **Step 5: Lint + load + smoke**

Run:
```bash
/home/michal/Qt/6.8.3/gcc_64/bin/qmllint ui/qml/CredentialDialog.qml ui/qml/BranchBar.qml ui/qml/Main.qml
cmake --build build --parallel
DISPLAY=:1 ./build/app/gittide_qml_app
```
Expected: lint clean (bar known context-prop notes); app loads; the pull-rebase toggle reflects/sets state; triggering an auth-required op opens the dialog.

- [ ] **Step 6: Commit**

```bash
git add ui/qml/CredentialDialog.qml ui/qml/Main.qml ui/qml/qml.qrc ui/qml/BranchBar.qml
git commit -m "feat(ui): credential dialog + pull-strategy toggle, wired to authRequired"
```

---

## Task 13: Spec + docs + memory update

**Files:**
- Modify: `docs/spec/product/product.md`
- Modify: `docs/spec/engineering/engineering.md` (or the async-model subsection)
- Modify: `docs/plans/index.md`
- Modify: `~/.claude/projects/-home-michal-Documents-gittide/memory/qml-ui-migration.md`

- [ ] **Step 1: product.md** — Move push/pull/fetch out of the out-of-scope list (line ~42 / ~108). Add a short "Syncing" subsection: ahead/behind on the branch bar; Fetch/Pull/Push/Publish; pull strategy (FF-only or rebase) persisted in git config; conflicts during rebase abort with an error (CLI resolution for now).

- [ ] **Step 2: engineering.md** — Add a note: network ops run through the AsyncRepo worker model; **credentials are pre-supplied** by the ViewModel as a `Credentials` POD (ssh-agent + HTTPS session token), selected for libgit2 by URL scheme in a pure helper; the credential callback never blocks for UI. Tokens are session-only (secure persistence deferred).

- [ ] **Step 3: plans/index.md** — Add a row for this plan (date 2026-06-21, status done, realises product · engineering · design).

- [ ] **Step 4: memory** — Update `qml-ui-migration.md`: mark Fetch/Pull/Push done with the merge commit; note deferred items (secure token storage, merge strategy, rebase/merge conflict UI, SSH keyfile+passphrase, multi-remote, fetch-all/pull-all).

- [ ] **Step 5: Commit**

```bash
git add docs/spec/product/product.md docs/spec/engineering/engineering.md docs/plans/index.md
git commit -m "docs(spec): fetch/pull/push + ahead/behind syncing"
```

---

## Final verification

- [ ] `cmake --build build --parallel` — clean build.
- [ ] `./build/tests/gittide_core_tests "[sync]"` and `"[cred]"` — all green.
- [ ] `ctest --test-dir build --output-on-failure` — full suite green.
- [ ] `qmllint` on all changed/new QML — only known context-prop notes.
- [ ] Manual: `DISPLAY=:1 ./build/app/gittide_qml_app` — sync cluster renders; pull-strategy toggle works; auth dialog opens on an auth-required op. Real push/pull against a GitHub remote (SSH + HTTPS token) verified manually.
- [ ] Merge to master following the established merge-commit convention.
