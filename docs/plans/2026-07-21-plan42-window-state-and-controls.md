# Plan 42 — Window state restore + titlebar control glyphs

> **For agentic workers:** implement this plan task-by-task, test-first. Each
> task's steps use checkbox (`- [ ]`) syntax for tracking; tick them as you go.

| | |
|--|--|
| **Date** | 2026-07-21 |
| **Status** | `done` |
| **Spec** | [`spec/product/app-menu.md` §1 (settings), §2 (frameless window + TitleBar)](../spec/product/app-menu.md) |
| **Depends on** | Plan 29 (menu bar), Plan 35 (macOS native chrome) |

**Goal:** Make the app reopen at its last window size / maximised state (today a
saved-maximised window relaunches small), and replace the heavy/odd unicode
window-control glyphs (`⬜`/`❐`) with light, consistent ones.

**Architecture:** Both changes are QML-only, in `ui/qml/Main.qml` and
`ui/qml/TitleBar.qml`. No C++, no core, no new sources. The `Settings` store and
its geometry keys already exist and persist correctly — the defect is *when* the
maximise is applied, not *whether* it is saved.

**Tech stack:** Qt Quick / QML (`ApplicationWindow`, `QtCore.Settings`,
`Qt.callLater`, `Window.Visibility`).

## Global constraints

- Invariants in [`spec/engineering/engineering.md`](../spec/engineering/engineering.md):
  no Qt in `core/` (untouched here); colour from theme tokens only.
- No new sources or tests files: any assertions extend the existing headless UI
  target `gittide_ui_tests` (`tests/ui/test_qml_shell.cpp`). No `CMakeLists.txt`
  change.
- Must keep passing: existing `objectName`s `titleBar`, `appWindow`,
  `appIconButton`; the double-click maximise/restore `TapHandler`; the seven
  `EdgeResizer` zones gated on `visibility !== Window.Maximized`.
- macOS chrome is **out of scope** — do not touch the `isMac` traffic-light
  branch (glyphs `✕ ─ ❐/+`) in `TitleBar.qml` or `WindowButton.qml`'s `macOs`
  path.

**Environment note:** the reported failure is on **X11 + GNOME/Mutter** with the
frameless flag. `showMaximized()` called synchronously in
`Component.onCompleted` runs before Mutter maps the frameless window, so the
`_NET_WM_STATE_MAXIMIZED` request is dropped — Qt still reports
`visibility === Window.Maximized` (edge-resizers stay off, glyph shows restore),
but the window renders at the default 1100×720. Because the window was thus never
truly in the `Windowed` state, `onWidthChanged`/`onHeightChanged` (guarded by
`visibility === Window.Windowed`) never captured a real windowed size, so
`windowWidth`/`windowHeight` sit at their defaults.

---

## Task 1: Restore maximised state reliably on startup

**Files:** Modify `ui/qml/Main.qml`. Verify: manual `/run` on X11 with
`console.log` instrumentation into `gittide.log` (WM maximise cannot be asserted
under the `offscreen` headless platform).

**Interfaces:** none new — behavioural only.

> **Root cause (corrected during implementation).** The initial hypothesis
> (frameless + `showMaximized()` mapping-timing on Mutter) was **wrong**.
> Instrumenting the QML showed `onVisibilityChanged` fires with the transient
> `Windowed` state **before** `Component.onCompleted` runs, and its handler
> writes `appSettings.windowVisibility = 2`, clobbering the stored `4` before
> `onCompleted` reads it. `onCompleted` then always saw `2`, took the windowed
> branch, and never maximised. Synchronous `window.showMaximized()` works fine
> once the stored value survives — no `Qt.callLater` needed.

- [x] **Step 1: Reproduce** — with `windowVisibility=4` in
      `~/.config/gittide/gittide.conf`, launched on X11; window opened windowed
      (min 860×560), and the conf was rewritten to `windowVisibility=2`.
- [x] **Step 2: Instrument + find root cause** — added temporary `console.log`
      to `onCompleted`/`onVisibilityChanged`; the log proved the clobber ordering
      above.
- [x] **Step 3: Fix** — added `property bool _restored: false`, set `true` at the
      end of `Component.onCompleted`; gated every geometry-persistence handler
      (`onX/Y/Width/Height/VisibilityChanged`) on `_restored`. Kept the maximise
      branch synchronous (`window.showMaximized()`). Removed the instrumentation.
- [x] **Step 4: Verify** — relaunch with stored `4`: window fills the monitor
      (`1920×1051`), conf stays `4`. Relaunch with stored windowed `1400×900@120,120`:
      restores exactly. `gittide_ui_tests` passes.

## Task 2: Ensure a sane first windowed size after "always maximised"

**Files:** none (verification only).

- [x] **Step 1: Check** — the existing `onWidthChanged`/`onHeightChanged`
      writers (now `_restored`-gated) capture windowed size once the window
      genuinely enters `Windowed`; a first restore-down falls back to the
      `Settings` defaults (1100×720). No gap — no code change.

## Task 3: Lighter titlebar control glyphs (Win/Linux)

**Files:** Modify `ui/qml/TitleBar.qml` (right-side `!isMac` `RowLayout`).
Optionally `ui/qml/WindowButton.qml` (glyph `font.pixelSize` for optical
balance). Verify: `/run` visual check.

**Interfaces:** none new — cosmetic.

- [x] **Step 1: Swap glyphs** — in the single Win/Linux maximise/restore
      `WindowButton`: `? "❐" : "⬜"` → `? "⧉" : "□"` (restore `⧉` U+29C9, maximise
      `□` U+25A1). Minimise `─` and close `✕` unchanged; tooltips and close red
      hover untouched.
- [x] **Step 2: Optical tune** — not needed; `□`/`⧉` read balanced against
      `─`/`✕` at `font.pixelSize: 13`. `WindowButton.qml` left unchanged.
- [x] **Step 3: Verify** — `/run` screenshots confirmed: maximised window shows
      restore `⧉`, windowed shows maximise `□`; both light and consistent.

## Task 4: Close out

- [x] Updated `spec/product/app-menu.md`: §1 documents the `_restored` gate that
      prevents the startup clobber; §2 notes the maximise/windowed restore paths
      and the gate. (ASCII control row already showed `[─] [□] [✕]`.)
- [x] Ticked this plan's boxes, filled **Outcome**, index row set to `done`.

---

## Outcome

- **Shipped:** The window reopens in its last state — a maximised window relaunches
  maximised (previously it always came up small), and a windowed session restores
  its exact saved size/position. The Win/Linux titlebar maximise/restore glyphs are
  lighter and clearer (`□` / `⧉` in place of the emoji-weight `⬜` / `❐`).
- **Root cause:** not a Mutter mapping-timing issue (the plan's initial guess) but a
  **startup clobber** — `onVisibilityChanged` fired with the transient `Windowed`
  state before `Component.onCompleted` read the stored value, overwriting
  `windowVisibility` (4 → 2). Fixed with a `_restored` flag gating all
  geometry-persistence handlers until the initial restore has run. See Task 1.
- **Spec updated:** [`spec/product/app-menu.md`](../spec/product/app-menu.md) §1
  (the `_restored` gate) and §2 (maximise/windowed restore paths).
- **Code:** `ui/qml/Main.qml` (`_restored` gate + synchronous maximise restore),
  `ui/qml/TitleBar.qml` (glyph swap). `WindowButton.qml` unchanged.
