# API docs via Doxygen (CMake-driven)

| | |
|--|--|
| **Added** | 2026-06-17 |
| **Status** | `idea` |
| **Touches** | engineering (build, docs tooling) |

## What

A build target that generates an HTML **API reference** from the Doxygen comments
in the code. Configured entirely through **CMake** — a `cmake/doxygen.cmake`
module that sets up the target via `find_package(Doxygen)` — **not** a
hand-maintained `Doxyfile` checked into the repo.

## Why

Doxygen comments are our source of truth for symbol-level documentation (see the
docs strategy in [`spec/spec.md`](../spec/spec.md) and rule `(937)` in
[`code-style.md`](../spec/engineering/code-style.md)). Today that's only a
convention with no output. A generated reference makes the pillar tangible, gives
a browsable API, and can flag undocumented symbols. Keeping the config in CMake
(alongside the FetchContent deps and build options) keeps it consistent with the
rest of the build and avoids a standalone `Doxyfile` drifting out of sync.

## Notes

- Prefer CMake's `doxygen_add_docs()` (it generates the config from variables at
  build time) over a committed `Doxyfile`.
- Live in `cmake/doxygen.cmake`, included from the top-level `CMakeLists.txt`
  behind an option (e.g. `GITTIDE_BUILD_DOCS`, default `OFF`) — no impact on the
  normal build.
- Existing comments are plain `//`, not yet Doxygen-style — rolling this out pairs
  with converting comments as files are touched (`(937)`, conform-on-touch).
- Possible later: `WARN_AS_ERROR` to fail on missing docs, and a CI job to build
  (or publish) the reference.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/engineering (build section) · plan: plans/<file>
-->
