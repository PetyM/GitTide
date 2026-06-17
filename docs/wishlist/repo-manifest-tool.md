# `repo` tool — manifest-driven multi-repo projects

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `idea` |
| **Touches** | product (Project ↔ manifest), engineering (core: manifest model + sync engine, persistence), design (sync UI/status) |

## What

Give GitTide a built-in equivalent of Google's [`repo`](https://gerrit.googlesource.com/git-repo/) tool: drive a whole set of repositories from a single **manifest** file instead of adding each repo by hand. Concretely, the user can:

- **`repo init`** — point a project at a manifest (a local file or a remote
  manifest repository) and initialise the workspace from it.
- **`repo sync`** — clone every repository the manifest lists that isn't present
  yet, fetch/update the ones that are, and check each out to the revision the
  manifest pins (branch, tag, or commit). New entries appearing in the manifest
  are **added to the project automatically** on the next sync; the project's repo
  set converges to the manifest.
- Work with the **manifest** itself inside GitTide — view it, see which repos it
  defines vs. which are actually checked out, and (later) edit it.

A manifest-backed **project** maps naturally onto GitTide's existing first-class
[**Project**](../spec/product/product.md) (a named grouping of repositories). The
intent: **a project can be *defined by* a manifest** — its repo list becomes a
projection of the manifest rather than a hand-curated list. But this is not
forced: a project may stay manually managed, or be manifest-backed. **repo ≈
gittide project, but not necessarily.**

## Why

Today a project's repos are added one at a time and the set lives only in
`projects.json`. That doesn't scale to the real reason multi-repo grouping
exists: teams that ship a product as *many* coordinated repositories want one
declarative source of truth for "these are the repos, at these revisions," that
they can version, share, and reproduce on a fresh machine. A manifest + sync
turns "set up the project on a new clone" from a manual chore into one action,
keeps everyone's repo set in lockstep, and makes the multi-repo Project concept —
GitTide's differentiator — genuinely powerful instead of just a folder of
bookmarks.

## Notes

- **Format / compatibility — open question.** Adopt the existing Android `repo`
  XML manifest (interop with established tooling, well-understood semantics:
  `<remote>`, `<default>`, `<project name path revision>`, includes), or define a
  GitTide-native manifest (likely JSON, consistent with `projects.json` /
  core's nlohmann-json persistence)? Supporting the real `repo` XML is more
  useful to teams already on it; a native format is simpler and on-brand. Could
  also do native-first with an XML importer.
- **Project ↔ manifest relationship.** Decide the binding: is the manifest the
  source of truth and the project a cache/projection of it, or is the project
  primary with the manifest as one way to populate it? Affects `projects.json`
  (does a project gain a `manifest` reference + remote? do manifest-derived repos
  get flagged so the UI knows they're managed?) and what happens when the user
  manually adds/removes a repo in a manifest-backed project.
- **Sync semantics — needs design.** What `repo sync` does to a repo with local
  changes, untracked work, or a divergent branch (skip? warn? never clobber?).
  Honour the existing invariant spirit: don't destroy user work silently. Pinned
  revisions can be a branch (track + fast-forward), a tag, or a fixed commit —
  each checks out differently.
- **Async + concurrency.** Sync is inherently many-repos-in-parallel — clone N,
  fetch M — which fits GitTide's per-worker-repo model and the `AsyncRepo`
  bridge, but is a bigger orchestration than today's single-repo operations.
  Respect the invariant: **one owner per `GitRepo`, parallelism via per-worker
  instances, never a shared repo.** Progress/cancellation across a fleet of
  clones is real UI work.
- **Layering.** The manifest model + parser and the sync engine are pure
  multi-repo logic → they belong in **`core/`** (std + libgit2 only, no Qt),
  surfaced through the ViewModel/`AsyncRepo` boundary like everything else. Core
  already speaks `Expected<T>`; sync results/errors should too.
- **Scope creep to resist (YAGNI).** The real `repo` tool is huge (groups,
  linkfiles/copyfiles, superproject, gerrit upload, partial-clone). A first cut
  is `init` + `sync` + a read-only manifest view; the rest are later wishes.
- **Entry point.** "repo init / repo sync" reads as commands, but GitTide is a GUI
  — these surface as project-level actions (e.g. "Create project from manifest…",
  a "Sync" button), not a CLI. The CLI verbs are just the mental model.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/product (Project ↔ manifest, sync flow), spec/engineering (core manifest model + sync engine, persistence) · plan: plans/<file>
- Manifest format (Android-repo XML vs native JSON) rejects a real alternative → log in decisions.md
-->
