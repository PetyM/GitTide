# Decisions

The significant, durable choices behind GitTide and the alternatives they
rejected — the *why* that the spec sections reference but don't belabour. Append a
new entry when a choice is hard to reverse, surprising, or rejects a real
alternative; link to it from the relevant spec. Don't rewrite history — supersede
an entry with a newer one if it changes.

## Product

- **D1 — Project-first model.** A Project groups multiple repositories; this is the
  product's differentiator over one-repo-at-a-time clients. *Why:* fast multi-repo
  context switching. → [`product`](spec/product/product.md)
- **D2 — MVP excludes network ops beyond clone.** No push/pull/fetch, no branch
  management in the first cut. *Why:* focus the MVP; those land post-MVP with the
  network feature set. → [`product`](spec/product/product.md)
- **D3 — Multi-window, hybrid.** Windows are views over shared services; "active
  project" is **per-window** state, not a global. *Why:* open several projects at
  once without a global mode. → [`product`](spec/product/product.md)
- **D4 — Submodules discovered live, never persisted.** Git state is the single
  source of truth; opening a submodule reuses the normal repo machinery. *Why:* no
  stale list when `.gitmodules` changes. → [`product`](spec/product/product.md)
- **D21 — Branch checkout is stash-and-switch (safe by default).** On a dirty
  working tree, checkout stashes, switches, then re-applies the stash onto the
  target so the work follows the user; a re-apply conflict stops and keeps the
  stash. *Rejected:* clean-tree-only (blocks the most common case), block-with-
  message (same), and stash-then-leave-it-parked (no stash-list UI yet → work gets
  forgotten). Uses git stash **internally** only — not the user-facing stash wish.
  → [`product`](spec/product/product.md), [`engineering`](spec/engineering/engineering.md)
- **D5 — Two persistence files.** `projects.json` (registry) is separate from
  `session.json` (window state). *Why:* opening/moving windows never rewrites the
  project registry. → [`product`](spec/product/product.md)
- **D22 — GitHub-Desktop UI model: no staging area, no Dashboard, unified diff.**
  The Changes surface drops the staged/unstaged lists for one list of changed
  files with **default-checked** checkboxes (and per-line checkboxes in the diff);
  History stops being a standalone graph tab and shares one **diff panel** with
  working changes (commit → its files → read-only diff); the **Dashboard** is
  removed. *Rejected:* keeping the explicit staging area (more ceremony than a
  focused client needs) and a separate History tab (duplicate diff surface).
  *Why:* fewer surfaces, an inline tick-what-to-commit model that's faster and
  more legible. The multi-repo **Project** sidebar (now collapsible) stays — it's
  the differentiator. → [`product`](spec/product/product.md)
- **D29 — Conflict resolution is inline (VS Code style), not a 3-pane merge
  editor.** A conflicted file opens in the existing shared diff panel with its
  marker regions shown inline — *Current (ours)* / *Incoming (theirs)* tinted and
  labelled, per-region Accept Current / Incoming / Both, plus free edit — and a
  file is "resolved" when no markers remain (derived, no manual toggle). Merge is
  started from both the branch dropdown and the History context menu. *Rejected:*
  a coarse per-file ours/theirs-only picker (too blunt for real conflicts) and a
  full 3-way Incoming|Result|Current editor (a large new multi-pane view for the
  first cut — YAGNI; a later iteration). *Why:* reuses the diff machinery GitTide
  already has, matches a UI users know, and keeps the first cut scoped. →
  [`product`](spec/product/product.md), [`design`](spec/design/design.md)

- **D32 — History editing ships its rebase-free slices first; combined diff is
  contiguous-only.** The history-editing/rebase wishes graduate in two cuts. Round
  one ships only the parts needing **no** interactive-rebase engine: a **combined
  range diff** and **reword of the tip** (`git_commit_amend`, tree unchanged).
  Reword-of-older, squash, and reorder are deferred — each rewrites descendants
  and is interactive rebase under the hood (driver + todo-list editor + per-step
  conflict UI). Combined diff is offered **only for a contiguous selection**
  (`tree(parent(oldest))` vs `tree(newest)`); a non-contiguous (Ctrl-click)
  selection has no single tree-vs-tree diff that represents only the chosen
  commits, so the pane prompts for a contiguous range instead of showing a
  misleading span. *Rejected:* building the interactive-rebase engine now (the
  "big, separate iteration" both wishes warn against — YAGNI); span-the-bounds or
  sum-per-commit diffs for holey selections (semantically loose / unreadable).
  *Why:* fast, safe wins that stand alone, leaving the multi-select model and
  commit menu as the home for the deferred verbs. →
  [`product`](spec/product/history-editing.md)

## Engineering

- **D6 — C++23 + Qt 6 Widgets (not QML).** Native desktop; the graph uses
  `QGraphicsView`. → [`engineering`](spec/engineering/engineering.md)
- **D23 — Stage-on-commit; the index is an invisible build buffer.** With the
  staging area gone, the checked selection lives in ViewModel state; on commit the
  index is reset to `HEAD`, the checked whole-files/lines are staged (reusing the
  D11 patch synthesis), then committed. *Rejected:* mirroring the index live
  (checkbox = `stage`/`unstage`), which keeps "staging" in the user's mental model
  and surfaces pre-existing CLI-staged state. *Why:* makes staging truly invisible
  while keeping all index mutation in `core/`. Adds one core primitive (reset
  index to `HEAD`). → [`engineering`](spec/engineering/engineering.md)
