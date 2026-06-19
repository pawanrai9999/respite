# Respite

**Respite** is a break-reminder application for the GNOME desktop. It helps you
protect your eyes, posture, and focus by enforcing regular breaks while you work.
You tell Respite how long you want to work and how long each break should last,
and it takes care of the rest — gently locking you out of your screen when it's
time to rest.

> Work hard, then give yourself a *respite*.

---

## The Idea

Sitting in front of a screen for hours on end is hard on the body and the mind.
The common advice — take a short break every 20–30 minutes — is easy to ignore
when you're absorbed in your work. Respite removes the temptation to skip breaks
by making them part of the environment rather than a suggestion you can dismiss
without thinking.

The concept is simple:

1. The user sets a **work interval** (e.g. 25 minutes) and a **break duration**
   (e.g. 5 minutes).
2. Respite runs quietly in the background and counts down the work interval.
3. When the interval elapses, Respite **takes over the entire screen**, painting
   it black with a centered countdown message:

   ```
   Break for 05:00
   ```

4. The countdown ticks down second by second (`05:00`, `04:59`, … `00:00`).
5. When the break is over, the overlay disappears and the desktop returns to
   normal. The work interval starts again, and the cycle repeats.

### Example

If the user sets the interval to **25 minutes** and the break duration to **5
minutes**, Respite will remind the user every 25 minutes to take a break, then
black out the screen for 5 minutes before returning to normal.

---

## Behavior in Detail

### Break overlay
- When a break begins, Respite covers **every connected monitor** with a black,
  full-screen overlay.
- The overlay shows the text **`Break for MM:SS`** centered on screen, where the
  time counts down to zero.
- Monitors plugged in or unplugged during a break are reconciled live, so a
  newly connected display is covered immediately.
- The overlay is intended to be hard to ignore so that the break actually
  happens, while still respecting the user's ability to handle the occasional
  emergency (see *Postpone* below).

### Pre-break warning & postpone (limited)
- A configurable number of seconds before a break, Respite raises a **warning
  notification** with a **Postpone** button so a break never arrives without
  notice.
- Breaks are not infinitely dismissible. Respite allows a **limited number of
  postponements** per cycle so that a user who is in the middle of something
  urgent can delay a break — but cannot avoid breaks indefinitely. Each
  postponement pushes the break back by a configurable amount.
- Once the allowance for a cycle is used up, the break proceeds and must be
  taken. The allowance resets at the start of each new work interval.

### Status & pause
- The settings window shows a live **Status** panel: the current phase
  (working, break starting soon, on a break, or paused), a countdown to the next
  transition, and how many postponements remain this cycle.
- A **Pause / Resume** control stops and restarts the work/break cycle without
  quitting the daemon.

### Background operation
- Respite is designed to **run as a background daemon** and to **start
  automatically on login**, so the work/break cycle continues across the session
  without the user needing to keep a window open.
- A small front-end (built with **GTK4 + libadwaita**) is used to configure the
  work interval, break duration, pre-break warning, postpone behavior, and the
  start-on-login preference.
- During an active break, Respite holds a session inhibitor so the system does
  not suspend or blank the screen out from under the overlay.

### Wayland note
On Wayland, applications cannot freely grab the whole screen or all input the way
they can under X11, and GNOME's Mutter does not implement `wlr-layer-shell`. The
break overlay is therefore a **best-effort visual nudge**, not an unbreakable
lock: it fullscreens on every monitor, grabs focus, and re-presents itself, but
a determined user can still switch workspace or application from the keyboard.
The point is to make skipping a break a deliberate act rather than a reflex.

### Strict break enforcement (optional)
For when a best-effort nudge isn't enough, Respite ships an optional **companion
GNOME Shell extension** that enforces breaks from *inside* the compositor — the
one place on Wayland that can actually block Alt+Tab, Super, the overview, and
app switching. The daemon stays the brain (all timer logic lives in Respite);
the extension is a dumb actuator that locks and unlocks on command over D-Bus.

- Turn it on with **Strict Enforcement** in the settings window. The toggle is
  greyed out with an install prompt unless the extension is installed and
  enabled, so you can't switch on "strict" while still being able to work around
  a break.
- During a strict break, the screen blacks out and input is held by the shell;
  Alt+Tab and friends do nothing.
- **Emergency escape hatch: hold `Esc` for about 3 seconds** to end a strict
  break early. There is no on-screen hint (that would weaken enforcement), but
  the exit always exists.
- It can't trap you: if the daemon stops driving the break for any reason, the
  extension auto-releases the lock within ~10 seconds.
- This is **not** a kiosk lock. Switching to a TTY (Ctrl+Alt+F2) or killing
  `gnome-shell` always ends a break — unavoidable, and arguably correct.

