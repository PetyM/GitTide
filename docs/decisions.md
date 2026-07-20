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

## Process

- **D20 — A living spec, not append-only dated specs.** Design lands in a
  domain-organised spec that stays current (`wishlist → spec → plans`); symbol-level
  docs live in Doxygen comments, cross-cutting design in the spec, and the **code is
  ground truth** when the two drift. *Why:* one always-current source of truth
  instead of reassembling intent from a pile of dated plans. →
  [`spec.md`](spec/spec.md), [`workflow.md`](workflow.md)
