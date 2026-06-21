# Logging — categories with per-category and global levels

| | |
|--|--|
| **Added** | 2026-06-21 |
| **Status** | `done` |
| **Touches** | engineering (a logging facility across `core` / `ui` / `app`), product (a way to see/raise verbosity when something goes wrong) |

## What

A single, robust logging facility used **across the whole application** — `core`,
`ui`, and `app` — so that what GitTide does (and why it failed) is observable
instead of silent.

- **Sensible categories.** Log lines are tagged by a small, named set of
  categories (e.g. `git` for libgit2 operations, `repo`/`async` for the
  worker/refresh cascade, `auth` for credentials, `ui` for view-model/QML, `app`
  for startup/composition). One coherent taxonomy, not ad-hoc strings.
- **Levels.** The usual ladder — `trace`/`debug`/`info`/`warning`/`error` (or the
  Qt equivalents).
- **Per-category *and* global control.** The user (or a developer) can raise or
  lower verbosity **globally** and **override it per category** — e.g. silence
  everything but turn `git` up to `debug` while chasing a fetch bug.
- **Logging from the GUI.** The QML/ViewModel layer must be able to log through
  the same facility (not stray `console.log`/`qDebug` that bypasses the category
  + level controls).

## Why

Today there is **no logging at all** — no `qDebug`, no message handler, nothing.
When a clone stalls, a checkout refuses, or an auth callback misbehaves, there is
no record of what happened: diagnosis means reaching for a debugger or guessing.
A categorised, level-controlled log turns "it doesn't work" into an actionable
trace, makes support/bug reports possible, and gives developers a fast dial to
turn up exactly the area they're investigating without drowning in noise from the
rest. Per-category control is what keeps it usable as the app grows.

## Notes (open questions for the design phase)

- **The core/no-Qt tension is the crux.** `QLoggingCategory` + `qInstallMessageHandler`
  is the natural fit for `ui`/`app`, but **`core/` must not depend on Qt** (a hard
  invariant — see `spec/engineering/engineering.md` §Cross-cutting invariants).
  So `core` needs a Qt-free way to emit logs. Likely shape: a tiny logging
  interface/sink in `core` (a function pointer or `std::function` callback,
  category + level + message in `std` types), which `app` wires to Qt's logging at
  composition time. Decide this boundary first; it drives everything else. Log it
  in [`decisions.md`](../../decisions.md) if it rejects real alternatives (e.g.
  pulling in spdlog vs. a hand-rolled facade vs. Qt-only with a core shim).
- **Don't reinvent if Qt suffices.** Qt's category logging already gives named
  categories and per-category rules via `QLoggingCategory::setFilterRules` /
  the `QT_LOGGING_RULES` env var and a `qtlogging.ini`. Lean on it for the
  `ui`/`app` side; the question is only how `core` plugs in.
- **Levels are values, like errors.** Core already returns `Expected<T>` =
  `std::expected<T, GitError>`; logging is the *diagnostic* channel alongside that
  error-as-value channel, not a replacement. An `error`-level log usually pairs
  with a returned `GitError`, not an exception.
- **How is the level controlled at runtime?** Env var (`QT_LOGGING_RULES`-style)
  is the cheap dev path. A persisted setting + an in-app control (so an end user
  can flip on verbose logging for a bug report) is the product-facing path — scope
  which of these the first cut delivers.
- **Where do logs go?** Console for dev; consider a rotating log file under the
  app data dir for shipped builds so a user can attach it to a report. File sink
  can be a follow-on.
- **Paths & unicode.** When logging paths, honour the path rule — use
  `generic_u8string()`, never `.string()` — and keep core messages in UTF-8
  `std::string`.
- **Scope creep to resist.** Structured/JSON logging, log shipping/telemetry, and
  a log viewer UI are later wishes. First cut: one facility, a handful of
  categories, global + per-category levels, console (and maybe a file) sink,
  reachable from core, ui, and app.

---

**Graduated 2026-06-21.**

- Designed in: [`spec/engineering` §Logging & diagnostics](../../spec/engineering/engineering.md#logging--diagnostics)
  — the Qt-free `core` facade (`gittide::logf` over a `LogBackend`) bridged onto
  Qt's `QLoggingCategory` at composition time, one taxonomy + one `QT_LOGGING_RULES`
  set across `core`/`ui`/`app`, QML logging through a `log` context property.
- The core/no-Qt boundary resolved as a hand-rolled facade bridged onto Qt
  categories → [D26](../../decisions.md); env-var-only control with console +
  rotating-file sinks → [D27](../../decisions.md).
- Realised by: [Plan 10 — Logging facility](../../plans/2026-06-21-plan10-logging.md).
