# Engineering

How GitTide is built: the layered architecture, the rules that hold across the
whole codebase, the async model, and the build/test setup.

**Scope of this section.** It describes *cross-cutting* design — things that span
many files and are not derivable from any single one. It is deliberately **not**
an API reference: the contract of a class or function lives in **Doxygen
comments next to that symbol**. When you want "what does `GitRepo::stage` do",
read [`core/include/gittide/gitrepo.hpp`](../../../core/include/gittide/gitrepo.hpp);
when you want "why is `core/` allowed to know nothing about Qt", read on.

## Architecture

Clean layering. Dependencies point downward only; each layer is understandable
and testable on its own.

```
┌─ app/  (Qt, process-wide) ───────────────────────────┐
│  QQmlApplicationEngine loads Main.qml; shared services │
│  (ProjectStore registry, libgit2 init) live once here  │
├─ ui/   (Qt Quick/QML + ViewModels) ───────────────────┤
│  Main.qml, Sidebar, WorkingPane, HistoryPane … bound   │
│  to context props: ThemeManager/QmlTheme,              │
│  ProjectController, RepoViewModel (Qt signals; never   │
│  call libgit2). C++ models feed QML.                   │
├─ ui/   (async bridge) ────────────────────────────────┤
│  AsyncRepo — QtConcurrent + QCoro over GitRepo         │
├─ core/ (pure C++23, no Qt) ───────────────────────────┤
│  GitRepo, DiffEngine, GraphBuilder, ProjectStore       │
└─ libgit2 ──────────────────────────────────────────────┘
```

- **`core/`** — pure C++23, no Qt. The git engine and persistence: `GitRepo`
  (RAII over one `git_repository`), `DiffEngine` (libgit2 diff → display model),
  `GraphBuilder` (commit walk → lane layout), `ProjectStore` (JSON project
  registry). Namespace `gittide`. Unit-tested with Catch2, no display needed.
- **`ui/`** — **Qt Quick/QML** views, the C++ ViewModels/models they bind to
  (`ProjectController`, `RepoViewModel`, `RepoListModel`, `ChangedFilesModel`,
  `DiffLinesModel`, `BranchListModel`, `HistoryListModel`, the `GraphColumn`
  `QQuickPaintedItem`, `ThemeManager`/`QmlTheme`), and the async bridge
  `AsyncRepo`. Namespace `gittide::ui`. The static lib links **no QWidgets** —
  only Qt Gui/Qml/Quick/QuickControls2. Controllers expose Qt signals/slots and
  translate between core's `std` types and Qt's. Headless-testable with Qt Test.
- **`app/`** — process-wide composition: a `QGuiApplication` +
  `QQmlApplicationEngine` that wires the context properties and loads
  `qrc:/qml/Main.qml`, plus the single `ProjectStore` registry and one-time
  `libgit2` init ([`app/qml_main.cpp`](../../../app/qml_main.cpp)). A single
  window today; multi-window/session restore is deferred.

### Where to find what

| Concern | Code |
|---------|------|
| Git operations (status/diff/stage/commit/log/submodules/branches) | `core/src/gitrepo.cpp`, `core/include/gittide/gitrepo.hpp` |
| Diff parsing + partial-staging patch synthesis | `core/src/diffengine.cpp` |
| Commit graph lane layout | `core/src/graphbuilder.cpp` |
| Project registry persistence (JSON) | `core/src/projectstore.cpp` |
| Async / off-thread git | `ui/src/asyncrepo.cpp` |
| Controllers (ViewModels) | `ui/src/projectcontroller.cpp`, `ui/src/repocontroller.cpp`, `ui/src/repoviewmodel.cpp` |
| QML views | `ui/qml/*.qml` (loaded from `ui/qml/qml.qrc`) |
| QML context wiring (context props + type registration) | `ui/src/qmlcontext.cpp` |
| Theming (tokens → QML bindings) | `ui/src/theme.cpp`, `ui/src/thememanager.cpp`, `ui/src/qmltheme.cpp` — see [`../design/design.md`](../design/design.md) |

## Cross-cutting invariants

These hold everywhere. Breaking one is a design regression, not a local choice.
The rationale and rejected alternatives behind them are logged in
[`../../decisions.md`](../../decisions.md).

1. **No Qt in `core/`.** Core compiles and tests without Qt on the include path.
   This is what keeps the git engine unit-testable and the layering honest.
2. **libgit2 and nlohmann/json are PRIVATE to `core/`.** No public core header
   includes them — `gitrepo.hpp` only forward-declares `struct git_repository`.
   They never leak onto downstream consumers (see the `PRIVATE` linkage in
   [`core/CMakeLists.txt`](../../../core/CMakeLists.txt)). Tests that need
   `<git2.h>` link libgit2 explicitly.
