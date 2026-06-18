# Respite — Development Plan

Break-reminder application for the GNOME desktop (C + GTK4 + libadwaita, Wayland, Flatpak).

**Current status:** Phases 1 (settings foundation), 2 (timer engine), 3 (process model & daemon), 4 (break overlay), 5 (postpone mechanism), 6 (autostart & background), and 7 (polish & release readiness) complete. The optional idle handling (7.1) was deliberately skipped as unreliable on sandboxed GNOME; the remaining 7.2–7.6 items are done. A post-7 polish item adds an in-app **Status** panel and **Pause/Resume** control (see below). Release tooling — `.editorconfig` and `.clang-format` capturing the GTK C style — is in place.

## Architecture Decisions

These choices drive every phase below:

- **Overlay:** best-effort fullscreen GTK4 windows (one per monitor), pure C/GTK. Mutter does not implement `wlr-layer-shell`, so the overlay is treated as a strong visual nudge, **not** an unbreakable lock — the user can still switch workspace/app via the keyboard on Wayland. We make it visually total and re-assert focus; nothing more.
- **Process model:** a single `respite` binary. `--daemon` runs headless (`g_application_hold`, no window) with the timer running; default activation presents the settings window. Single-instance via GApplication; preferences synced between daemon and GUI through GSettings live-change notifications.
- **Autostart:** XDG autostart `.desktop` entry, requested through the Background portal under Flatpak.
- **Packaging:** Flatpak (sandboxed). All privileged behavior (background, autostart, inhibit, idle) goes through XDG portals.

### Core structure

- One GObject `RespiteTimer` core that knows nothing about UI and emits signals.
- `RespiteApplication` runs the timer headless in `--daemon` mode, or presents the settings window when activated normally.
- All durations/preferences live in GSettings; UI and daemon both bind to it and react to live changes.
- Overlay = transient fullscreen windows created on break-start, destroyed on break-end.

### Conventions / defaults

- Durations are stored in **seconds** in GSettings; the UI presents minutes where sensible.
- Live settings changes apply to the **next** cycle, not the one in progress.
- App ID / namespace: `io.github.pawanrai9999.respite`; GResource base path `/io/github/pawanrai9999/respite`; C symbols use the `respite_` / `RESPITE_` / `Respite` prefixes.

---

## Phase 1 — Settings foundation

- **1.1** Define GSettings keys in `data/io.github.pawanrai9999.respite.gschema.xml`: `work-interval` (uint, sec), `break-duration` (uint, sec), `postpone-allowance` (uint), `postpone-duration` (uint, sec), `pre-break-warning` (uint, sec), `autostart` (bool). Add ranges/defaults.
- **1.2** Verify the schema compiles (`glib-compile-schemas`) and is reachable at runtime.
- **1.3** Replace the template "Hello World" content in `respite-window.ui` with an Adwaita preferences layout (one row per setting).
- **1.4** Wire each control two-way to GSettings via `g_settings_bind`.
- **1.5** Enforce input constraints in the UI (min/max, spin adjustments) matching the schema ranges.

## Phase 2 — Timer engine (headless core)

- **2.1** Create `RespiteTimer` GObject with an explicit state enum: `IDLE → WORKING → (WARNING) → BREAK → WORKING`.
- **2.2** Implement the work countdown using `g_get_monotonic_time` + a 1 s `g_timeout` (monotonic so it is drift/sleep-correct).
- **2.3** Implement the break countdown and the WORKING↔BREAK transitions.
- **2.4** Emit signals: `tick(remaining)`, `state-changed(state)`, `break-started`, `break-ended`.
- **2.5** Read durations from GSettings and react to live changes (apply only to the *next* cycle, not the one in progress).
- **2.6** Add a temporary `--debug-timer` console mode that logs state/ticks, to validate the engine before any overlay exists.

## Phase 3 — Process model & daemon

- **3.1** Add `--daemon` command-line handling on the GApplication (keep default activate behavior).
- **3.2** In daemon mode: `g_application_hold`, no window, instantiate and start `RespiteTimer`.
- **3.3** Default activation presents the settings window; a second launch of the daemon-held instance just raises the window (single-instance).
- **3.4** Ensure one shared timer instance regardless of how the process was started.
- **3.5** Clean lifecycle: the quit action releases the hold and tears down the timer.

