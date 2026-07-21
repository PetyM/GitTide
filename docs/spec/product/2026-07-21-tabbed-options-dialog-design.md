# Design — Tabbed Options dialog + folded-in Git identity

**Status:** shipped
**Date:** 2026-07-21
**Shipped:** 2026-07-21 (Plan 40)
**Realises:** an in-session wish to make the Options dialog a proper multi-tab
settings surface, fold Git identity/credentials into it as its own tab (no more
secondary dialog), drop the now-redundant **View** app menu, and seed the
Identity tab from the existing git config so it is not empty for users who
already have `user.name`/`user.email` set.

## Problem

Settings are scattered across three surfaces:

- **`OptionsDialog.qml`** — a single flat panel with Theme radios, Pull-default
  radios, and a **"Manage identities…"** button that opens a *second* dialog.
- **`IdentityDialog.qml`** ("Credentials") — a large separate dialog holding git
  Identities, Host accounts (forge tokens), and SSH keys. Its only entry point is
  the Options button above (verified: no other caller).
- **`AppMenuBar.qml` → View** — a menu whose *only* content is a Theme submenu
  that duplicates the Options theme control.

This means two nested dialogs for one settings flow, and a whole app-menu button
carrying one redundant control.

Goal: one **tabbed** Options dialog that owns everything; delete the secondary
Credentials dialog and the View menu.

## What already exists (reused, not rebuilt)

- **Tab visuals.** `WorkingPane.qml` defines a flat underline-active tab as a
  local `component MainTab: TabButton { … }` (active = `textPrimary` demibold over
  a 2px `accent` underline; inactive = `textSecondary`; hover tints the row).
- **All credential logic.** `credentialManager` + `identityModel` / `hostModel` /
  `sshKeyModel` context properties, plus `IdentityDialog`'s assignment logic
  (`repoOverrideId` / `projectDefaultId` refresh, `credentialManager.changed` and
  `repoVm.changed` Connections, `hostValidated` handling). This moves verbatim.
- **`AppDialog`** modal base (themed header + ✕), `AppRadioButton`, `AppButton`,
  `AppComboBox`, `AppScrollBar` — unchanged.
- **`appSettings`** store (`themeMode`, `pullRebase`) — unchanged.

## Design

### 1. Shared tab primitive — `AppTabButton.qml` (new)

Extract `WorkingPane`'s inline `MainTab` into a standalone
`ui/qml/AppTabButton.qml` (a themed `TabButton`). `WorkingPane` replaces its
three `MainTab { … }` uses with `AppTabButton { … }` and drops the local
`component` definition — **pure refactor, no visual change**. The Options dialog
reuses the same primitive so both tab strips look identical.

### 2. Options dialog becomes a tabbed shell — `OptionsDialog.qml`

Shell = themed `TabBar` (top, `objectName: "optionsTabBar"`, built from
`AppTabButton`) + a `StackLayout` bound to `tabBar.currentIndex` + the existing
Close footer. Opens on tab index 0 (**Appearance**) every time — no last-tab
persistence (YAGNI). Width grows from 360 → ~560; each tab body scrolls
independently (`Flickable` + `AppScrollBar`) when taller than the dialog.

Drops the `identityRequested` signal and the "Manage identities…" button.
Keeps `required property var appSettings` and `objectName: "optionsDialog"`.

Four tab bodies, each its own focused component file:

| Tab | New file | Content (source) |
|-----|----------|------------------|
| **Appearance** | `OptionsAppearanceTab.qml` | Theme radios System/Dark/Light — from current OptionsDialog. |
| **Git** | `OptionsGitTab.qml` | Pull-default radios Merge/Rebase — from current OptionsDialog. |
| **Identity** | `OptionsIdentityTab.qml` | Git identities list + add row + Global/Project/Repo assignment — from IdentityDialog "Identities" section, incl. `refreshAssignments()`, the `repoOverrideId`/`projectDefaultId` properties, and the `credentialManager`/`repoVm` Connections. |
| **Accounts** | `OptionsAccountsTab.qml` | Host accounts (forge tokens) + SSH keys — from IdentityDialog, incl. `statusText` and `hostValidated` handling. |