- **D24 — UI-refactor reaffirms QWidgets over QML; Fusion base style.** The
  GitHub-Desktop refactor reopened the QML question (D6) and again chose
  QWidgets: QML would mean *more* design work for a native feel, immature native
  controls, and rewiring the `AsyncRepo`/controller boundary and test harness for
  near-zero benefit given a native-modern-minimal goal. The modern look comes from
  the Qt **Fusion** style instead of a hand-rolled QSS skin. *Rejected:* QML
  migration (large blast radius), a third-party QSS theme (dependency, less
  native), pure platform-native (inconsistent across OSes). →
  [`engineering`](spec/engineering/engineering.md), [`design`](spec/design/design.md)
- **D7 — Strict layering; `core/` is pure C++, no Qt.** *Why:* the git engine stays
  unit-testable and the boundaries stay honest. → [`engineering`](spec/engineering/engineering.md)
- **D8 — libgit2 & nlohmann/json are PRIVATE to `core/`.** *Why:* no dependency
  leaks onto downstream consumers. → [`engineering`](spec/engineering/engineering.md)
- **D9 — Errors are values (`std::expected`), no exceptions across layers** — and
  none thrown from Qt slots. *Why:* predictable control flow; throwing from a slot
  is undefined behaviour. → [`engineering`](spec/engineering/engineering.md)
- **D10 — Concurrency = QtConcurrent + QCoro.** *Rejected:* `std::execution
  par_unseq` (drags in TBB on libstdc++) and a hand-rolled thread pool (reinvents
  QtConcurrent). *Why:* QtConcurrent ships with Qt; QCoro adds `co_await` with a
  small dep. → [`engineering`](spec/engineering/engineering.md)
- **D11 — Partial-staging patch synthesis lives in Core, not UI.** *Why:* keeps it
  Qt-free and Catch2-testable. → [`engineering`](spec/engineering/engineering.md)
- **D12 — Qt via `find_package` (system/aqtinstall), never FetchContent;
  libgit2/QCoro/Catch2 via FetchContent; vcpkg avoided.** *Why:* building Qt from
  source is impractical; pinned FetchContent tags are reproducible. →
  [`engineering`](spec/engineering/engineering.md)
- **D13 — Network transports off this milestone (`USE_SSH`/`USE_HTTPS` OFF).**
  *Superseded by D28.* *Why (then):* avoids the OpenSSL/mbedTLS dependency; local
  and `file://` paths sufficed. → [`engineering`](spec/engineering/engineering.md)
- **D28 — Network transports on; HTTPS everywhere, SSH off on Windows for now.**
  `USE_HTTPS=ON` uses the platform TLS backend (OpenSSL on Linux → `libssl-dev`,
  SChannel on Windows, SecureTransport on macOS — only Linux needs a dev package).
  `USE_SSH=ON` links libssh2 on Linux/macOS so the credential callback's
  ssh-agent/key auth works; Windows has no system libssh2 and is left OFF.
  *Deferred:* Windows SSH route (vcpkg libssh2 vs `USE_SSH=exec`). *Why:* real
  remotes (https/ssh) are needed now; the userpass + ssh-agent credential paths
  were already wired. → [`engineering`](spec/engineering/engineering.md)
- **D30 — Merge-in-progress state is derived from the repository, never held in
  app memory.** `MergeState` (in-progress, merged ref, conflicted paths +
  submodule subset) is re-read from disk (`MERGE_HEAD` + the index conflict
  iterator) on every status refresh; the UI renders banner/conflicts/Abort purely
  from it. *Rejected:* tracking "are we merging?" as a ViewModel boolean (the
  approach that leaves clients like GitHub Desktop stuck in a merge they can't
  describe or exit when app memory and the repo disagree). *Why:* disk is the one
  source of truth, so a merge from the CLI or surviving a restart shows correctly
  and **Abort is always reachable** — the no-limbo guarantee. →
  [`engineering`](spec/engineering/engineering.md)
- **D31 — Merge auto-stash + reactive submodule deinit live in the controller,
  not core.** A dirty tree is auto-stashed before merge and popped after a clean
  result (deferred past a conflicted merge until `commitMerge`; pop-conflict keeps
  the stash, per D21). Submodule (gitlink) conflicts are handled **reactively**:
  try a plain merge first, and only when pointers actually conflict offer
  *deinit-and-retry* (abort → deinit the conflicted submodules → re-merge →
  re-init+update to pinned commits). *Rejected:* core doing its own stashing
  (mixes orchestration into the merge primitive); an upfront "always deinit
  submodules before merge" toggle (penalises the common no-submodule-conflict case
  and adds persisted config — the user preferred reactive). *Why:* keeps the core
  `mergeBranch` primitive clean and avoids messy nested-repo conflicts only when
  they'd actually occur. → [`engineering`](spec/engineering/engineering.md)
- **D14 — Paths via `generic_u8string()`, never `.string()`.** *Why:* `.string()`
  yields ANSI on Windows and corrupts non-ASCII names. → [`engineering`](spec/engineering/engineering.md)
- **D15 — Classic `#include` headers, not C++ modules.** *Why:* Qt's `moc` does not
  cooperate with module units. (The original design intended modules in Core; the
  reality is headers.) → [`engineering`](spec/engineering/engineering.md)
