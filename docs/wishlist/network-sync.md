# Network sync — fetch / pull / push

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `partly shipped` — per-repo fetch / pull / push + ahead/behind + credentials **done** (Decision D28 turned transports on); fleet **fetch-all** **shipped** ([Plan 11](../plans/2026-06-22-plan11-fleet-fetch-all.md)); determinate **transfer progress** for fetch/pull/push **shipped** ([Plan 12](../plans/2026-06-22-plan12-ui-polish-sync-progress.md)). Remaining: fleet pull-all, cancel, OS-keychain creds. |
| **Touches** | product (sync actions + remote state), engineering (core: remotes + transport on `GitRepo`, credentials, progress/cancel), design (sync UI, ahead/behind, auth prompts) |

## What

Connect a repo to its **remotes** and move commits across the network, per repo:

- **Fetch** — update remote-tracking refs from a remote (no working-tree change),
  with progress + cancel.
- **Pull** — fetch then integrate the upstream into the current branch
  (fast-forward where possible; a real merge/rebase pull is gated on those wishes).
- **Push** — publish the current branch to its upstream, including setting an
  upstream on first push.
- See **ahead/behind** counts against the upstream and the configured remotes.

Plus the **Project-level** payoff GitTide is built for: **fetch/pull all** across
every repo in a project, in parallel, with aggregated progress.

## Why

This is the single biggest gap between GitTide and a usable daily client, and the
one the product spec explicitly defers (Decision D2/D13). Clone already proves the
transport machinery exists; fetch/pull/push complete the loop so a user never has
to leave for the terminal to sync. And **fetch/pull-all across a project** is
exactly the multi-repo differentiator made real — keeping a fleet of coordinated
repos up to date is one action instead of N.

## Notes

- **This turned the network tier ON.** Decision D13 kept SSH/HTTPS transport off;
  Decision **D28 superseded it** — transports are now on (HTTPS everywhere, SSH on
  Linux/macOS). Per-repo fetch/pull/push shipped on top of that. The remaining
  unbuilt piece is the fleet-wide bulk op.
- **Credentials — the hard part.** HTTPS (token/password) and SSH (key, agent,
  passphrase) both need libgit2 credential callbacks, secure storage (OS keychain,
  never plaintext in `projects.json`), and a UI prompt flow. Decide supported
  transports for a first cut (HTTPS-token only? + SSH-agent?) and where secrets
  live. This alone may warrant its own decision log entry.
- **Layering.** Remotes + transport are `core/` on `GitRepo` (libgit2
  `git_remote_*`, fetch/push with callbacks), returning `Expected<T>`; credentials
  cross the ViewModel boundary carefully — the prompt is Qt/UI, the credential
  *value* must not leak into a core public header. No git command strings.
- **Async + cancel.** Network ops are slow and must be cancellable, off the UI
  thread, with determinate-ish progress like clone. Project-wide fetch-all is
  many-repos-in-parallel → per-worker repo instances, never a shared `GitRepo`
  (the one-owner invariant), with aggregated progress/cancel across the fleet.
- **Pull integration depends on other wishes.** Fast-forward pull is self-contained;
  a pull that must merge or rebase pulls in [merge](shipped/merge.md) /
  [rebase](rebase.md) and conflict handling. First cut: fetch, push, FF-only pull.
- **Push safety.** Reject non-fast-forward by default; force-push is a separate,
  guarded action (and dangerous — gate it like discard). Show ahead/behind so the
  user understands what a push/pull will do before doing it.
- **Failure modes.** Auth failure, rejected push, network down, detached HEAD, no
  upstream configured — each needs a clear `Expected` error and a sensible UI
  message, not a crash or a silent no-op.
- **Scope creep to resist.** No PR/forge integration, no submodule-recursive
  fetch in the first cut, no per-repo remote *management* UI beyond what push/pull
  needs. Land fetch + push + FF-pull + ahead/behind first.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (sync actions, ahead/behind, fetch-all), spec/engineering (core remotes+transport, credentials, async fleet) · plan: plans/<file>
- Reopens Decision D13 (network off) and adds a credentials/transport decision → log in decisions.md
-->