Each tab receives `appSettings` where needed; Identity/Accounts read the same
global context properties (`credentialManager`, `identityModel`, `hostModel`,
`sshKeyModel`, `repoVm`, `projectController`) they already used inside
IdentityDialog. Assignment/validation state lives inside its owning tab, not on
the dialog root, so each tab stays self-contained and independently testable.

**Object names preserved** so existing interaction tests keep passing with only a
changed parent: `identityList`, `identityName`, `identityEmail`, `identityAdd`,
`hostList`, `hostName`, `hostToken`, `hostAdd`, `sshKeyList`, `sshAdd`,
`optionsCloseButton`.

### 3. Delete `IdentityDialog.qml`

Remove the file and its `qml.qrc` entry. In `Main.qml`: delete the
`IdentityDialog { id: identityDialog }` instance and the
`onIdentityRequested: identityDialog.openDialog()` handler on `optionsDialog`.

### 4. Remove the View menu — `AppMenuBar.qml`

Delete `menuBtnView`, its `menuView` AppMenu, and the three theme items. Bar
drops to three buttons: **File / Edit / Repository**. Theme is reached only via
Options → Appearance. Update the header comment (no longer "File / Edit / View /
Repository").

### 5. Seed Identity from git config on first run

**Symptom:** a fresh install shows an empty Identity tab even when the user has a
perfectly good global git identity — `credentials.json` starts with zero
identities and nothing bridges the gap.

**Core — new repo-independent reader.** `GitRepo::effectiveIdentity()` needs an
open repo. Add a `static Expected<ConfigIdentity> GitRepo::globalIdentity()` that
opens the default config with **no repo** (`git_config_open_default()`, which
merges global/system/xdg) and reads `user.name` / `user.email`. Missing keys →
empty strings (not an error), matching `readEffectiveIdentity`'s tolerant style.
This is the read-side counterpart to the existing static
`GitRepo::setGlobalIdentity(name, email)`.

**UI — one-time seed in `CredentialManager`.** After the store is loaded in the
constructor, if `m_store->identities().empty()`, call `GitRepo::globalIdentity()`;
when it returns a non-empty **name and email**, `addIdentity(name, email)` and
`setGlobalIdentity(newId)` so the seeded identity is marked Global. Guarded on
`identities().empty()` ⇒ genuinely one-time — it never resurrects an identity the
user later deletes, and never runs once they have any identity of their own. No
git config is *written* by the seed beyond the normal `setGlobalIdentity`
pointer (which already targets `~/.gitconfig` and is idempotent for the same
name/email).

## Testing (TDD — failing test first)

- **`test_qml_menu_bar.cpp`** — rename/adjust
  `app_menu_bar_exposes_four_buttons_each_with_a_menu` → three buttons
  (`menuBtnFile`, `menuBtnEdit`, `menuBtnRepository`); assert `menuBtnView` is
  **absent**.
- **`test_qml_shell.cpp`** — extend `options_and_about_dialogs_exist` (or a new
  case): `optionsDialog` still found; new `optionsTabBar` present; tab contents
  `identityList` / `hostList` / `sshKeyList` reachable **inside** `optionsDialog`;
  `identityDialog` and `manageIdentitiesButton` **absent**.
- Existing identity/host/ssh interaction tests (if any beyond the above) continue
  to resolve their controls by the preserved object names.
- **Core** — `GitRepo::globalIdentity()`: in a `TempRepo`/temp-HOME with a global
  config carrying `user.name`/`user.email`, returns them; with the keys unset,
  returns empty strings and no error.
- **UI** — `CredentialManager` seed: constructed against an empty store with a
  global git identity present ⇒ exactly one identity exists afterwards and it is
  the global one; constructed against a **non-empty** store ⇒ no seed occurs.

## Non-goals

- No change to credential *behaviour* (identities, tokens, SSH, keychain,
  assignment semantics) — pure relocation, except the one-time first-run seed.
- No new settings, no per-tab persistence, no keyboard tab shortcuts in the
  dialog.
- Core change is limited to the additive `GitRepo::globalIdentity()` reader; no
  existing core behaviour changes.

## Spec touch-ups (same change)

- App-menu section of the spec (§7 / `docs/spec/product`) — drop View from the
  menu list.
- Wherever the spec documents the Options/Credentials flow — reflect the single
  tabbed dialog and the removed secondary dialog.
