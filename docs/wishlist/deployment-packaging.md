# Deployment — native installers per OS

| | |
|--|--|
| **Added** | 2026-06-22 |
| **Status** | `idea` |
| **Touches** | engineering, product |

## What

Ship GitTide through each platform's native channel:

- **Ubuntu / Debian** — installable via `apt` (a `.deb` package, ideally from a
  hosted APT repository or a PPA so updates flow through the system package
  manager).
- **macOS** — installable via Homebrew (`brew install gittide`, likely a Cask
  pointing at a signed, notarized `.app` / `.dmg`).
- **Windows** — a downloadable `.exe` installer (NSIS / WiX / Inno Setup), so a
  user double-clicks and gets Start-menu entry, file associations, and an
  uninstaller.

## Why

Right now the only way to get GitTide is to build it from source. That gates the
app to developers with the full Qt + libgit2 + CMake toolchain set up. Native
packaging makes it a one-command (or one-click) install for normal users, and on
Linux/macOS it wires GitTide into the system updater so upgrades are automatic.

## Progress

- **macOS self-contained `.app` — done** (2026-07-23). `cmake --build build
  --target deploy_macos` produces a portable, ad-hoc-signed `GitTide.app` you can
  drag to `/Applications` (or `cmake --install build --component gittide --prefix
  /Applications`). See [decision D54](../decisions.md) and
  [engineering → Build & test](../spec/engineering/engineering.md#build--test).
  This is the *building block*; the wish stays open for the rest:
  - **macOS**: an Apple Developer ID + **notarization** (today an unnotarized
    bundle needs right-click → Open on a fresh Mac), a `.dmg`, and the Homebrew
    Cask.
  - **Linux** (`.deb` / apt) and **Windows** (`windeployqt` + `.exe` installer)
    are untouched.
  - A tagged **release CI pipeline** that builds, signs, and publishes.

## Notes (optional)

- CMake already drives the build; **CPack** can generate `.deb`, `.dmg`, and a
  Windows installer (NSIS/WiX) from the same project, which keeps one source of
  truth for the package metadata.
- Qt deployment helpers bundle the runtime: `windeployqt` (Windows),
  `macdeployqt` (macOS); on Linux consider `linuxdeploy` + the Qt plugin for an
  AppImage, or stick to a system-dependency `.deb`.
- **Signing / notarization** is the hard part, not the packaging:
  - macOS needs an Apple Developer ID and notarization or Gatekeeper blocks it.
  - Windows wants an Authenticode certificate or users hit SmartScreen warnings.
  - APT repo / Cask need a hosting + signing-key story (GPG for APT).
- Homebrew Cask vs. formula: a Qt GUI app is a **Cask** (prebuilt binary), not a
  source formula.
- Open question: self-hosted APT repo vs. a PPA vs. just attaching `.deb` to
  GitHub Releases. Releases-only is the cheapest first step; managed repo gives
  auto-updates.
- CI angle: a release pipeline (e.g. GitHub Actions matrix over the three OSes)
  that builds, packages, signs, and publishes on tag would make releases
  repeatable. Could become its own wish.

---

<!-- When this graduates, link out and set Status:
- Designed in: spec/<section> · plan: plans/<file> -->
