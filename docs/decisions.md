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

## Design

- **D17 — One accent (cyan brand); never a second hue** for emphasis. *Why:* brand
  coherence. → [`design`](spec/design/design.md)
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

## Process

- **D20 — A living spec, not append-only dated specs.** Design lands in a
  domain-organised spec that stays current (`wishlist → spec → plans`); symbol-level
  docs live in Doxygen comments, cross-cutting design in the spec, and the **code is
  ground truth** when the two drift. *Why:* one always-current source of truth
  instead of reassembling intent from a pile of dated plans. →
  [`spec.md`](spec/spec.md), [`workflow.md`](workflow.md)