3. **Core speaks `std`; Qt stays at the boundary.** `std::string` (UTF-8),
   `std::vector`, `std::filesystem::path`, `std::expected`. Qt types appear only
   in `ui/`, and conversion happens in one place at the ViewModel edge
   (`QString::fromStdString(path.generic_u8string())`).
4. **Errors are values.** Core returns `Expected<T>` =
   `std::expected<T, GitError>`; no exceptions cross a layer boundary. A
   `GitError` carries the libgit2 code + message. Worker-thread failures surface
   as a signal, then a non-intrusive UI banner — never a crash.
5. **One owner per `GitRepo`.** `GitRepo` is move-only and not thread-safe.
   Parallelism comes from each worker opening *its own* repo instance, never
   from sharing one across threads.
6. **Paths via `generic_u8string()`, never `.string()`.** libgit2 wants UTF-8
   with forward slashes on every OS; `path.string()` yields ANSI on Windows and
   corrupts non-ASCII names. Keep paths as `std::filesystem::path` internally and
   convert only at the libgit2 edge. Never build git command strings — we use
   the libgit2 API, so there is no shell quoting.

## Async & threading model

The UI thread never blocks; git work runs off it.

- **`AsyncRepo`** ([`ui/include/gittide/ui/asyncrepo.hpp`](../../../ui/include/gittide/ui/asyncrepo.hpp))
  wraps each blocking `GitRepo` call in `QtConcurrent::run` (Qt's global thread
  pool) and exposes it as a `co_await`-able `QCoro::Task`. A **per-repo mutex**,
  held inside the worker lambda, serializes pool access so two awaited ops never
  touch the same `git_repository` at once — invariant #5, enforced. The repo +
  mutex live behind a `shared_ptr` so in-flight work stays valid even if the
  `AsyncRepo` is destroyed first.
- **Why QtConcurrent + QCoro.** QtConcurrent ships with Qt6 (no new dep); QCoro
  adds `co_await` over `QFuture` via FetchContent. Rejected: `std::execution
  par_unseq` (drags in TBB on libstdc++) and a hand-rolled pool (reinvents
  QtConcurrent).
- **Rendering** of graph/log/diff is lazy and virtualized — only visible rows
  render — so a very large history never stalls the UI.

### Live refresh — watching the working tree and `.git`

GitTide keeps the view current automatically: an external change — your editor,
`git` in a terminal, another tool — appears without any manual refresh. (Earlier,
the UI refreshed only after its *own* actions; see [D35](../../decisions.md).)

