# Author avatars in commit history

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `idea` |
| **Touches** | product (avatar in History rows), design (avatar size/placement, fallback/placeholder), engineering (ui-side avatar fetch + cache; **not** core) |

## What

Show a small **avatar** next to each commit's author in the History tab (and
anywhere an author is shown), instead of just a name + email. The user scans
history visually — recognising contributors by face is faster than reading names.

- An avatar appears in each History row beside the author.
- While loading (or when none is available) a **deterministic placeholder** shows
  — e.g. initials on a colour derived from the email — so rows never jump.
- Avatars are **cached** so scrolling a large history doesn't refetch.

## Why

Pure recognisability and polish. GitHub Desktop, GitKraken, etc. all show author
faces; it's a small touch that makes history feel alive and lets the user pick out
"who did this" at a glance — especially valuable in GitTide's multi-repo Projects,
where many contributors flow past. Low functional risk, high perceived quality.

## Notes

- **Layering — avatars are a `ui/` concern, not `core/`.** Core already exposes
  the author **name + email** per commit; that's all core owes. Resolving an email
  to an image (network fetch, decode, cache) is a Qt/UI job — keep it out of core
  (no Qt in core, and core stays offline/deterministic for tests). The History
  ViewModel asks a UI-side avatar service for "image for this email."
- **Source — open question.** Most clients hash the author email and hit
  **Gravatar** (`https://www.gravatar.com/avatar/<md5(email)>?d=…`). Alternatives:
  a forge API (GitHub/GitLab) which needs auth and a host guess, or purely-local
  identicons with no network at all. A sensible first cut: **Gravatar with a
  generated-identicon fallback** (`d=identicon`), and an **offline/disabled mode**
  that uses only local initials — so the feature degrades cleanly with no network.
- **Network + privacy.** This makes outbound requests keyed on author *email*
  hashes — a privacy consideration worth a setting (**"load avatars from the
  network"**, default on or off — decide). It does **not** depend on the git
  network tier ([network sync](network-sync.md)) — it's plain HTTP image fetch
  from the UI, not libgit2 transport — so it can land independently of fetch/push.
- **Caching & async.** Fetch off the UI thread; cache in memory and on disk (keyed
  by email hash) with a sane TTL, so a 10k-commit history reuses a handful of
  images. Virtualized History rows mean the avatar service must serve the
  currently-visible rows and not stampede the network — coalesce/limit in-flight
  requests.
- **Fallback must be instant and stable.** Never block a row on a network image.
  Show the deterministic initials-on-colour placeholder immediately, swap in the
  real avatar when it arrives. Colour from a theme token (per the invariant — no
  hex literals in widgets), derived deterministically from the email.
- **Scope creep to resist.** No avatar editing, no per-identity overrides, no
  mailmap resolution in the first cut (though mailmap could be a later nicety). One
  size, History only, then expand to other author sites if it pays off.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (avatar in History), spec/design (size/placement/fallback), spec/engineering (ui avatar service + cache) · plan: plans/<file>
- Avatar source (Gravatar vs forge API vs local-only) and the network/privacy default → log in decisions.md
-->