- **D16 — Coding standard = Workswell, adopted verbatim** (`m_` members, lowercase
  file names, Allman braces). Existing code is conformed **when touched**, not in a
  repo-wide reformat. *Why:* a shared standard, with opportunistic migration to
  avoid big-bang churn. → [`code-style`](spec/engineering/code-style.md)

- **D26 — Logging is a hand-rolled Qt-free core facade bridged onto Qt categories.**
  `core/` logs through a tiny `gittide::logf` facade over a `LogBackend` (two
  `std::function`s, `std` types only); `app` installs a backend that routes onto
  Qt's `QLoggingCategory` so one taxonomy + rule set spans `core`/`ui`/`app`, and
  QML logs through the same path via a `log` context property. *Rejected:* a
  third-party logger (spdlog/fmt — a new dependency for what a ~40-line facade +
  Qt's existing category system already cover); Qt logging directly in `core`
  (breaks invariant #1, no Qt in core); a core-only logger that `ui`/`app` ignore
  (two control surfaces, the GUI bypasses category/level rules). *Note:* the level
  enum is `LogLevel::Error` (PascalCase, matching the codebase's `enum class`
  convention) rather than `ERROR`, which also dodges the `<windows.h>` `ERROR`
  macro; and the `LogBackend` emit member is named `write`, not `emit`, because Qt
  defines `emit` as a macro. → [`engineering`](spec/engineering/engineering.md)
- **D27 — First-cut logging control is env-var only; sinks are console + a rotating
  file.** Verbosity is set through Qt's `QT_LOGGING_RULES` / `qtlogging.ini`
  (global and per-category); records go to stderr and to a size-rotated
  `gittide.log` under the app data dir. *Rejected (deferred, not refused):* a
  persisted setting + in-app verbosity toggle (product-facing, but more surface
  than the first cut needs) and structured/JSON logging, log shipping, and a log
  viewer UI (later wishes). *Why:* the cheapest path that makes the app observable
  and bug reports attachable. → [`engineering`](spec/engineering/engineering.md)
- **D33 — Rebase and merge are mutually exclusive; rebase auto-stash follows D31;
  interactive todo-editor is deferred (YAGNI).** Every rebase verb guards on
  `git_repository_state() != GIT_REPOSITORY_STATE_NONE` and refuses to start a
  rebase while a merge (or another rebase) is already in progress; the merge path
  applies the same guard. At most one in-progress-operation banner is visible at a
  time. The rebase **auto-stash** follows the D31 pattern: the controller calls
  `stashSave` before starting a rebase, records the handle in `m_pendingStashPop`
  (shared with merge — safe, because only one operation runs at a time), pops on
  clean `finishRebase` / `abortRebase` / start-error, and leaves the stash pending
  while the rebase is paused on a conflict. The first rebase cut ships **plain
  rebase only** (init/next/commit/finish/abort, continue/skip/abort); the
  interactive todo-list editor (squash / reorder / drop / fixup / reword-older) is
  deliberately deferred — it is the next, independent iteration that builds on this
  driver. *Rejected:* implementing the interactive editor now (large new UI surface,
  complicates the first-cut scope — the plain driver already reuses the existing
  conflict UI and merge machinery). *Why:* fast, safe wins that stand alone, per the
  YAGNI rule stated in D32. → [`engineering`](spec/engineering/engineering.md),
  [`product`](spec/product/rebase.md)
- **D34 — Interactive rebase is a manual cherry-pick engine over a GitTide-private
  todo, with a mid-rebase message pause.** libgit2's `git_rebase_init` only
  generates a `PICK`-only operation list in original order and exposes no API to
  inject a reordered / squashed / dropped / reworded todo, so the interactive
  engine is built by hand: detach HEAD at the base, `git_cherrypick` each kept
  commit on the detached HEAD (the branch ref is moved only at finish, making abort
  trivial), `git_commit_amend` for squash/fixup, skip for drop. State lives in
  `<gitdir>/gittide-rebase/` (todo + `done` cursor + `applied` marker + orig-head +
  branch), so `RebaseState` stays disk-truth (D30) and a paused rebase survives a
  restart. Reword/squash pause mid-rebase for a message (git-CLI style) rather than
  collecting messages up-front. The Tier 1 `continueRebase`/`skipRebase`/`abortRebase`
  verbs dispatch to whichever engine is live (libgit2 plain vs. our dir). *Rejected:*
  re-`init`'ing libgit2 per step (no todo API); shelling out to `git rebase -i`
  (violates the no-git-command-strings invariant, loses structured conflict state);
  up-front message collection (the user chose git-faithful mid-rebase pausing). →
  [`engineering`](spec/engineering/engineering.md), [`product`](spec/product/rebase-interactive.md)

- **D35 — Auto-refresh by watching working-tree directories + the git dir, not
  per-file and not by polling the active repo.** The active repo is kept current
  with a `QFileSystemWatcher` over every non-ignored working-tree directory plus
  the git dir, debounced and classified into a status-only vs. full-cascade
  refresh; other repos in the project are refreshed by a low-frequency, window-
  active-gated poll. *Why:* per-file watching does not scale (OS watch-descriptor
  limits) and constantly polling the active repo both lags and wastes work.
  Directory-level watching catches git's atomic `*.lock`→rename rewrites
  (`index`/`HEAD`/`refs`/`MERGE_HEAD`/rebase dirs) and tree add/remove/rename;
  the residual gap — an in-place edit of an existing file that touches no
  directory entry — is closed by a window-focus re-sync. The watch set is computed
  by a core `watchTargets()` so libgit2's ignore rules stay in `core/` (invariant
  #1/#2). Refreshes are read-only, so the watcher has no feedback loop; self-
  induced events are muted around the controller's own mutations. *Rejected:*
  watching every file (unscalable, hits inotify/FSEvents limits); hand-rolling a
  recursive FSEvents/inotify watcher (re-implements Qt, non-portable); polling the
  active repo on a timer (lag + churn). →
  [`engineering`](spec/engineering/engineering.md)

- **D36 — History reorder has two gestures over one engine; direct-in-history drag
  is gated to the linear single-parent run from HEAD and confirmed.** Reordering is
  expressed entirely through the D34 interactive engine (a reorder is a plan of all
  `pick`s in a new order on a fixed base). Two front-ends feed it: drag-the-grip in
  the todo editor (alongside the kept ↑/↓ buttons, which stay for keyboard reach),
  and drag a commit row directly in the history view. The direct gesture is fenced
  to the **reorderable run** — the contiguous span of single-parent (non-merge)
  commits from HEAD down — because a merge (≥2 parents) or the root (0 parents)
  cannot be replayed by a plain cherry-pick; only those rows are draggable. Because
  a drag silently rewrites history, the direct gesture routes through an explicit
  confirmation, and the abortable rebase banner remains the escape hatch. The
  already-pushed-commit warning stays **deferred** to network-sync (consistent with
  the existing interactive-rebase deferral). *Rejected:* a bespoke reorder engine
  (the interactive engine already expresses it); free drag across merges (the
  cherry-pick reorder can't honour a merge's two parents); reordering with no
  confirmation (too easy to rewrite history from a stray drag). →
  [`product`](spec/product/rebase-interactive.md), [`product`](spec/product/history-editing.md)

- **D37 — Undo last commit is a soft reset to the first parent (keep changes
  staged), a core verb guarded by mutual exclusion.** "Undo last commit" runs
  `git reset --soft HEAD~1`: the branch moves to HEAD's first parent and the index
  and working tree are left intact, so the undone commit's changes stay staged
  ready to re-commit. It errors on an unborn branch, a detached HEAD, a root commit
  (no parent), or while a merge/rebase is in progress (D33). It is offered on the
  HEAD commit's context menu and in the app menu (disabled mid-merge/-rebase).
  *Rejected:* mixed/hard reset as the default (soft is the safe, common "oops"
  that loses nothing — hard is destructive); a generic reflog-based multi-step
  undo (out of scope; this is the focused last-commit case). →
  [`product`](spec/product/history-editing.md)

- **D38 — Whole-row long-press drag + three-band drop zone disambiguates reorder
  from squash in the history view.** History drag was grip-only (a 16 px `⠿` target
  easy to miss) and squash had no direct-manipulation path. The whole commit row in
  the reorderable run is now a drag source armed by a 250 ms press-and-hold (a quick
  click still selects — no accidental reorder). A three-band drop zone on the target
  row disambiguates: top/bottom thirds reorder (insert above/below, existing
  confirmation), the middle third squashes the dragged commit into the target via
  `squashCommitInto` → the combined-message `RewordDialog`. *Hold-to-drag over
  grip-only:* the grip was undiscoverable; whole-row hold is the platform-standard
  reorder gesture and keeps click-select intact — the grip stays as a hint.
  *Drop-zone thirds over a modifier key:* Shift/Ctrl-to-squash is invisible; live
  band indicators (insertion line vs. squash fill + "◆ squash" badge) show the
  outcome before release (shape-differentiates, not colour-only — D19).
  *Drag-squash opens the combined-message editor (no fixup via drag):* the
  `RewordDialog` pause mirrors menu-driven squash; message-discarding fixup stays in
  the todo editor; direction is fixed — the dragged commit folds into the target,
  which keeps its slot. Hardening: `repoviewmodel.cpp` now calls
  `updateReorderableRun()` after clearing `m_lastLayout` in both `open()` and
  `close()`, preventing a stale `reorderableRunLength` from indexing an empty rows
  vector (SIGSEGV). No core change: the D34 engine already squashes and pauses for
  the message. Extends D36. →
  [`product`](spec/product/rebase-interactive.md)

- **D40 — Message-pause auto-surfaces the editor; drag feedback is a floating chip,
  not a moved row.** A `RebasePause::Message` step (squash / reword) already has
  the combined message prefilled in `rebaseMessagePrefill`; requiring the user to
  also click Continue before the editor opens is pure friction. `RepoViewModel` now
  detects the rising edge into each message step and emits `rebaseMessagePauseEntered()`
  so `WorkingPane` opens the dialog immediately; the banner's Continue is the fallback
  if the dialog is dismissed. For drag feedback, the `DragHandler` keeps `target:
  null` (the row must not follow the cursor — the target-row indicators show the
  outcome); instead a separate pane-level `dragChip` Item follows `dropLogic.dragPos`,
  showing the dragged commit and a **"◆ Squash"** / **"Move"** label that makes the
  drop intent legible before release without moving any list row. *Rejected:*
  collecting messages before the rebase starts (loses the mid-rebase git-faithful
  model and requires a new UI gate); moving the source row with the cursor (fights
  the three-band drop zone and confuses where the commit currently sits). →
  [`product`](spec/product/rebase-interactive.md)

- **D39 — The branch graph moved to its own all-refs Graph tab; the History drag
  bug was a `MouseArea` grab-steal, fixed with `TapHandler`.** The in-history
  graph column only walked HEAD (`git_revwalk_push_head`), yielding a near-linear
  strip that earned its column width poorly. Moving it to a dedicated Graph tab
  (fed by `logAllRefs` — all `refs/heads/*`, `refs/remotes/*`, `refs/tags/*`)
  gives a real multi-branch graph with branch/tag chips and is cheaper to keep
  off-screen when unused. The History drag regression (Plan 23 named `TapHandler`
  in its tech stack but the implementation used `MouseArea`) was that a
  `MouseArea` takes an *exclusive* grab on press, so the sibling `DragHandler`
  never won the grab and the row could never be dragged. Replacing it with two
  `TapHandler`s (left-click select, right-click menu) lets the `DragHandler` win
  the grab after the 250 ms hold, with no conflicts. *Rejected:* keeping the graph
  column and patching `log` to push more refs (would widen an already-cramped
  column and still needed the `MouseArea` fix). → [`product`](spec/product/product.md),
  [`history-editing`](spec/product/history-editing.md)

- **D47 — Graph tab drops its inline commit-detail panel; double-click hands off
  to History instead.** The Graph tab's split (graph list + `CommitDetail`) left
  the graph itself cramped at a fixed 460px. Since `commitDiff`/`selectedCommit`
  are already global state on `RepoViewModel` shared by both panes'
  `CommitDetail`, a double-click only needs to select the row and switch
  `WorkingPane`'s tab — no oid lookup or re-selection needed, and it works even
  when the commit isn't in History's own HEAD-only list (that pane just shows no
  highlighted row). *Rejected:* keeping a collapsible/toggleable detail panel
  (not requested, adds state for no real benefit). →
  [`product`](spec/product/product.md#graph-tab)

- **D44 — On a stash apply/pop conflict, report and preserve the stash; do not
  drive into the inline conflict UI (first cut).** The user-facing stash stack
  (list / apply / pop / drop / clear / preview) exposes git's native stack in a
  collapsible panel in the Changes tab; selecting an entry previews its diff in
  the shared right-hand diff panel. When `git_stash_apply`/`pop` would land on
  conflicts against the current tree, the op stops, surfaces via `operationFailed`,
  and leaves the stash on the stack (libgit2 does not drop on failure) — never
  silently losing parked work. *Why:* highest value per line and zero coupling to
  the merge-conflict flow; the minimum the wish demands. *Rejected:* routing the
  conflict into the existing inline merge-conflict resolution UI (richer, but
  couples stash to merge state and is materially more work — deferred as the
  upgrade path); a message prompt + keep-index/untracked toggles on save (kept the
  existing one-click save, YAGNI). The preview reuses the read-only `commitDiff`
  model. → [`product`](spec/product/product.md#stash),
  [`engineering`](spec/engineering/engineering.md)

- **D48 — The frameless custom title bar is Windows/Linux-only; macOS uses native
  chrome + a native system menu bar.** The unified frameless bar (D-era app-menu
  work) broke on macOS: `Qt.FramelessWindowHint` disables native fullscreen and
  the menu lived in-window instead of the system menu bar. macOS therefore keeps
  native decorations (`flags: Qt.Window`) and gets a `Qt.labs.platform`
  `NativeMenuBar.qml` in the system menu bar, both selected by `window.isMac`.
  *Why:* respect the platform — native fullscreen and a top-of-screen menu are
  what Mac users expect — with no C++/Objective-C. *Rejected:* keeping the unified
  look via Qt 6.9 `ExpandedClientAreaHint` overlaying real traffic lights (more
  work; the user preferred a standard native bar); native `NSWindow` code
  (violates the no-native-code stance). The custom `TitleBar` stays instantiated
  but `visible: false` on macOS so shared signal wiring and tests still resolve.
  → [`app-menu §8`](spec/product/app-menu.md#8-macos-native-chrome--system-menu-bar),
  [Plan 35](plans/2026-07-06-plan35-macos-native-chrome.md)

- **D49 — Managed git identity is materialized into git config with an ownership
  marker, not injected at commit time.** GitTide resolves an effective identity
  (repo override → project default → global) and *writes* it: the global identity
  into `~/.gitconfig` (global level), a per-repo/per-project effective identity into
  the repo's local `.git/config`. Every write also sets a `gittide.identity = <id>`
  marker; GitTide only ever overwrites or clears local `user.name`/`user.email`
  that are absent or carry that marker — a local identity set by the user/CLI (no
  marker) is left untouched and shown as "manually configured". *Why:* `user.*` is
  read by `git_signature_default` at ~7 sites (commit, reword, pull-rebase, and the
  cherry-pick/merge/rebase engine); materializing to config makes all of them Just
  Work with zero threading, keeps the CLI consistent, and reuses the proven
  `setPullStrategy` config-write path. *Rejected:* overriding author/committer at
  commit time (would have to plumb an identity through every signature site,
  including a rebase loop with no UI path, and would silently diverge from what the
  CLI shows). *Storage:* the identity catalogue and the global/project/repo
  assignments live in a new metadata-only `credentials.json`
  (`core/CredentialsStore`, mirroring `ProjectStore`); the pure resolver
  `resolveIdentity(repoPath, candidateProjectIds)` takes the priority order from the
  ui so core stays free of ProjectStore coupling. → `core/credentialsstore.{hpp,cpp}`,
  `GitRepo::{setLocalIdentity,clearLocalIdentity,setGlobalIdentity,effectiveIdentity,localIdentity}`,
  [Plan 36](plans/2026-07-06-plan36-identity.md)

- **D50 — Secrets live in the OS keychain behind a `ui/`-side `SecretStore`; core
  stays pure and receives the plaintext only at call time.** HTTPS tokens and
  SSH-key passphrases are stored via QtKeychain (macOS Keychain / libsecret /
  Windows Credential Store), never in `credentials.json` or any GitTide file. The
  `SecretStore` seam (`KeychainSecretStore` prod, `InMemorySecretStore` for tests)
  lives in `ui/` — QtKeychain is Qt-dependent, so `core/` never sees it and the
  "no Qt in core" invariant holds. The secret is read (async, on the UI thread)
  into the core `Credentials` POD *before* a git op is dispatched to the worker, so
  the existing synchronous credential trampoline is unchanged. `Credentials` gained
  SSH-keyfile fields (public/private path + passphrase) and `chooseCredential`/the
  trampoline a `SshKey` kind (`git_credential_ssh_key_new`); `clone()` now takes
  `Credentials` too (was unauthenticated). *Why:* real secure storage that survives
  restart, without leaking a crypto dependency into the pure git engine. *Rejected:*
  an app-encrypted file (the key has to live somewhere); keeping session-only
  tokens (the deferred status quo — lost every quit). *Headless/CI:* no keyring →
  the keychain job errors and GitTide degrades to the per-session prompt; tests
  inject `InMemorySecretStore` and never touch a real keychain (a real round-trip
  can block on an OS access prompt). QtKeychain is pulled via FetchContent
  (`BUILD_WITH_QT6`, pinned `v0.14.0`), Linux needs `libsecret-1-dev`. →
  `ui/secretstore.{hpp,cpp}`, `core/sync.hpp`, `core/credentialselect.cpp`,
  [Plan 37](plans/2026-07-06-plan37-keychain-secrets.md)

- **D51 — Forge (GitHub/GitLab) token validation lives in `ui/` via
  `QNetworkAccessManager` + `QJsonDocument`; there is no forge client in `core/`.**
  Adding a host account validates the token with `GET {apiBase}/user`
  (`Authorization: Bearer`) and reads `login`/`name`/`email` to confirm the token
  and pre-fill an identity (`ForgeClient` → `CredentialManager::validateAndAddHost`).
  It parses with `QJsonDocument` — **never nlohmann/json**, which stays private to
  `core/` — and reuses QCoro's signal-await (`qCoro(reply, &QNetworkReply::finished)`)
  so no QCoro network module is needed; only `Qt6::Network` is added. *Why:* GitTide
  is a git client, not a forge client — the first cut is token validation +
  identity prefill, not PR/issue integration, and the HTTP client belongs at the Qt
  boundary. *Rejected:* a forge API layer in `core/` (would drag an HTTP + JSON
  stack into the pure git engine and break the nlohmann-private invariant); full
  forge integration (out of scope). *Testing:* a local `QTcpServer` serves canned
  JSON so validation is exercised with no live network. → `ui/forgeclient.{hpp,cpp}`,
  [Plan 38](plans/2026-07-06-plan38-forge-central-ui.md)

- **D52 — Author avatars are Gravatar-first (forge deferred), fetched by a `ui/`
  `AvatarService`, network-on by default.** v1 resolves an author email to an image
  via mem → disk (keyed by `md5(email)`, with a TTL + negative-cache marker) →
  Gravatar (`d=404` probe, then `d=identicon`); the decoded image reaches QML through
  an async `image://avatar/<hash>` provider. Network loading defaults **on** — only
  an MD5 hash leaves the machine (industry-standard; GitHub Desktop / GitKraken
  behave the same) — behind a session-only toggle. *Why:* Gravatar needs no auth,
  host detection, or per-user API calls and works for every repo, so it ships the
  recognisability win immediately; GitHub has no clean public email→avatar endpoint
  (would need a token + commits-API lookups), and GitLab's `avatar?email=` only helps
  GitLab remotes. *Rejected:* forge-API-primary in v1 (token/host/rate-limit
  plumbing for marginal coverage — the resolution chain is left ordered so a forge
  step can prepend later); network-off-by-default (hurts the out-of-box polish for a
  hash-only request); an avatar path through `core/` (violates no-Qt-in-core and
  core's offline determinism). *Threading:* the provider hops the fetch off the QML
  pixmap-reader thread onto the service's thread, since its `QNetworkAccessManager`
  is main-thread-affine. → [`engineering`](spec/engineering/engineering.md#author-avatars),
  [Plan 39](plans/2026-07-20-plan39-avatars-and-local-remote.md)

- **D53 — Local-only commits are computed in `core/` by hiding `refs/remotes/*` in a
  revwalk, and cued by shape + dim, never colour alone.** `GitRepo::localOnlyOids()`
  pushes HEAD and hides every remote-tracking tip, returning exactly the unpushed
  OIDs; the History cue is a `↑` row badge + dimmed-pushed / full-strength-local
  text, and a hollow `GraphColumn` dot. *Why:* the revwalk keeps the commit walk and
  `GraphBuilder` uncoupled from remote state and costs only O(ahead), and it maps
  onto the model's existing oid-map role pattern; the combined shape/dim cue honours
  the never-colour-alone invariant (D19). It rides the History refresh cascade and
  re-emits after fetch/pull/push so pushed-ness stays live. *Rejected:* a `bool
  pushed` field on `CommitNode` (forces every `log` caller to pay for remote
  resolution and couples graph layout to sync state); a colour-only treatment. →
  [`engineering`](spec/engineering/engineering.md#local-only-vs-pushed-commits),
  [Plan 39](plans/2026-07-20-plan39-avatars-and-local-remote.md)
- **D54 — The macOS `.app` is finished by a post-`macdeployqt` script, not by
  `macdeployqt` alone.** [`packaging/macos/macdeploy.py`](packaging/macos/macdeploy.py)
  runs `macdeployqt`, then does two fixups it does not do reliably (at least with a
  Homebrew Qt): it rewrites the absolute `/opt/homebrew/...` install-names and
  references it leaves on the transitively pulled-in *qtdeclarative* frameworks to
  `@rpath` (else the bundle loads the *system* Qt and is not portable), and it
  **ad-hoc codesigns** every Mach-O in the bundle — including the QML plugin dylibs
  under `Contents/Resources/` that `codesign --deep` does not descend into. *Why
  signing is mandatory:* `macdeployqt`'s load-command edits invalidate the
  signatures Homebrew shipped, and Apple Silicon SIGKILLs a process the moment it
  maps a page whose signature does not match ("Code Signature Invalid"). *Signing
  order:* sign the Resources-tree dylibs individually first, then one
  `codesign --deep` on the app seals everything consistently. The bundle is ad-hoc
  signed, **not notarized** — fine locally; a fresh Mac needs right-click → Open.
  *Rejected:* `install(TARGETS ... BUNDLE)` for `cmake --install` (its install-time
  RPATH rewrite + re-sign undoes the fixups and breaks the signature — we
  `install(DIRECTORY)` the finished bundle verbatim instead). Full notarization +
  native installers remain a wish. →
  [`engineering`](spec/engineering/engineering.md#build--test),
  [deployment-packaging](wishlist/deployment-packaging.md)

- **D55 — Background auto-fetch is silent; failures never raise UI on a timer.**
  A branch that tracks an upstream is fetched every ~3 min from a QML `Timer`, so
  ahead/behind and the Pull/Push affordances stay fresh. The timer calls a dedicated
  `RepoViewModel::autoFetch()` that sets an `m_silentSync` flag for the in-flight
  op; while set, the ViewModel swallows the controller's `authFailed`
  (no credential dialog) and `operationFailed` (no error toast). The manual **Fetch**
  button (`fetch()`, `silent=false`) stays the way to force a refresh and to surface
  an auth prompt. *Why:* an unattended token prompt or error toast popping every few
  minutes is worse than a stale count; the explicit button covers the authenticated
  case. *Rejected:* a backend timer in the controller (the UI already owns the
  repo-open lifecycle and gating on `hasUpstream`/`syncing`); auto-fetching every
  open repo regardless of upstream (wasteful, and errors on remote-less repos). →
  [`product`](spec/product/product.md#syncing)

- **D56 — Discarding a submodule resets it to its pinned commit, not a checkout of
  the gitlink.** `GitRepo::discard` detects a submodule path via
  `git_submodule_lookup`; a plain `git_checkout_head` only rewrites the superproject
  index/gitlink and leaves the submodule's own working tree untouched, so a moved or
  dirty submodule would stay modified. Instead it resets the superproject index to
  HEAD, then force-updates the submodule (`git_submodule_update`, the libgit2
  equivalent of `git submodule update --force`) so the submodule checks out its
  pinned commit. *Why:* "discard" must actually revert the visible change; the old
  path silently no-op'd on submodules. *Rejected:* recursing into the submodule repo
  by hand to checkout the pin (reimplements `git_submodule_update`). →
  [`product`](spec/product/product.md#changes-tab)

- **D58 — Network ops get a UI watchdog + reused-callback cancel, not a core
  cancellation API.** A fetch/pull/push on an unreachable remote (e.g. an internal
  repo off-VPN) used to hang the UI forever. Three layers bound it: libgit2 server
  connect/read timeouts (HTTPS only), a per-op cancel flag that the existing
  `ProgressCallback` observes (returning `false` aborts the transfer via the
  trampoline — SSH-capable, no core signature change), and a 30 s `QTimer` watchdog
  + generation counter on `RepoController` that returns the UI to idle even while
  the worker thread stays blocked. *Why the watchdog:* `QtConcurrent::run` isn't
  cancelable and libgit2's connect phase never invokes the progress callback, so
  the worker genuinely cannot be interrupted mid-connect — the UI must be freed
  independently and the stale coroutine dropped via the generation guard (kept
  valid by the `QPointer self` + the worker's captured `impl` `shared_ptr`).
  *Rejected:* a dedicated core cancellation-token API (the `bool` callback already
  carries the signal, and Qt must stay out of `core/`); truly interrupting the
  worker (not possible for the connect-phase hang); relying on the libgit2 server
  timeouts alone (they don't cover SSH). Keychain awaits are deliberately left
  unbounded — a QCoro timeout race there risks destroying a live `QKeychain::Job`,
  and the op is local. →
  [`engineering`](spec/engineering/engineering.md#network-operations--credentials)

## Design

- **D17 — One accent (Material Blue brand); never a second hue** for emphasis.
  *Why:* brand coherence. *(Was cyan on a navy ground; reworked to a neutral
  Material Grey ground with a Material Blue accent — 2026-07-20.)* →
  [`design`](spec/design/design.md)
- **D18 — Colour comes only from tokens; both themes define every token.** *Why:*
  adding a theme is adding one column, and no widget hard-codes a hex. →
  [`design`](spec/design/design.md)
- **D25 — Tokens drive a `QPalette` over Fusion, not a full QSS skin.** Supersedes
  the "ThemeManager produces a full QSS string" approach: with Fusion as the base
  style (D24), tokens resolve into a `QPalette` plus a small accent stylesheet for
  the few cues a palette can't express (selection border, tab underline, focus
  ring, diff gutter). *Why:* less hand-maintained QSS, a more native look, while
  the token-only colour rule (D18) still holds. → [`design`](spec/design/design.md)
- **D19 — Never signal state by colour alone** — always pair with an icon/letter.
  *Why:* accessibility. → [`design`](spec/design/design.md)

- **D41 — One themed `AppButton`/`AppComboBox` replaces per-call-site inline
  styling; raw Basic controls are not used for plain actions or dropdowns.**
  Every dialog footer button and in-pane action button uses `AppButton`
  (`primary` / `secondary` / `danger` + `compact`); every plain dropdown uses
  `AppComboBox`. The `danger` variant reuses the `stateDeleted` token (no new
  token). *Rejected:* continuing to inline `contentItem`/`background` per call site
  (duplicated, inconsistent, unthemeable at a glance); a fourth-party control
  library. *Exceptions:* `MainTab`, `WindowButton`, `AppRadioButton`/`AppCheckBox`,
  and `EmptyState.Cta` keep bespoke styling — they are distinct, non-action
  controls. → [`design`](spec/design/design.md),
  [`themed-controls design doc`](spec/product/2026-06-26-themed-controls-design.md)

- **D42 — Sidebar repo rows show the current branch and working state, not a
  commit hash.** Each top-level repo row is a two-line entry: name + dirty badge
  (changed-file count, or a clean check), then the current branch + ahead/behind
  arrows. No upstream shows `—`; a detached HEAD shows `detached <shortOid>`.
  *Why:* the branch and dirty/sync state are what a user scans a multi-repo
  project for; a raw hash carried little at-a-glance value. Per D19, every state
  pairs a glyph with its colour. Row state is seeded in `RepoListModel::setRepos`
  and refreshed on the existing fleet-poll path (no new watcher). *Rejected:*
  keeping the name-only row; showing the HEAD short OID for repos (submodule rows
  still show their pinned OID, whose identity *is* the commit). →
  [`repo-tree-entry design doc`](spec/product/2026-07-20-repo-tree-entry-redesign-design.md)

- **D43 — Submodule rows show branch + dirty + ahead/behind vs the *pinned*
  commit, not a remote upstream.** An initialized submodule gets the same
  two-line entry as a repo; its ahead/behind counts the submodule's current HEAD
  against the commit the superproject pins (the submodule's contract), computed
  on the submodule's own repository via `git_graph_ahead_behind`. Sync indicators
  are right-aligned into a column (repos and submodules). The detail is filled
  once in `submoduleTree()`, so the load-seed and the 5 s poll share one path.
  *Why:* "have I drifted from the pin, and which way" is the multi-repo question;
  a remote-upstream comparison would need N recursive repo opens and answers a
  different question. *Rejected:* ahead/behind vs the submodule's own remote;
  a lightweight off-pin marker without counts (the user wanted exact counts). A
  shallow submodule missing the pinned commit simply shows no arrows. →
  [`submodule-detail design doc`](spec/product/2026-07-20-submodule-detail-and-right-align-design.md)

- **D57 — Dialog layout lives in shared primitives, not per-dialog.** `AppDialog`
  owns the cross-cutting behaviour and two wrappers own the body/footer: it
  **centres in the window** (parents to `Overlay.overlay`, so a dialog declared
  inside the diff pane no longer centres over that pane) and **sizes to content**
  (derives `implicitHeight` and sets `height`, because QtQuick's `Dialog` drops the
  content height from its implicit size once a `footer` exists — the card sizes too
  short, overflowing content and overlapping the footer). Bodies wrap their stack in
  **`DialogColumn`** (a `ColumnLayout` used *directly* as a Popup `contentItem`
  reports implicit height 0 — a plain Item wrapper fixes it) and footers use
  **`DialogButtons`** (a bare `RowLayout` footer sits flush to the border —
  `Layout.margins` there is a no-op — and gives the Dialog no stable footer height).
  *Why:* the New-branch dialog visibly overflowed (the base-branch combo escaped the
  card) and every dialog's buttons hugged the edge; fixing it once in the base keeps
  all ~18 dialogs correct. *Rejected:* per-dialog explicit heights/margins (fragile,
  and re-broken by any content change); `childrenRect.height` for the content height
  (a binding loop Qt silently breaks to 0). →
  [`design`](spec/design/design.md#components)

## Process

- **D20 — A living spec, not append-only dated specs.** Design lands in a
  domain-organised spec that stays current (`wishlist → spec → plans`); symbol-level
  docs live in Doxygen comments, cross-cutting design in the spec, and the **code is
  ground truth** when the two drift. *Why:* one always-current source of truth
  instead of reassembling intent from a pile of dated plans. →
  [`spec.md`](spec/spec.md), [`workflow.md`](workflow.md)
