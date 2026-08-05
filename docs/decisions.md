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

- **D35a (amends D35) — the active repo also gets a status-only safety net, and
  refresh is made coherent end to end.** Practice showed D35's mechanism was right
  but its plumbing leaked, in four ways. (1) The residual gap D35 closed with
  focus-resync alone is not closed by it: with GitTide and an editor side by side
  the window never loses focus, so an in-place save stayed invisible. The active
  repo now runs `status()` — and *only* status, never the cascade — on a low-
  frequency timer gated on window focus, skipped whenever the watcher has fired
  inside the last interval or a refresh is already running. This is deliberately
  not the "poll the active repo" D35 rejected: that meant the full cascade on a
  timer, which is what lagged and churned. (2) Re-arming the watch set after every
  cascade was a remove-all/add-all rebuild that discarded any event received during
  the refresh; it is now an incremental set diff that leaves the debounce batch
  alone. (3) The self-mute was a plain flag with a timed release, so nested guards
  released each other mid-cascade; it is now reference-counted with a generation
  check. (4) The active repo's own refreshes never reached its sidebar row, which
  only the fleet poll wrote — so an in-app revert left a stale dirty badge for a
  poll interval, or forever while unfocused. The open repo now pushes its head /
  status / sync into its row directly, and the poll skips it in return. The poll is
  additionally single-flight (passes were stacking on the shared thread pool), and
  the sidebar's rows are built without any git I/O on the UI thread — hydration
  moved to that same poll. *Rejected:* dropping the focus-resync in favour of the
  timer (focus-in is still the cheapest catch-up after a long background spell);
  making the safety net a full `refreshAll` (the churn D35 rightly rejected);
  reference-counting mute without the generation check (a deferred release could
  still land inside a later mute). →
  [`engineering`](spec/engineering/engineering.md),
  [Plan 48](plans/2026-08-04-plan48-refresh-coherence.md)

- **D35b — the ViewModel owns list selection; QML binds to it and never assigns
  `currentIndex`.** Every list (changed files, commit files, history, graph) used
  to paint its highlight from its own `ListView.currentIndex`, assigned in click
  and key handlers, while `RepoViewModel` separately owned which file/commit was
  actually loaded. The two diverged on every selection the ViewModel made without
  a click — auto-selecting a commit's first file so its diff loads, preserving the
  active file across a status refresh, re-anchoring after a rebase, clearing on
  repo switch — and a model reset silently snapped `currentIndex` to 0. The
  ViewModel now exposes the selected **row** (`activeFileRow`,
  `activeCommitFileRow`, `selectedCommitRow`, `selectedGraphRow`); QML binds
  `currentIndex` to it one-way and calls only `select*` verbs. Keyboard navigation
  goes through the same verbs rather than moving the index itself. *Why:* one
  writer, one direction — a highlight that disagrees with the pane beside it is not
  a rendering bug that can be patched per list, it is two owners of one fact.
  *Rejected:* a `SelectedRole` on each model (the row is derived state, not row
  data, and it would re-emit `dataChanged` across two rows per selection);
  syncing `currentIndex` from a signal handler (restores the second writer). →
  [`engineering`](spec/engineering/engineering.md),
  [Plan 48](plans/2026-08-04-plan48-refresh-coherence.md)

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