- **The active repo is watched live.** `RepoController` owns a `RepoWatcher`
  (`ui/` — a `QFileSystemWatcher` plus a debounce `QTimer`), re-pointed at the
  repository on each `open()`. It watches a flat set of **directories**: every
  non-ignored working-tree directory plus every directory inside the git dir.
  Watching directories (not individual files) is what makes this scale and stay
  portable: it catches files added / removed / renamed in the tree, and git's
  **atomic rewrites** of `index`, `HEAD`, `refs/*`, `packed-refs`, `MERGE_HEAD`,
  and the rebase state dirs — each writes a `*.lock` and renames it into place, so
  the containing watched directory fires. The watch set comes from one core
  primitive, **`GitRepo::watchTargets()`**, which walks the tree under libgit2's
  ignore rules (so `node_modules` / `build` are pruned) and enumerates the git
  dir — keeping libgit2 in `core/` (invariants #1/#2). The controller re-arms the
  set after each batch, so newly created subdirectories start being watched.
  - **Plus the on-screen file.** Directory watches catch listing changes but miss
    an **in-place content edit** of an existing file (Linux inotify dir-watches
    ignore child `IN_MODIFY`), which would leave the open diff stale. So
    `refreshDiff` also calls `RepoWatcher::setActiveFile(absPath)` to add a single
    **per-file** watch for the file currently shown; its change reports the
    worktree scope, refreshing status and re-loading that file's diff. The per-file
    watch survives `watch()` re-arms and is recovered when a rename-on-save editor
    drops the inotify watch; it is reset on `open()`.
- **Events are debounced and classified.** Bursty FS events (a build, a multi-file
  save, a multi-step git op) are coalesced by a short timer (injectable for
  tests). On fire the watcher emits the **scope**: a worktree-only change triggers
  `refreshStatus` (plus the active diff); any change under the git dir triggers the
  **full cascade** (`refreshAll` = status + history + branches + sync) — the same
  cascade a checkout uses. Self-induced churn is suppressed by **muting** the
  watcher around the controller's own work: both its mutations *and* the
  watch-driven refresh handlers are bracketed by a mute (held for one debounce tail
  after they finish). The handlers must mute too because libgit2 "reads" are not
  inert — `status` and ref lookups update on-disk caches under `.git`, which would
  otherwise re-trigger the watcher in a tight loop. Muting closes that loop.
- **Window focus is the safety net.** The one gap in directory-level watching is
  an in-place content edit of an *existing* tracked file (e.g. `echo >> f` in a
  terminal) that changes no directory entry. `Main.qml` re-syncs the active repo
  on window activation (`onActiveChanged`), so anything missed while GitTide was in
  the background is picked up the moment it regains focus.
- **Other repos in the project are polled, not watched.** Watching every repo in a
  large fleet would mean thousands of OS watches. Instead `ProjectController` runs
  a low-frequency `QTimer` — **only while the window is active** — that re-reads
  each non-missing top-level repo's local sync counts (HEAD vs its tracking ref —
  no network) and updates the sidebar rows, reusing the per-row roles the
  [fleet fetch](#fleet-fetch-all) already feeds. This refreshes the *sidebar* view
  of every repo; the **active** repo additionally gets the deep, per-edit live
  refresh of its main view from the `RepoWatcher` above. The poll opens its own
  short-lived `AsyncRepo` per repo (one-owner invariant), so it never shares the
  active repo's handle.

Per-symbol contracts live in the `RepoWatcher` / `watchTargets` Doxygen.

### Network operations & credentials

Fetch, pull, and push run through the same `AsyncRepo` / `QtConcurrent::run`
worker model as all other git ops — they never block the UI thread. Before each
network call, the ViewModel supplies a `Credentials` POD (`sshUseAgent`,
`username`, `password`, and an ordered `sshKeyfiles` vector) containing the auth
material for that session. A pure `credentialAttempts` helper
(`core/src/credentialselect.cpp`, no Qt) inspects the remote URL and the libgit2
`allowed_types` bitmask and returns an **ordered plan** of attempts — for SSH,
the agent first (when enabled) then each keyfile; for HTTPS, one userpass — and
is unit-testable without a live remote. The libgit2 credential callback
(`credentialTrampoline`) walks that plan one attempt per invocation: libgit2's
libssh2 transport re-calls the callback while auth returns `GIT_EAUTH`, so trying
the agent and then successive keyfiles mirrors OpenSSH. It never blocks for a UI
dialog. HTTPS tokens are stored in a session map on the
ViewModel and discarded on quit — secure keychain persistence is deferred (moving
to the OS keychain, with the non-secret metadata in `credentials.json`; see below).

**Identity management.** Who a commit is authored by is resolved from a small
catalogue in `credentials.json` (`core/CredentialsStore`, a versioned atomic-JSON
store mirroring `ProjectStore` — metadata only, never a secret) and **materialized
into git config**, so every `git_signature_default` reader (commit, reword,
pull-rebase, the cherry-pick/merge/rebase engine) and the CLI agree (D49). The
pure resolver `CredentialsStore::resolveIdentity(repoPath, candidateProjectIds)`
(repo override → project default → global; the caller supplies the priority order
so core stays free of `ProjectStore` coupling) drives `GitRepo` primitives:
`setLocalIdentity`/`clearLocalIdentity` (repo-local level, plus a `gittide.identity`
ownership marker), the static `setGlobalIdentity` (writes `~/.gitconfig`, creating
it if absent), and `localIdentity`/`effectiveIdentity` for read-back. In `ui/`,
`CredentialManager` owns the store, references `ProjectStore` to compute the
priority order, and — reacting to the active repo changing — opens a transient
`AsyncRepo` and writes the resolved identity, **never overwriting a local identity
that lacks our marker** (CLI-set config is left as the user set it). Secrets (HTTPS
tokens, SSH passphrases) stay out of `core/` and out of `credentials.json`: they
live in the OS keychain via a `ui/`-side `SecretStore` (QtKeychain;
`InMemorySecretStore` for tests), read into the `Credentials` POD only at call
time. `CredentialManager::credentialsForRemote(url)` assembles that POD — a matched
host account's token for HTTPS; for SSH, the configured keyfiles (+ passphrase,
agent off) when the user registered any, else the ssh-agent **plus the
conventional default identity files** discovered under `~/.ssh` (the pure
`discoverDefaultSshKeyfiles(sshDir)`; home resolution stays in `ui/`). That
default-key fallback is what makes a repo which authenticates from the CLI (a
key on disk, an empty agent) also work in the app. `RepoViewModel` /
`ProjectController` `co_await` the POD before dispatching
fetch/pull/push/clone/fleet-fetch; the auth-dialog fallback persists an entered
token back to the keychain. Core's `Credentials` carries the `sshKeyfiles` list
and `credentialAttempts`/the trampoline emit a `SshKey` attempt per keyfile;
`clone()` takes `Credentials` too. On a machine with no keyring the keychain job errors and GitTide
degrades to the per-session prompt (D50).