## Phase 4 — Break overlay (Wayland best-effort)

- **4.1** Build a single fullscreen black `RespiteOverlay` window with a centered `Break for MM:SS` label.
- **4.2** Bind the label to the timer's `tick` signal; format MM:SS.
- **4.3** Show the overlay on `break-started`, destroy it on `break-ended`.
- **4.4** Multi-monitor: enumerate `gdk_display_get_monitors`, one overlay per monitor, fullscreened on each.
- **4.5** Handle monitor hotplug during a break (monitors-changed → add/remove overlays).
- **4.6** Styling via GResource CSS: true black, large legible countdown font.
- **4.7** Best-effort focus assertion: fullscreen + re-present/grab focus on the overlay; document explicitly that escape is possible on Wayland.

## Phase 5 — Postpone mechanism

- **5.1** Track `postpones_remaining` per cycle inside `RespiteTimer`, seeded from `postpone-allowance`.
- **5.2** Pre-break warning: at `pre-break-warning` seconds, emit a `warning` signal and show a notification offering "Postpone".
- **5.3** Implement `respite_timer_postpone()` — if allowance remains, delay the break by `postpone-duration` and decrement; otherwise no-op.
- **5.4** Add a Postpone button (on the warning notification and/or overlay) wired to that call; reflect the remaining count.
- **5.5** Reset `postpones_remaining` at the start of each new WORKING cycle.

## Phase 6 — Autostart & background (Flatpak portals)

- **6.1** Add the required `finish-args` to the Flatpak manifest for portal access (background, notifications, inhibit); confirm Wayland/dri are already present.
- **6.2** Implement the Background portal request (run-in-background) on daemon startup.
- **6.3** Implement autostart via the Background portal `RequestAutostart`, launching with `--daemon`.
- **6.4** Wire the `autostart` GSettings key ↔ portal autostart state (toggle in the settings UI).
- **6.5** Native fallback: install `~/.config/autostart/io.github.pawanrai9999.respite.desktop` when not sandboxed.

## Phase 7 — Polish & release readiness

- **7.1** *(Optional — skipped)* Idle handling: pause/restart the work cycle when the session is idle/locked (via the Inhibit/idle portal where available; degrade gracefully if not). Flagged optional because sandboxed idle detection on GNOME is unreliable; left unimplemented.
- **7.2** ✅ Inhibit suspend/screensaver during an active break so the overlay is not interrupted — `gtk_application_inhibit()` held across each break in `RespiteApplication`.
- **7.3** ✅ Finalize the About dialog, app name/icon, and `data/io.github.pawanrai9999.respite.metainfo.xml` (AppStream) — template placeholders replaced; desktop entry keywords/comment added.
- **7.4** ✅ Update `po/POTFILES.in`, wrap all user-facing strings in `_()`, regenerate the translation template — `shortcuts-dialog.ui` added to POTFILES; `po/respite.pot` regenerated.
- **7.5** ✅ Remove the `--debug-timer` scaffolding (or hide it behind a build option) — removed from `main.c`.
- **7.6** ✅ Final Flatpak build + manual smoke test of the full work → warn → break → postpone → autostart loop — manifest fixed to build (runtime 50, correct git source) and verified via meson build + validation tests under the SDK runtime; interactive on-session smoke test left to the maintainer (no flatpak-builder CLI / display in CI).

## Post-Phase 7 — Polish

- **P.1** ✅ In-app **Status** panel: the settings window mirrors the shared `RespiteTimer` live — phase headline + icon, a countdown subtitle to the next transition, and a postpones-remaining row shown only while a postpone could still be spent.
- **P.2** ✅ **Pause/Resume** control (`app.toggle-timer`): one button stops or restarts the work/break cycle. `RespiteApplication` keeps the daemon hold across a pause so a paused process stays alive and ready to resume, releasing it only on quit.
- **P.3** ✅ Release tooling: `.editorconfig` and `.clang-format` capture the GTK/GNOME C style for contributors; README rewritten from the old template stub to document the finished app, build/run, and the formatting workflow.
