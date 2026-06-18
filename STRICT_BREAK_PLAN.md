# Strict Break Enforcement — Feature Plan

## Problem

When a break overlay is shown, the user can press Alt+Tab (or Super, or click
another window) and keep working anyway. Today's overlay is a best-effort nudge:
it grabs focus, refuses to close, and re-presents itself, but on Wayland under
Mutter an ordinary client window **cannot grab global input or block the
compositor's own keybindings**. Alt+Tab, Super, and the overview are handled
inside `gnome-shell` before our window ever sees the events. This is a
deliberate security boundary, not something app-side C can work around.

## Approach: companion GNOME Shell extension

The only robust way to enforce a break on GNOME/Wayland is code that runs
**inside the compositor process**. We ship a small companion GNOME Shell
extension that performs the lockdown, while the existing `respite --daemon`
process stays the brain.

### Principle: daemon-as-brain, extension-as-actuator

The daemon keeps all timer logic (`RespiteTimer` remains the single source of
truth). The extension is a dumb actuator — it locks/unlocks on command and
reports back. We do **not** move countdown/timer state into JS, because
extensions get disabled on lock, reloaded on GNOME version bumps, and killed on
crashes; that is too volatile a home for the timer.

```
┌─────────────────────────┐         D-Bus            ┌──────────────────────────┐
│  respite --daemon (C)   │  ───────────────────────▶│  GNOME Shell extension   │
│  RespiteTimer = truth   │   StartBreak(seconds)    │  (runs in gnome-shell     │
│  break-started/ended    │   UpdateRemaining(secs)  │   compositor process)     │
│  owns fallback overlay  │   EndBreak()             │  Main.pushModal + actor   │
└─────────────────────────┘  ◀───────────────────────└──────────────────────────┘
                                signals: Released,
                                EscapeRequested
```

### What the extension can do that the client cannot

1. **`Main.pushModal(actor, params)`** — the real lever. It puts the shell into
   a modal grab: keyboard and pointer events route to our actor, and Alt+Tab,
   Super, the overview, the hot corner, and app switching are all suppressed
   because the shell will not process its own keybindings while a modal grab is
   active. `popModal()` releases it. This is the mechanism the screen-shield /
   lock dialog uses.
2. **A full-screen `St.Widget` / `Clutter.Actor`** added to
   `Main.layoutManager` above everything (including the top panel), drawn in the
   compositor's own layer — no client window that can be lowered or tabbed past.
3. **Optionally inhibit specific keybindings** as defense in depth, though a
   modal grab usually makes this unnecessary.

This is the difference from today's `gtk_window_present` re-assertion: there is
no escape because the events never reach the rest of the shell.

## D-Bus contract

The extension owns a well-known name on the **session bus**; the daemon consumes
it.

```
Bus name:     io.github.pawanrai9999.respite.Strict   (owned by the extension)
Object path:  /io/github/pawanrai9999/respite/Strict
Interface:    io.github.pawanrai9999.respite.Strict

Methods:
  StartBreak(u seconds)        → begin lockdown, show blackout + countdown
  UpdateRemaining(u seconds)   → refresh the countdown text
  EndBreak()                   → popModal + remove actor
  Ping() → (s version)         → presence + capability/version handshake

Signals:
  Released()          → emitted if the modal grab is lost for any reason, so the
                        daemon knows enforcement dropped and can fall back to the
                        GTK overlay or log it.
  EscapeRequested()   → user invoked the emergency escape hatch.
```

The extension owns the name via `Gio.DBusExportedObject.wrapJSObject` +
`Gio.bus_own_name`. A custom bus name (rather than poking
`org.gnome.Shell.Extensions`) gives a clean capability handshake and lets the
daemon behave identically regardless of how the helper is implemented.

## App-side integration

New file `src/respite-strict.c`, mirroring `src/respite-background.c`'s raw-GDBus
style:

- `respite_strict_start(seconds)`, `respite_strict_update(seconds)`,
  `respite_strict_end()` — fire-and-forget `g_dbus_connection_call`.
- Presence is tracked with **`g_bus_watch_name`** on the Strict bus name (not a
  one-shot `Ping`), so `name-appeared` / `name-vanished` drive the toggle's
  sensitivity live.
- Subscribe to `Released` / `EscapeRequested`.

Wiring in `src/respite-application.c` (existing integration points):

- `respite_application_on_break_started` (≈ line 291): if strict mode is on and
  the helper owns the name, call `StartBreak` instead of `show_overlays`; keep
  the GTK overlay as a fallback only.