The extension is a separate artifact (it cannot live inside the Flatpak, since a
sandboxed app cannot install Shell extensions into the live session). See
[`extension/`](extension/) for installation; if it isn't installed, Respite
falls back to the best-effort overlay above.

---

## Technology

- **Language:** C
- **UI toolkit:** GTK4 + libadwaita
- **Display server:** Wayland (GNOME)
- **Build system:** Meson
- **Packaging:** Flatpak (sandboxed; all privileged behavior goes through XDG
  portals)

---

## Status

✅ **Feature-complete.** The timer engine, settings UI, full-screen multi-monitor
overlay, pre-break warning, limited postpone, background daemon, and
start-on-login are all implemented. See [`PLAN.md`](PLAN.md) for the phased
roadmap and what was deliberately left out.

---

## Install

Respite is distributed as a **signed [Flatpak](https://flatpak.org/)** from a
self-hosted repo on GitHub Pages. First make sure you have `flatpak` and the
Flathub remote (the GNOME runtime is pulled from there):

```sh
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

**One-line install** — adds the signed remote and installs the app:

```sh
flatpak install --user https://pawanrai9999.github.io/respite/respite.flatpakref
flatpak run io.github.pawanrai9999.respite
```

**Or** add the repo and install by name:

```sh
flatpak remote-add --if-not-exists respite https://pawanrai9999.github.io/respite/respite.flatpakrepo
flatpak install --user respite io.github.pawanrai9999.respite
```

The repo is GPG-signed, so installs and updates are verified automatically — no
`--no-gpg-verify` needed. Update with `flatpak update io.github.pawanrai9999.respite`.

---

## Building & Running

Respite is a standard Meson project that needs the GTK4 and libadwaita
development files plus the usual GNOME build tooling (meson, ninja,
`glib-compile-schemas`, `desktop-file-validate`, `appstreamcli`).

### With the GNOME toolchain on `PATH`

```sh
meson setup _build
meson compile -C _build
./_build/src/respite          # run the built binary
meson test -C _build          # run the data-file validation tests
meson install -C _build       # optional, installs system-wide
```

The binary takes a single optional flag, `--daemon`, which runs Respite headless
(no window) as the background timer; any other invocation presents or raises the
settings window.

### Inside the GNOME SDK runtime

If meson/GTK are not installed on the host but the `org.gnome.Sdk` Flatpak
runtime is, build against it directly:

```sh
flatpak run --filesystem="$PWD" --command=sh org.gnome.Sdk//50 -c \
  'meson setup _build && meson compile -C _build && meson test -C _build'
```

### Flatpak

```sh
flatpak-builder --user --install _flatpak io.github.pawanrai9999.respite.json
flatpak run io.github.pawanrai9999.respite
```

The manifest (`io.github.pawanrai9999.respite.json`) builds against GNOME Platform/SDK 50.
Its git source points at the local checkout, so a Flatpak build pulls **committed**
code — commit your changes before building.

---

## Development

- **Code style:** GNOME/GTK C style — tab indentation, GNU-style layout with the
  function return type on its own line, Allman braces, and a space before every
  parenthesis. The style is captured in [`.clang-format`](.clang-format) and
  [`.editorconfig`](.editorconfig).
- **Linting / formatting:** check formatting without modifying files, or apply it:

  ```sh
  clang-format --dry-run --Werror src/*.c src/*.h   # check
  clang-format -i src/*.c src/*.h                    # apply
  ```

- **UI** is defined in GtkBuilder `.ui` templates under `src/`, bundled via
  `src/respite.gresource.xml`.
- **Settings** live in the GSettings schema
  `data/io.github.pawanrai9999.respite.gschema.xml`; durations are stored in seconds and
  presented as minutes in the UI.
- **Translations:** wrap user-facing strings in `_()` and add new translatable
  source files to `po/POTFILES.in`.

---

## Roadmap

- [x] Timer engine for the work/break cycle
- [x] Settings UI for interval, break duration, and postpone allowance
- [x] Full-screen black break overlay with live `MM:SS` countdown
- [x] Multi-monitor support (with live hotplug handling)
- [x] Pre-break warning and limited postpone mechanism
- [x] Background daemon + autostart on login
- [x] Persisting user preferences
- [x] Optional strict enforcement via a companion GNOME Shell extension

---

## Contributing

Contributions are welcome. Please open an issue to discuss substantial changes
before submitting a pull request, and keep formatting consistent with the
project's `.clang-format` / `.editorconfig`.

---

## License

Respite is free software, licensed under the **GNU Affero General Public License,
version 3.0 (AGPL-3.0)**. See the [`COPYING`](COPYING) file for the full license
text.

```
Copyright (C) 2026  The Respite contributors

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>.
```