- **D41 — History edits act-then-offer-undo, not confirm-first.** Two forced prompts
  in the interactive-rebase UX were removed because, in each, there was realistically
  only one thing to do. **(1) Squash no longer pauses for a message:** the engine
  commits with the concatenated default message (HEAD's accumulated message + the
  squashed commit's, which a squash chain accumulates naturally) and finishes in one
  run; the user rewords afterward if they want it tidied. `RebasePause::Message` now
  belongs to **reword only** (reword's whole purpose is a new message, so it keeps its
  auto-opened editor). Fixup is unchanged (retains the target's message, no pause).
  **(2) Drag-to-reorder applies immediately:** the D36 confirm modal
  (`ReorderConfirmDialog`) is deleted — the whole-row hold-to-arm gesture (D38) is
  already deliberate, so a second "are you sure?" was pure friction. The safety net in
  both cases is a **non-blocking Undo toast** (extending D40's act-then-offer-undo
  philosophy): a clean, **drop-free** edit emits `historyEditUndoable(preTip, label)`
  and `Main.qml` shows a transient "…— Undo" strip whose Undo soft-resets the branch
  back to `preTip` (`GitRepo::undoHistoryEdit`). Undo is a **soft reset** and is
  offered **only when the plan drops nothing**, because a drop-free replay yields a
  content-identical tree — so moving the ref back leaves the working tree untouched and
  loses nothing. A plan that drops a commit is excluded (its replay changes content, so
  a soft reset would misrepresent it) and keeps its editor-only path with no toast.
  *Rejected:* keeping the reorder confirm (redundant with hold-to-arm + always-reachable
  abort/undo); a hard-reset undo (would clobber uncommitted work and is unnecessary for
  content-identical replays); collecting the squash message up front (loses the
  git-faithful mid-rebase model D34 chose and re-introduces a gate). Supersedes the
  reorder-confirmation part of **D36** and the squash-message-pause part of **D38/D40**.
  → [`product`](spec/product/rebase-interactive.md)

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

- **D64 — Bulk-add repositories: rescan-on-activation, permanent per-source
  ignore, an additive schema key, and submodules never surface as repos.**
  Four choices from the same feature, each with a real rejected alternative.
  (1) A registered **repository source** is rescanned on project activation
  (plus on-demand from Project Options), not watched continuously.
  *Rejected:* a `QFileSystemWatcher` per source, matching D35's watcher for the
  active repo — a source is typically a large, often-external tree (many repos
  under one parent) the project isn't actively working in, so a live watch
  would pay a standing OS watch-descriptor cost for freshness nobody is
  waiting on; activation is already the natural "the user is paying attention"
  moment. (2) Removing a repo that came from a source is **permanent**: the
  removal is recorded in that source's `ignored` list, and a source's own
  folder counts as containing the repo when the folder *is* the repository
  (an exact-path match, not just a directory-boundary one — see the
  `ignoreInSources` fix below). *Rejected:* re-offering a removed repo on the
  next rescan (defeats the point of removing it — "auto-add" must compose with
  "the user said no"). (3) `RepoSource` is serialized as an additive
  `"sources"` array with **no `ProjectStore::kVersion` bump**: an older
  document simply loads with an empty source list. *Rejected:* bumping the
  version — the new key is purely additive and degrades safely one direction
  (an older *build*, however, silently drops `"sources"` on its next save,
  since its `to_json` doesn't know the key exists — a real one-way trap, not
  chosen deliberately but accepted as the cost of no migration machinery for
  a first cut). (4) `scanForRepos` **never returns a repository's submodules**
  — descent stops at a repository. *Rejected:* returning them as ordinary
  top-level candidates (they already reach the user through the parent's
  submodule tree; offering them again would duplicate each one as a
  free-standing repository with none of the pin/init/deinit lifecycle that
  makes it a submodule).
  *Follow-up fixed in review:* `ignoreInSources` initially matched only
  `isUnder(source, repo)`, which is false by construction when the source's
  path equals the repo's own path — exactly what registering a source on a
  folder that is itself a repository produces. Removal recorded nothing, so
  D64(2)'s permanence silently didn't hold for that one case; a source's own
  path now also counts as containing the repo. →
  [`product`](spec/product/product.md#bulk-add--repository-sources),
  [`engineering`](spec/engineering/engineering.md#bulk-add-folder-scan-and-repository-sources),
  [Bulk-add repositories plan](superpowers/plans/2026-08-05-bulk-add-repos.md)

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

- **D59 — A rename is one row (`R`), detected via libgit2, and staged/discarded as
  a pair.** `FileStatus` carries an `oldPath`; `status()` sets the
  `GIT_STATUS_OPT_RENAMES_*` flags and `commitFiles()` runs `git_diff_find_similar`,
  so a moved file collapses from a delete + add into a single renamed entry shown as
  *`old → new`* in `state.incoming` blue. Because git tracks content, not moves, the
  row still maps to **two** index operations: committing an `R` row stages the
  destination *and* removes the source, and discarding it deletes the moved copy
  while restoring the source from HEAD — the ViewModel emits both selections so the
  rename is always whole. *Why:* the old delete+add pair mis-read a move as data
  loss and split one intent across two checkboxes. *Rejected:* a distinct
  `state.renamed` theme token (reused the existing blue `state.incoming`); modelling
  renames only in commit history and leaving the working tree as delete+add (the
  working-tree view is where the user first sees the move). Rename rows always stage
  whole-file — a partial (line-level) rename stage has no meaningful UI. →
  [`design`](spec/design/design.md#components)

- **D60 — Contrast-driven palette re-tune: `text.muted` lifted, git-state colours
  split per theme, and the diff highlighter switched to token-derived KSyntax
  themes.** A full WCAG audit of the palette found `text.muted` below the spec's own
  4.5:1 body-text floor on **both** themes (dark 3.3:1, light 2.5:1) and the
  git-state colours — shared verbatim across themes — near-invisible on the light
  ground (green `A`/amber `M` at ~2.3:1). Fixes: `text.muted` → `#8E8E93` (dark) /
  `#6E6E73` (light); the git-state palette becomes **per theme** (dark keeps its
  bright-on-dark values, light darkens to GitHub-light-style hues) — a saturated
  colour cannot clear 4.5:1 on both `#1C1C1E` and `#F5F5F5` at once. The changed-file
  directory prefix moves from `text.muted` to `text.secondary`. Diff syntax
  highlighting takes the **D45 upgrade path**: two `.theme` files derived from our
  tokens (bundled in the KSyntax `themes-addons` QRC, loaded by name) replace KDE's
  Breeze palette, so the diff harmonises with the app. *Why:* the app failed its own
  accessibility invariant and the diff read as a foreign surface. *Rejected:*
  keeping one shared git-state set (can't pass on both grounds); a per-user theme
  editor (out of scope); baking syntax hexes into the widget (D18 — colours live in
  a data file, exactly like the token table). The colours stay data-driven, so D18
  and the one-accent rule (D17) hold — the syntax multi-hue set is a sanctioned
  exception like the history-graph lane colours. →
  [`design`](spec/design/design.md#git-state-colours)

- **D61 — User-friendliness pass on the live app: empty states, unified
  selection, and clearer dark separation.** A review of the *running* app (not just
  the code) found the main content pane rendered a large blank void whenever nothing
  was selected (clean repo, or before picking a commit/file), selection was
  inconsistent and partly inverted (the repo tree filled the active row with
  `surface.base` — *darker* than the sidebar, so "selected" read weaker than the
  `surface.overlay` hover, while the lists used `surface.overlay`), dark panels
  barely separated on the low-contrast ground, and the Graph tab pushed each row's
  date to the far window edge (~900px gap). Fixes: the **Changes** diff column and
  the **CommitDetail** pane gain centred empty states (faded brand mark + a one-line
  hint); **selection becomes one shared treatment** everywhere — `accent` @~0.16α +
  a 2px `accent` left border, always stronger than the neutral hover; the dark
  `border` token is lifted `#3D3D40` → `#4A4A4E` (a divider colour, never text, so
  the WCAG floor is untouched); the Graph row's text block is width-bounded so the
  date sits by the metadata; the CommitDetail files sub-pane sizes to content up to
  a cap; and `AppButton compact` height goes 22 → 24 to hold the ≥24px hit-target
  floor, with label text on the type scale (13 / compact 12). *Why:* the app looked
  unfinished on first open and the selection cue was ambiguous. *Rejected:* retuning
  `surface.raised` for separation (drops `text.muted`-on-raised below 4.5:1 — the
  border lift is safe instead); a pure-white `onAccent` button-label token (larger
  plumbing for a colour that already passes contrast — deferred); differentiating
  Graph ref pills by tag/branch/remote (needs the row model to expose ref *kind* —
  follow-up). →
  [`design`](spec/design/design.md#selection)

- **D62 — Graph ref chips carry their kind (branch / remote / tag), not just a
  name.** The D61 follow-up. Core already classified each tip (`RefTipKind`
  Branch/Remote/Tag), but `RepoController::refreshGraph` flattened tips to a bare
  `QHash<oid, QStringList>`, so every chip rendered identically — a remote-tracking
  ref and a tag both read as local branches. The oid→chip map now carries
  `QVariantList` of `{name, kind}` maps all the way to `RefLabelsRole`, and the
  Graph delegate styles the three distinctly: local branch = neutral outlined chip;
  remote = dimmed outlined chip + a `☁` glyph (reusing the branch-dropdown
  convention); tag = filled `accent` chip. Outline→fill is a shape cue, so the kind
  is never colour-only. *Why:* a graph where you can't tell a tag or a remote from a
  local branch is missing the point of an all-refs graph. *Rejected:* a string
  heuristic on the name (e.g. treat `origin/…` as remote) — fragile against branches
  with slashes and blind to tags, when core already knows the exact kind. →
  [`design`](spec/design/design.md#qml-history-view)

- **D63 — Close the D61 follow-ups: `on.accent` label token and stable Pull/Push
  slots.** The two items D61 deferred. (1) Filled `AppButton` labels read from a new
  `on.accent` token (white in both themes) instead of `surface.base`, so a primary
  or danger button's label is crisp on the blue/red fill rather than near-black —
  the earlier colour passed WCAG but read muddy. (2) Pull and Push keep a **stable
  slot** whenever the branch has an upstream (matching the spec's "appear only with
  an upstream"): they no longer pop in and out as the ahead/behind counts cross
  zero — which shifted their neighbours and broke muscle memory — but instead
  **disable** (greyed, no pill) when there's nothing to pull/push. *Why:* predictable
  control positions and a crisper primary action. *Rejected:* a per-theme `on.accent`
  (white works on both fills, so one value suffices — the "both themes defined" rule
  is still met); always-enabled Pull/Push (a disabled state signals "nothing to do"
  more clearly than a live button that no-ops). →
  [`design`](spec/design/design.md#accent-brand)

## Process

- **D20 — A living spec, not append-only dated specs.** Design lands in a
  domain-organised spec that stays current (`wishlist → spec → plans`); symbol-level
  docs live in Doxygen comments, cross-cutting design in the spec, and the **code is
  ground truth** when the two drift. *Why:* one always-current source of truth
  instead of reassembling intent from a pile of dated plans. →
  [`spec.md`](spec/spec.md), [`workflow.md`](workflow.md)
