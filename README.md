# GitTide

A fast, cross-platform desktop git client built around a first-class **Project**.

GitTide groups several repositories into a Project so you can switch contexts
quickly and see them together — an aggregated status dashboard, file/hunk/line
staging, commits, and per-repo history with a commit graph. It runs on macOS,
Windows, and Linux, and is built to never block the UI: repositories are worked
in parallel and large diffs and histories render incrementally.

Built with **C++23**, **Qt 6 Quick/QML**, and **libgit2**.

## AI-first development

GitTide is developed **AI-first**, with [Claude Code](https://claude.com/claude-code).
The design is not an afterthought to the code — it lives as a **living
specification** in [`docs/spec/`](docs/spec/spec.md), features are built through
test-first **plans** in [`docs/plans/`](docs/plans/index.md), and ideas queue up
as [`docs/wishlist/`](docs/wishlist/index.md). Guidance for the AI agent (and any
human reading along) is in [`CLAUDE.md`](CLAUDE.md). The lifecycle is:

```
wishlist/ (wanted)   →   spec/ (designed)   →   plans/ (built)
```

## Build & test

**Prerequisites:**

- A C++23 compiler and **CMake ≥ 3.28**.
- **Qt 6** (Gui, Qml, Quick, QuickControls2, Test, Concurrent, Svg) — from a
  system install or [`aqtinstall`](https://github.com/miurahr/aqtinstall), found
  via `find_package`.
- libgit2, QCoro, and Catch2 are fetched automatically by CMake (FetchContent);
  no manual install needed.
- **TLS + SSH dev libraries** for network transports — OpenSSL and libssh2.
  On Debian/Ubuntu: `sudo apt install libssl-dev libssh2-1-dev`. On macOS they
  ship with the platform (SecureTransport) / Homebrew `libssh2`; on Windows
  HTTPS uses SChannel and SSH needs libssh2.

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build everything (in parallel)
cmake --build build --parallel

# Run the full test suite
ctest --test-dir build --output-on-failure
```

**Targeted runs:**

```bash
# Core (Catch2) cases only / the UI suite only
ctest --test-dir build --output-on-failure -E gittide_ui_tests   # everything except UI
ctest --test-dir build --output-on-failure -R gittide_ui_tests   # the UI suite (one binary)

# A single core test case (catch_discover_tests registers one ctest entry per case)
ctest --test-dir build -R "<part of the test name>" --output-on-failure
# …or run the binary directly with a Catch2 name/tag filter:
./build/tests/gittide_core_tests "<test name or [tag]>"

# The UI tests are one aggregated Qt Test binary, run headless:
QT_QPA_PLATFORM=offscreen ./build/tests/gittide_ui_tests
```

**Options:** build core-only (no Qt) with `-DGITGUI_BUILD_UI=OFF`; disable tests
with `-DGITGUI_BUILD_TESTS=OFF`.

The CI matrix (Linux / macOS / Windows) runs the same configure → build → test
sequence — see [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Credits

GitTide's interaction model is inspired by
[**GitHub Desktop**](https://github.com/desktop/desktop) — its approachable,
focused take on a graphical git client. GitTide is an independent project and is
not affiliated with or endorsed by GitHub.
