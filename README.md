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
- libgit2, QCoro, Catch2, and **KSyntaxHighlighting** (KDE Frameworks) are fetched
  automatically by CMake (FetchContent); no manual install needed.
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

## Packaging

**macOS — a self-contained `GitTide.app`.** A normal build produces a launchable
`build/app/GitTide.app`: the `deploy_macos` target is part of `ALL`, so
`cmake --build build` also copies the Qt frameworks and QML plugins into the
bundle with `macdeployqt`, rewrites the residual absolute Qt references to
`@rpath`, and ad-hoc codesigns the result — see
[`packaging/macos/macdeploy.py`](packaging/macos/macdeploy.py). This is required
for the app to launch at all: a bare build links the executable against Homebrew
Qt, which then clashes with the bundled frameworks and fails to load the `cocoa`
platform plugin.

The deploy is gated behind a stamp file, so it only re-runs when the executable
has actually been relinked — no-op and test-only builds stay fast and do not pay
the ~90 MB Qt copy. (icon + `Info.plist` are baked into the bundle by the build.)
The bundle is portable (no Homebrew Qt needed at runtime); drag it into
`/Applications` — or `cmake --install build --component gittide --prefix
/Applications`. It is ad-hoc signed, not notarised, so on another Mac the first
launch needs right-click → **Open** (or *System Settings → Privacy & Security*).

**Linux** installs a `.desktop` entry and icon via the same `gittide` install
component: `cmake --install build --component gittide`.

> Always pass `--component gittide`. A bare `cmake --install build` also runs the
> bundled dependencies' own install rules (libgit2/QCoro/QtKeychain dev files,
> and a KSyntaxHighlighting step that errors out) — the `gittide` component
> installs *only* the app.

## Credits

GitTide's interaction model is inspired by
[**GitHub Desktop**](https://github.com/desktop/desktop) — its approachable,
focused take on a graphical git client. GitTide is an independent project and is
not affiliated with or endorsed by GitHub.