**Forge validation.** Adding a host account validates the token against the host
API via `ForgeClient` (`ui/`, `QNetworkAccessManager` + `QJsonDocument` — never
nlohmann, which stays private to `core/`): `GET {apiBase}/user` with a bearer token,
reading the login/name/email to confirm the token and pre-fill an identity
(`CredentialManager::validateAndAddHost`). It awaits the reply with QCoro's signal
support (`qCoro(reply, &QNetworkReply::finished)`), so only `Qt6::Network` is added,
not a QCoro network module. `CredentialManager` owns `IdentityListModel` /
`HostListModel` / `SshKeyListModel`; credential management lives in the **Options**
dialog's **Identity** and **Accounts** tabs (`OptionsIdentityTab.qml` /
`OptionsAccountsTab.qml`, D51) — the earlier standalone `IdentityDialog.qml`
Credentials dialog was removed in Plan 40 and its content folded into those tabs.
On first run, if the identity store is empty, `CredentialManager` seeds one
Global identity from the user's global git config (via the static
`GitRepo::globalIdentity()`, which reads `user.name`/`user.email` without an
open repo) so the Identity tab isn't empty.

**Transfer progress.** Each network call carries a `ProgressCallback`
(`std::function<bool(unsigned received, unsigned total)>`, core-owned, pure
`std`). libgit2 drives it from the worker thread — fetch/pull via
`transfer_progress`, push via `push_transfer_progress` (same received/total
shape). `RepoController::progressSink()` builds a callback that marshals those
counts onto the controller thread (`QMetaObject::invokeMethod`, queued; guarded
by a `QPointer` so a controller that dies mid-transfer drops the call) and emits
`syncProgressChanged(received, total)`. `RepoViewModel` exposes this as
`syncProgress` (fraction, or `-1` when `total` is still 0 ⇒ indeterminate) plus
`syncReceived` / `syncTotal` for the caption, reset at each transfer's start and
end. Fleet fetch-all's callback ignores the counts — it surfaces per-row tree
state, not byte-level progress — but still relays the fleet cancel flag.

**Timeouts & cancellation.** A network op must never hang the UI forever — the
motivating case is fetching an internal remote while off-VPN. Three layers bound
it:

1. **libgit2 server timeouts** (`LibGit2Context` ctor): `GIT_OPT_SET_SERVER_CONNECT_TIMEOUT`
   (10 s) and `GIT_OPT_SET_SERVER_TIMEOUT` (30 s). Process-global, but they cover
   the **HTTP(S) transport only** — libssh2/SSH is unaffected — so they are a
   backstop, not the whole answer.
2. **Cancel flag → `ProgressCallback`.** `RepoController` holds a per-op
   `shared_ptr<atomic<bool>>` (fresh in `beginSync()`, so a still-running worker
   from a prior op keeps its own flag). `progressSink()` captures it and returns
   `false` once set; the existing trampoline turns that into `-1` → the op aborts
   with `GIT_EUSER`. This is the SSH-capable cancel path, but it can only fire
   **after** bytes start flowing — a stalled TCP connect never invokes the
   callback. `ProjectController` has the analogous `m_fleetCancel` for fetch-all
   (`cancelFetchAll()`), and each `fetchOne` relays it.
