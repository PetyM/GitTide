# Plan 10 — Logging facility (categories + levels across all layers)

| | |
|--|--|
| **Date** | 2026-06-21 |
| **Status** | `done` |
| **Spec** | [`spec/engineering/engineering.md` §Logging & diagnostics](../spec/engineering/engineering.md#logging--diagnostics) |
| **Depends on** | — |

**Goal:** Make GitTide observable instead of silent — one categorised,
level-controlled logging facility reachable from `core`, `ui`, and `app`, with
per-category and global verbosity, console + rotating-file sinks.

**Architecture:** A Qt-free facade in `core` (`gittide::logf` over a `LogBackend`
of two `std::function`s) keeps invariant #1 intact; `app` installs a backend that
bridges those records onto Qt's `QLoggingCategory` system, so one taxonomy and one
set of `QT_LOGGING_RULES` govern every layer. QML logs through the same facade via
a `log` context property. See D26 (the facade-vs-library choice) and D27 (env-var
control + console/file sinks).

**Tech stack:** C++23 `std::format`; Qt `QLoggingCategory` / `qInstallMessageHandler`
/ `QFile`; QtTest + Catch2.

## Global constraints

- Must not break **invariant #1** (no Qt in `core/`): `core/log.hpp` and its
  consumers compile with no Qt on the include path.
- New `core/` source → `core/CMakeLists.txt`; new `ui/` source →
  `ui/CMakeLists.txt`; new tests → the matching lists in `tests/CMakeLists.txt`
  (UI test also wired into `tests/ui/main.cpp`).
- Paths in log messages use the `toGitPath()` UTF-8 form (path rule).

---

## Task 1: Core logging facade

**Files:** Create `core/include/gittide/log.hpp`, `core/src/log.cpp`,
`tests/test_log.cpp`; modify `core/CMakeLists.txt`, `tests/CMakeLists.txt`.

**Interfaces:** `enum class LogLevel`; `namespace logcat` name constants;
`struct LogBackend { write; enabled; }`; `setLogBackend`, `logEnabled`,
`logMessage`, and the `logf(level, category, fmt, …)` template.

- [x] **Step 1:** Catch2 test — no-backend is silent + disabled; `logf` routes
      level/category/formatted message; `logf` is gated (disabled record never
      emitted).
- [x] **Step 2:** Implement; backend held behind a mutex-guarded `shared_ptr`
      (Apple libc++ lacks `atomic<shared_ptr>` members), thread-safe for workers.
- [x] **Step 3:** Build + run — 3 cases green.

## Task 2: Qt bridge, sinks, and the QML logger

**Files:** Create `ui/include/gittide/ui/logging.hpp`, `ui/src/logging.cpp`,
`tests/ui/test_logging.cpp`; modify `ui/CMakeLists.txt`, `tests/CMakeLists.txt`,
`tests/ui/main.cpp`, `ui/src/qmlcontext.cpp` + header.

**Interfaces:** six `Q_LOGGING_CATEGORY` (`gittide.git` …); `categoryFor`;
`installCoreLogBridge`; `installMessageHandler(dir, maxBytes)` (rotating file +
stderr); `installLogging(dir)`; `class QmlLog`; `log` context property.

- [x] **Step 1:** QtTest — `categoryFor` mapping; bridge honours
      `setFilterRules`; core `logf` reaches the handler; `QmlLog` flows through the
      same path; file sink writes and rotates past the cap.
- [x] **Step 2:** Implement bridge (routes to `qC*`, gate via `isXEnabled`), the
      handler + `FileSink` (mutex-guarded, `.1` rollover), `QmlLog` → core facade.
- [x] **Step 3:** Build + run — 7 cases green.

## Task 3: Compose in app + instrument representative call sites

**Files:** Modify `app/qml_main.cpp`, `core/src/gitrepo.cpp`,
`core/src/credentialselect.cpp`, `ui/src/repocontroller.cpp`.

- [x] **Step 1:** `app` calls `installLogging(<AppDataLocation>/logs)` and logs
      startup (`app`).
- [x] **Step 2:** Instrument `GitRepo::open` (`git`), `chooseCredential`
      (`auth`), `RepoController::open`/`fetch` (`ui`/`async`).
- [x] **Step 3:** Full build clean (no warnings); suite green.

---

## Outcome

- **Shipped:** a single logging facility spanning `core`/`ui`/`app` with the
  category taxonomy `git`/`repo`/`async`/`auth`/`ui`/`app`, the
  `Trace`/`Debug`/`Info`/`Warning`/`Error` ladder, global + per-category control
  via `QT_LOGGING_RULES`, and console + rotating-file sinks. QML logs through a
  `log` context property. Verbosity example: `QT_LOGGING_RULES="gittide.git.debug=true"`.
- **Spec updated:** [`spec/engineering/engineering.md` §Logging & diagnostics](../spec/engineering/engineering.md#logging--diagnostics);
  decisions D26 (facade) and D27 (control + sinks) in
  [`decisions.md`](../decisions.md).
- **Code:** `core/include/gittide/log.hpp` + `core/src/log.cpp` (Qt-free facade);
  `ui/include/gittide/ui/logging.hpp` + `ui/src/logging.cpp` (Qt bridge, sinks,
  `QmlLog`); wired in `app/qml_main.cpp`; representative call sites instrumented
  in `core/src/gitrepo.cpp`, `core/src/credentialselect.cpp`,
  `ui/src/repocontroller.cpp`. Tests: `tests/test_log.cpp` (3) and
  `tests/ui/test_logging.cpp` (5 + init/cleanup).