- the `::tick` handler (≈ line 311, where `respite_overlay_set_remaining` is
  already called): also call `respite_strict_update`.
- `respite_application_on_break_ended` (≈ line 304): call `respite_strict_end`.

New GSettings key `strict-break` (boolean) in
`data/io.github.pawanrai9999.respite.gschema.xml`, surfaced as a preferences
toggle.

## UX decisions (settled)

### Fallback when the extension is absent: disable the toggle + warn

The `strict-break` toggle is **insensitive (greyed) unless the helper owns its
bus name**. Below it, an inline status row reads e.g. *"Requires the Respite
GNOME Shell extension — install it from extensions.gnome.org"* with a link. This
makes the dependency honest and prevents enabling "strict" while still being able
to Alt+Tab out.

- `RespiteWindow` already borrows the shared timer on `notify::application` and
  mirrors state; extend that pattern so the window binds the toggle's
  `sensitive` and the warning row's visibility to strict-availability.
- `g_bus_watch_name` callbacks live-update sensitivity: install/enable the
  extension while preferences are open → toggle enables; disable it → greys out.
- Defensive guard: if the extension vanishes **during** an active strict break
  (crash / disable / GNOME reload), `name-vanished` + the `Released` signal make
  the daemon fall back to the GTK overlay for the remainder of that break rather
  than dropping all enforcement silently.

### Escape hatch: hold Esc ~3s

Lives entirely in the extension (it owns the modal grab and sees the key
events):

- Start a 3s timer on Esc press, cancel on release. On fire, emit
  `EscapeRequested` and `popModal`.
- The daemon receives `EscapeRequested` and treats it as an early break-end via
  `RespiteTimer`, so timer state stays consistent.
- No on-screen hint (keeps enforcement strong), but **documented prominently** in
  the README and the extension's extensions.gnome.org description — both because
  reviewers expect a documented exit and because an undiscoverable escape is
  functionally no escape in a real emergency.

## Caveats / risks

- **Version coupling.** `metadata.json`'s `shell-version` list and the
  `pushModal` / actor APIs are the maintenance tax; expect to revisit on each
  major GNOME bump (currently targeting SDK 49/50). This is the single biggest
  ongoing cost.
- **`pushModal` can return false** if another modal grab already holds the shell
  when a break starts. The extension must report this (failed `StartBreak` /
  immediate `Released`) → daemon shows the GTK overlay for that break. So the
  fallback path still has to exist; it is just not user-selectable.
- **Lock screen.** Normal-mode extensions are disabled while the session is
  locked; decide and test what "strict" means across a lock (likely: let the
  lock screen take over).
- **Not unbreakable.** A TTY (Ctrl+Alt+F2/F3) or killing `gnome-shell` always
  exits. This is unavoidable and arguably correct — do not market it as a kiosk
  lock.
- **Pause/Resume.** `app.toggle-timer` pausing during a strict break must tear
  down the grab cleanly (route pause-during-break through `EndBreak`), or the
  user is trapped with a frozen countdown.
- **Multi-monitor is simpler here.** One stage-sized actor covers all monitors
  and the shell handles monitor changes, so the per-monitor overlay
  reconciliation (`respite-application.c` ≈ lines 99–167) is not needed on the
  strict path.

## Distribution

- The extension is a **separate artifact** with its own lifecycle, shipped via
  **extensions.gnome.org** (manual review) or bundled in the repo for manual
  install. It **cannot** live inside the Flatpak — sandboxed apps cannot install
  Shell extensions into the live session.
- The Flatpak'd daemon talks to it purely over the session bus, which already
  works across the sandbox boundary (same as the existing Background portal
  calls).
- Respite is already self-distributed (Flathub rejected it on AI policy), so a
  "download the extension here" step fits the existing model; document the
  friction honestly.

## Build-out order

1. **Spec the D-Bus interface** (above) + a throwaway minimal extension that
   just `log()`s on each method. Confirm the daemon ↔ extension round-trip works
   across the sandbox. (Cheapest way to de-risk.)
2. **Extension MVP**: `pushModal` + black full-screen actor + centered countdown
   label + `EndBreak` / `popModal`. Hard-coded, no settings.
3. **Daemon side**: `src/respite-strict.c`, the `strict-break` GSetting, wire
   into the three break hooks with GTK-overlay fallback.
4. **Escape hatch** (hold Esc 3s) + `Released` handling + `Ping`
   capability/version negotiation + toggle gating via `g_bus_watch_name`.
5. **Polish**: countdown styling, pause/lock-screen interaction, README +
   extension listing docs.
