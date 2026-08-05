# Bulk-add existing repositories

| | |
|--|--|
| **Added** | 2026-06-18 |
| **Status** | `done` |
| **Shipped** | 2026-08-05 |
| **Touches** | product (a scan-and-pick dialog on the "add existing" flow), engineering (core: scan a folder for git repos; ViewModel: add many in one pass), design (checklist dialog reusing existing dialog styling) |

## What

Add many existing repositories at once instead of one at a time. Point GitTide at
a parent folder (e.g. `~/projects`), and it finds every git repository directly
inside it and offers them as a **checklist** — all pre-checked — so the user
unticks the few they don't want and confirms. Everything left checked is added to
the **active project** in a single action.

This extends today's "Add Existing Repository" flow, which only takes one folder
that must itself be a repo. The bulk flow takes a folder *of* repos.

## Why

People keep their repos grouped under one parent directory. Onboarding a project
that spans a dozen repos today means a dozen trips through the file dialog. Folks
who clone a whole org or workspace at once hit this immediately. One scan-and-pick
replaces N manual adds, which is exactly the kind of friction the Project concept
exists to remove.

## Notes (optional)

- **Scan depth — direct children only.** Look at each immediate subfolder of the
  chosen parent and keep the ones that are git repos (`GitRepo::open` succeeds /
  a `.git` is present). No recursion in the first cut — predictable, fast, and
  matches the common `~/projects/<repo>` layout. (Recursive discovery is the
  ambition of the separate [`repo` manifest tool](../repo-manifest-tool.md) wish;
  don't pull it in here.)
- **Selection — checklist dialog.** After the scan, show the discovered repos in
  a list with checkboxes, all pre-checked. Show each repo's folder name (and full
  path on hover / as subtext). Already-in-project repos appear disabled/greyed
  with an "already added" hint rather than being silently dropped, so the user
  sees the whole picture. Confirm adds all still-checked repos.
- **Target — active project.** Same rule as single-add today: everything lands in
  the currently active project. No project picker in the first cut (the existing
  flow already requires an active project; reuse that precondition and its
  "No active project" guard).
- **Layering.** The folder scan is pure filesystem + git validation → belongs in
  `core/` (e.g. a `scanForRepos(path) -> Expected<std::vector<...>>` returning
  candidate repo paths), no Qt. The ViewModel adds the selected set, reusing
  `ProjectStore::addRepo` per repo. `addRepo` already rejects duplicate paths, so
  re-adds are naturally idempotent — surface that as the "already added" state
  rather than an error.
- **Add-many semantics.** Adding N repos should save the store once and refresh
  the repo model once, not N times. Decide partial-failure behaviour: if one repo
  fails validation at add-time, add the rest and report the failures (a short
  summary), never abort the whole batch.
- **Empty scan.** If no git repos are found directly under the folder, say so
  clearly ("No git repositories found in <folder>") instead of opening an empty
  checklist.
- **Scope to resist (YAGNI):** no recursive scan, no per-repo alias editing in the
  bulk dialog, no project picker, no "scan deeper" toggle. Direct children →
  checklist → add to active project is the whole first cut.

---

Graduated:

- Designed in: [`spec/product`](../../spec/product/product.md#bulk-add--repository-sources),
  [`spec/design`](../../spec/design/design.md#components),
  [`spec/engineering`](../../spec/engineering/engineering.md#bulk-add-folder-scan-and-repository-sources).
- Plan: [`superpowers/plans/2026-08-05-bulk-add-repos.md`](../../superpowers/plans/2026-08-05-bulk-add-repos.md).

**Went beyond the original wish:**

- **Scan depth is configurable, not direct-children-only.** The first cut above
  scoped out a "scan deeper" toggle deliberately (YAGNI). The shipped dialog has
  a depth stepper (default 2, 1 = direct children) because the common
  `~/projects/<org>/<repo>` layout needs to look two levels down, not one — the
  original direct-children-only design would have missed it entirely for anyone
  organising repos that way.
- **Repository sources were added beyond scope.** The wish above deliberately
  ruled nothing like this in — it was a one-shot scan-and-pick. What shipped
  also lets a scanned folder be *remembered* as a source, rescanned
  automatically on every project activation so repositories that appear later
  (a fresh clone, a newly created repo) join the project without the user
  revisiting the dialog, plus a Project Options section to inspect, rescan, and
  remove sources. This turned out to be the natural extension once the folder
  scan existed, and closes a real gap the one-shot version left: a user who
  adds a dozen repos from a folder today has no story for the thirteenth repo
  cloned into that folder tomorrow.