3. **UI watchdog (guarantees escape).** `beginSync()` arms a single-shot `QTimer`
   (`kSyncTimeout` = 30 s) and bumps a generation counter `m_syncGen`; `fetch`/
   `pull`/`push` capture the generation and, after the network `co_await`, drop
   out silently if `gen != m_syncGen`. On timeout (or an explicit `cancelSync()`)
   the watchdog sets the cancel flag, bumps the generation, and emits
   `syncBusyChanged(false)` — so the **UI regains control immediately** even
   though the worker thread stays blocked (holding the per-repo mutex) until
   libgit2 itself returns. When it finally does, the suspended coroutine resumes,
   sees the stale generation, and unwinds cleanly (the frame stays valid via the
   `QPointer self` guard and the worker's captured `impl` `shared_ptr`). QML
   surfaces this as a **Cancel** button in the sync-progress cluster
   (`RepoViewModel::cancelSync()` → controller). Auto-fetch inherits the watchdog
   for free.

Non-network blocking waits are bounded the same way in spirit: `ForgeClient` and
`AvatarService` set a per-request `setTransferTimeout(30 s)` on their
`QNetworkRequest`s (per-request, so an injected `QNetworkAccessManager` is bounded
too), and `submoduleTree()` caps recursion at `kMaxSubmoduleDepth` (20) so a
cyclic or pathologically deep submodule graph truncates instead of overflowing
the stack. The keychain awaits in `SecretStore` are left unbounded by design (a
QCoro race there would risk destroying a live `QKeychain::Job`; the op is local
and only blocks on an OS unlock prompt).

### Fleet fetch-all

Fetching every repo in a project is orchestrated in `ProjectController` (it owns
the active project and the `RepoListModel`), not in `RepoController` (which holds
the single *active* repo). `fetchAll()` iterates the active project's `RepoRef`s
and, for each non-missing one, opens its **own** fresh `AsyncRepo` and awaits
`fetch()`. Each repo therefore gets a distinct `git_repository` handle — the
one-owner invariant (#5) holds without sharing the active repo's handle, and
because fetch only updates remote-tracking refs it is safe alongside an open repo.
Actual parallelism is bounded by Qt's global thread pool (the same pool every
`AsyncRepo` call dispatches to); all coroutines are launched, the pool throttles.

Each repo's `Expected` is captured independently — one failure never aborts the
fleet. Results drive a transient per-row state on `RepoListModel`
(`Idle → Running → UpToDate | Updated | Failed`, plus a refreshed ahead/behind
from a follow-up `syncStatus()`), surfaced through new roles and incremental
`dataChanged`. `ProjectController` exposes a `fetchingAll` flag and an aggregate
summary string for the project header. Session credentials are shared at the
controller level (not per-`RepoViewModel`) so a single on-demand
`CredentialDialog` serves the whole fleet; the prompt is serialized — the first
auth failure prompts once, caches, and the rest reuse it.

### Branch operations & the refresh cascade

Branch enumeration and mutation (list / create / checkout / delete / rename, plus
detached-commit checkout) are pure git operations → they live on `GitRepo` in
`core/` over the libgit2 `git_branch_*` / `git_checkout_tree` /
`git_repository_set_head[_detached]` / `git_stash_*` APIs, returning `Expected<T>`
like the rest of core. `AsyncRepo` wraps each as a `QCoro::Task`; `RepoController`
exposes them as slots and emits the result.

- **Safe-switch invariant — never clobber uncommitted work.** A checkout that
  would overwrite a dirty working tree must not silently discard it. Both branch
  checkout and detached-commit checkout route through one core helper that, on a
  dirty tree, stashes (`git_stash_save`, including untracked), checks out the
  target, then re-applies (`git_stash_pop`). A pop conflict is the single
  non-clean exit: it stops, **keeps** the stash, and returns a `GitError` —
  `HEAD` has moved but no work is lost. Checkout uses `GIT_CHECKOUT_SAFE`, not
  `FORCE`. (Rationale and rejected alternatives: D21.)
- **Cascade.** A successful switch / checkout / create-with-checkout invalidates
  status + history + branches and triggers the same refresh cascade as "switch
  project," scoped to the one repo. Delete / rename (HEAD unchanged) refreshes the
  branch list only.

### Merge & conflict resolution

Merge of one local branch into the current branch is a pure git operation on
`GitRepo` (libgit2 `git_merge_analysis` → FF / normal / up-to-date, `git_merge`,
the index conflict iterator, and `git_commit` with two parents), wrapped by
`AsyncRepo` and surfaced by `RepoController` like every other op. The product
shape is in [`../product/product.md`](../product/product.md#merge); this is the
engineering contract.

- **Merge-in-progress state is derived from the repository, never from app
  memory.** This is the load-bearing invariant: GitTide must never be unable to
  describe or exit a merge (the failure mode that makes other clients get
  "stuck"). A `MergeState` value — *in-progress* (does `MERGE_HEAD` exist),
  the merged ref name, and the conflicted paths (with the gitlink subset flagged
  as submodule conflicts) — is read fresh from disk on **every** status refresh,
  alongside the normal `FileStatus` list (conflicted entries also carry a new
  `StatusFlag::Conflicted` bit). The UI renders the banner, conflict list, and
  Abort/Commit purely from this value, so a merge begun outside GitTide or
  surviving a restart is shown correctly, and **Abort is reachable whenever
  `MERGE_HEAD` exists**. There is no in-memory "are we merging?" boolean that can
  desync from the repo. Every core merge op returns `Expected<T>`; a failure
  surfaces as `operationFailed`, and the next refresh still reports true state —
  no silent half-state.
- **Inline conflicts need no special core parse.** libgit2 writes the
  `<<<<<<< / ======= / >>>>>>>` markers into the worktree file on conflict, so the
  existing file read already hands the marked content to the UI; `DiffLinesModel`
  groups it into conflict regions for the inline view, and "resolved" is simply
  "no markers remain" — derived, not flagged.
- **Auto-stash orchestration lives in `RepoController`, not core** (mirroring
  `commitSelection`), keeping the core `mergeBranch` primitive clean. On a dirty
  tree it stashes before the merge and records a "stash owed" marker; the pop
  runs after a clean FF/merge, or is **deferred past a conflicted merge until
  `commitMerge` succeeds** (never popped onto conflict markers). A pop conflict
  preserves the stash and reports — the same exit as the safe-switch (D21). The
  "stash owed" marker is *not* a merge-state flag (merge state is still derived
  from disk); worst case a crash leaves a recoverable stash entry, never lost
  work.
- **Submodule conflicts are handled reactively (deinit-and-retry).** When a
  merge's conflicts are gitlinks, `MergeState.conflictedSubmodules` is non-empty
  and the controller offers a retry that: aborts the merge, de-initialises those
  submodules (libgit2 submodule deinit — empties the working dir, keeps the
  gitlink), re-runs `mergeBranch` so the pointers merge as plain blobs, then
  re-initialises and updates them to their pinned commits once the merge concludes
  (commit or abort). De-init/re-init are core `GitRepo` operations; the
  re-init-owed set is tracked on the controller like the stash marker.
- **Cascade.** Starting / committing / aborting / retrying a merge invalidates
  status (+`MergeState`) + diff + history + branches + sync-status — the same
  cascade as a checkout, scoped to the one repo.

### Stash management

The user-facing stash stack is a pure git operation on `GitRepo` over the
libgit2 `git_stash_*` family, returning `Expected<T>` like the rest of core;
`AsyncRepo` wraps each as a `QCoro::Task` and `RepoController` exposes them as
slots. The product shape is in [`../product/product.md`](../product/product.md#stash).
It builds on the existing `stashSave` / `stashPop` / `stashCount` primitives
already used for auto-stash.

- **Core surface.** New `GitRepo` methods: `stashList()` →
  `Expected<std::vector<StashEntry>>` (`git_stash_foreach`; `StashEntry` is a
  plain core struct — `index`, `message`, hex `oid` string — so no libgit2 type
  crosses the public header); and `stashApplyAt(index)` / `stashPopAt(index)` /
  `stashDrop(index)` / `stashClear()` over `git_stash_apply` / `git_stash_pop` /
  `git_stash_drop` (clear drops high→low). The existing parameterless
  `stashPop()` is the `index 0` shortcut.
- **No new diff primitive — a stash *is* a commit.** Preview reuses the existing
  commit-diff path: a stash entry's `oid` is fed to `commitFiles(oid)` +
  `commitDiff(oid, file)`, which diff the stash commit's tree against its first
  parent (the base) — exactly `git stash show`. No `stashDiff` method is added.
- **Conflicts never drop the stash.** libgit2's apply/pop return a merge-conflict
  error and leave the stash on the stack on failure — core returns that `GitError`
  unchanged, the UI reports it via `operationFailed`, and the stack is preserved
  (D44). First cut does not drive into the inline conflict UI.
- **Bridge & model.** A `StashListModel` (`QAbstractListModel`) exposes the
  entries to QML; `RepoViewModel` owns it behind a `stashes` property plus
  invokables (`previewStash` / `applyStash` / `popStashAt` / `dropStash` /
  `clearStashes`). Preview reuses the read-only `commitDiff` `DiffLinesModel`.
- **Cascade.** Every mutating stash op (save / apply / pop / drop / clear) runs on
  the worker repo and then refreshes status + diff + the stash list together, the
  same cascade as discard / checkout. The existing `stashCountChanged` signal
  continues to feed `stashAvailable`.

### Inline selection, commit, and the history diff

There is **no staging area**: the UI owns the commit selection, and `core/` stays
the place that touches the index. This shapes two flows.

- **Working diff is vs `HEAD`.** The editable Changes diff shows *all* of a file's
  working changes against `HEAD` (the index is not the user's model), so `diff()`
  gains a `WorktreeVsHead` target alongside `WorktreeVsIndex` / `IndexVsHead`.
  Because `commitSelection` resets the index to `HEAD` before staging, the
  partial-staging patch (computed from `WorktreeVsIndex` at that moment) lines up
  with the line indices the user picked from the displayed `WorktreeVsHead` diff.
- **Commit from the checked set.** The Changes view holds the checked selection
  (whole files, or specific line indices within a file) as ViewModel state, not
  in the git index. On commit, `RepoController` rebuilds the index to match
  exactly the checked set and then commits it: reset the index to `HEAD`, stage
  each checked whole-file and each checked line-selection (reusing the existing
  file/hunk/line `stage` patch-synthesis, D11), then `commit`. The index is an
  invisible build buffer; the user never manages it. This needs one new core
  primitive — **reset the index to `HEAD`** (`git_reset_default` over all paths /
  unborn-safe) — alongside the existing `stage` / `commit`.
- **History shares the diff view.** Inspecting a commit reuses the same diff
  panel as working changes, so `core/` gains read-only commit-diff endpoints:
  **list a commit's changed files** and **diff one file in a commit** (its tree
  vs its first parent, with the root commit handled via an empty parent tree).
  These mirror the working `status` / `diff` shapes (`FileStatus` / `DiffResult`)
  so the UI renders both through one widget. Per-symbol contracts live in the
  `gitrepo.hpp` Doxygen.

### Author avatars

Resolving a commit author's email to an image is a **`ui/` concern, never `core/`**
(no Qt in core; core stays offline/deterministic). Core owes only the author
**email**, now carried on `CommitNode.email` (populated in `nodeFromCommit`) and
surfaced as the `authorEmail` model role.

- **`AvatarService`** (`ui/src/avatarservice.cpp`) resolves an email to a `QImage`
  through an ordered chain, first hit wins: in-memory cache → on-disk cache under
  the app cache dir keyed by `md5(email)` (with a TTL and a `.miss` negative-cache
  marker so a missing avatar is not re-probed on every scroll) → the network. v1's
  network step is **Gravatar only**: a `d=404` probe, then `d=identicon` on 404
  (which always renders). It uses the same async-HTTP shape as `ForgeClient`
  (`QNetworkAccessManager` + `co_await qCoro(reply, &QNetworkReply::finished)`), and
  the manager, cache dir, and Gravatar base are injectable so offscreen tests never
  touch the real network. Forge (GitHub/GitLab) sources are a later increment that
  prepends a step to the chain (D52).
- **`AvatarImageProvider`** (a `QQuickAsyncImageProvider`) bridges the service to
  `Image { source: "image://avatar/<md5-hash>" }`, so the engine's per-source pixmap
  cache dedups identical authors across the virtualized rows for free. Because
  `requestImageResponse` runs on the QML pixmap-reader thread while the service and
  its `QNetworkAccessManager` live on the main thread, the provider **hops the fetch
  onto the service's thread** (`QMetaObject::invokeMethod`) and marshals the finished
  image back to the response — touching the `QNAM` off-thread would crash. The
  service is exposed as the `avatarService` context property and its provider
  registered on the engine in `installQmlContext` (a default instance is installed
  when none is passed, so `Avatar.qml`'s bindings resolve in every shell test).
  Network loading is a toggle (`AvatarService::networkEnabled`), **default on**,
  session-only for now like the theme mode.

### Local-only vs pushed commits

A History commit is **local-only** when it is reachable from HEAD but from no
remote-tracking ref. The set is computed in `core/` by
**`GitRepo::localOnlyOids()`** — a revwalk that pushes HEAD and hides
`refs/remotes/*`, yielding exactly the unpushed OIDs (empty when HEAD is fully
pushed or unborn; every HEAD commit when there is no remote). This keeps
`log`/`logAllRefs`/`GraphBuilder` pure and uncoupled from remote state, and costs
only O(ahead); it is preferred over a `bool pushed` field on `CommitNode` (D53).

`AsyncRepo` wraps it as a `QCoro::Task`; `RepoController::refreshHistory` emits
`localOnlyOidsReady(QSet<QString>)` right after `historyReady` (via the shared
`refreshLocalOnly` helper), and — because fetch/pull/push change what is pushed
without touching HEAD — the fetch and push handlers also call `refreshLocalOnly`
(pull already re-runs `refreshHistory`). `RepoViewModel::onLocalOnly` feeds the set
to `HistoryListModel::setLocalOnlyOids`, backing the `isLocalOnly` role (mirroring
the `setRefTips`/`setLocalBranchTips` oid-map pattern). The cue is rendered per
[design](../design/design.md#qml-history-view): a row badge + dim in History, a
hollow `GraphColumn` dot in the Graph tab.

## Logging & diagnostics

GitTide is observable across every layer through one categorised, level-controlled
logging facility. It is the *diagnostic* channel that runs alongside the
error-as-value channel (invariant #4): an `Error`-level log usually accompanies a
returned `GitError`, it does not replace it.

- **The core/no-Qt boundary is a tiny Qt-free facade.** `core/` logs through
  `gittide::logf(level, category, fmt, …)` (in
  [`core/include/gittide/log.hpp`](../../../core/include/gittide/log.hpp)) — a
  `std::format`-based call that depends on nothing but the standard library, so
  invariant #1 holds. It routes through a process-wide `LogBackend` (two
  `std::function`s in `std` types — a `write` sink and a cheap `enabled` gate)
  installed once at startup. With no backend, logging is a silent no-op. (D26.)
- **The app bridges that facade onto Qt.** At composition time `app` calls
  `gittide::ui::installLogging()`
  ([`ui/src/logging.cpp`](../../../ui/src/logging.cpp)), which wires core's
  `LogBackend` to Qt's `QLoggingCategory` machinery: core records route to the
  matching category and the `enabled` gate consults it, so one set of Qt rules
  governs **all** layers. `ui`/`app` C++ logs directly with `qCDebug`/`qCWarning`
  on those categories; QML logs through the `log` context property (a `QmlLog`)
  so GUI diagnostics use the same categories + levels, never a stray
  `console.log`.
- **Categories** are one coherent taxonomy, defined once as `gittide::logcat`
  constants and namespaced `gittide.*`: `git` (libgit2 ops), `repo` (repo/project
  persistence), `async` (worker/refresh cascade), `auth` (credentials), `ui`
  (view-models/QML), `app` (startup/composition).
- **Levels** are `Trace`/`Debug`/`Info`/`Warning`/`Error`, mapped onto Qt's
  `QtMsgType` at the bridge (Trace+Debug → Debug, Error → Critical).
- **Control is global + per-category, via Qt's rules** —
  `QT_LOGGING_RULES="gittide.git.debug=true"` (or a `qtlogging.ini`) raises one
  area while the rest stay quiet. A persisted setting + an in-app toggle is a
  later wish; this cut is the env-var path. (D27.)
- **Sinks: console + a rotating file.** A `qInstallMessageHandler` writes every
  record to stderr and to `gittide.log` (rolled to `gittide.log.1` past a size
  cap) under the app data dir, so a user can attach it to a bug report. (D27.)
- **Paths in logs** honour the path rule — log the `toGitPath()` UTF-8 form, never
  `std::u8string`/`.string()`.

## Code style

Naming, formatting, and the mandatory C++/Qt rules live in
[`code-style.md`](code-style.md) — the standard, enforced by
[`.clang-format`](../../../.clang-format). The codebase conforms to it; keep new
code conformant (see the [Conformance](code-style.md#conformance) note for the
test-layer exception). Principles first: KISS, DRY, SOLID, YAGNI.

## Build & test

- **Toolchain.** C++23, CMake ≥ 3.28. The `core/` and `ui/` layers currently use
  classic `#include` headers (Qt's `moc` does not cooperate with C++ modules).
- **Dependencies.** Qt 6 comes from the system or `aqtinstall` via
  `find_package` — **never** FetchContent (building Qt from source is
  impractical). libgit2, QCoro, and Catch2 are fetched via **FetchContent**
  (pinned tags under [`cmake/dependencies/`](../../../cmake/dependencies), one
  file per dependency, aggregated by [`cmake/dependencies.cmake`](../../../cmake/dependencies.cmake)).
  vcpkg is deliberately avoided. Network transports (`USE_SSH`/`USE_HTTPS`) are
  **on** (D28): HTTPS uses the platform TLS backend (OpenSSL on Linux →
  `libssl-dev`, SChannel on Windows, SecureTransport on macOS); SSH links libssh2
  on Linux (`libssh2-1-dev`) and macOS (`brew install libssh2`) so the credential
  callback's ssh-agent / key auth works. **Windows SSH is off for now** (no system
  libssh2; vcpkg-vs-`exec` deferred). clone/fetch/push speak `https://`, `ssh://`,
  scp-like `user@host:path`, and local/`file://` paths.
- **Targets.** `gittide_core` (static lib), `gittide_ui` (static lib, AUTOMOC),
  `gittide_app` (executable), plus test targets below.
- **Packaging.** On macOS `gittide_app` is a `MACOSX_BUNDLE` (`GitTide.app`,
  icon + `Info.plist` from [`packaging/macos/`](../../../packaging/macos/)). The
  `deploy_macos` target makes it self-contained and launchable via
  [`packaging/macos/macdeploy.py`](../../../packaging/macos/macdeploy.py):
  `macdeployqt` bundles Qt, then the script rewrites the absolute Homebrew-Qt
  references macdeployqt leaves behind to `@rpath` and **ad-hoc codesigns** the
  bundle (Apple Silicon SIGKILLs a process whose signature does not match the
  pages it maps — D54). `deploy_macos` is part of `ALL` and **mandatory for the
  app to launch** — a bare build links the executable against Homebrew Qt, which
  clashes with the bundled frameworks and fails to load the `cocoa` plugin. To
  keep this off the incremental-build hot path, the deploy is gated behind a
  stamp file that depends on the executable, so it re-runs only after a relink
  (no-op/test builds skip the ~90 MB Qt copy). `cmake --install --component gittide` copies the finished
  bundle verbatim (`install(DIRECTORY)`, not `install(TARGETS)`, so CMake does no
  install-time RPATH/signature surgery). On Linux the same component installs a
  `.desktop` entry + icon. Full native installers / notarization remain a wish
  ([deployment-packaging](../../wishlist/deployment-packaging.md)).
- **Tests.** Catch2 for `core/` (`gittide_core_tests`, one ctest entry per case
  via `catch_discover_tests`). Qt Test for `ui/` (`gittide_ui_tests`, a single
  aggregated binary run headless with `QT_QPA_PLATFORM=offscreen`). New UI
  sources go in `ui/CMakeLists.txt`'s `gittide_ui` list; new UI tests go in the
  `gittide_ui_tests` source list in `tests/CMakeLists.txt`. How tests are
  structured (the `TempRepo` helper, the `#include` UI runner) and how to add one:
  [`testing.md`](testing.md).
- **CI.** GitHub Actions matrix: Linux / macOS / Windows
  ([`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml)).
- **Development is test-first (TDD):** write the failing test, then the code.
  See the plans in [`../../plans/`](../../plans/index.md) for the task-by-task
  cadence.

Command reference (configure / build / test / single test) lives in the
repository [`CLAUDE.md`](../../../CLAUDE.md).
