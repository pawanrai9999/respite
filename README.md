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

1. The user sets a **work interval** (e.g. 25 minutes) and a **break time**
   (e.g. 5 minutes).
2. Respite runs quietly in the background and counts down the work interval.
3. When the interval elapses, Respite **takes over the entire screen**, painting
   it black with a centered countdown message:

   ```
   Break for 05:00
   ```

4. The countdown ticks down second by second (`05:00`, `04:59`, … `00:00`).
5. When the break time is over, the overlay disappears and the desktop returns
   to normal. The work interval starts again, and the cycle repeats.

### Example

If the user sets the interval to **25 minutes** and the break time to **5
minutes**, Respite will remind the user every 25 minutes to take a break, then
black out the screen for 5 minutes before returning to normal.

---

## Behavior in Detail

### Break overlay
- When a break begins, Respite covers **every connected monitor** with a black,
  full-screen overlay.
- The overlay shows the text **`Break for MM:SS`** centered on screen, where the
  time counts down to zero.
- The overlay is intended to be hard to ignore so that the break actually
  happens, while still respecting the user's ability to handle the occasional
  emergency (see *Postpone* below).

### Postpone (limited)
- Breaks are not infinitely dismissible. Respite allows a **limited number of
  postponements** so that a user who is in the middle of something urgent can
  delay a break — but cannot avoid breaks indefinitely.
- Once the allowance for a cycle is used up, the break proceeds and must be
  taken.

### Background operation
- Respite is designed to **run as a background daemon** and to **start
  automatically on login**, so the work/break cycle continues across the session
  without the user needing to keep a window open.
- A small front-end (built with **GTK4 + libadwaita**) is used to configure the
  interval, break time, postpone allowance, and autostart preference.

---

## Technology

- **Language:** C
- **UI toolkit:** GTK4 + libadwaita
- **Display server:** Wayland (GNOME)
- **Build system:** Meson (via GNOME Builder)

> This repository currently contains the default **"Hello World" GNOME Builder
> template**. The functionality described above is the planned design and is not
> yet implemented.

### Wayland note

On Wayland, applications cannot freely grab the whole screen or all input the way
they can under X11. The full-screen break overlay and input handling will be
implemented using Wayland-appropriate mechanisms (e.g. layer-shell / fullscreen
surfaces and the relevant GNOME APIs). The exact approach will be documented as
the implementation progresses.

---

## Status

🚧 **Early / scaffolding stage.** This is the initial project skeleton generated
from the GNOME Builder template. Core features (timer engine, configurable
interval and break time, full-screen overlay, limited postpone, autostart
daemon) are planned and under development.

---

## Building

This is a standard GNOME Builder / Meson project.

```sh
# Open in GNOME Builder and press Run, or build from the command line:
meson setup _build
meson compile -C _build
meson install -C _build   # optional
```

*(Exact build dependencies will be listed here as the project matures.)*

---

## Roadmap

- [ ] Timer engine for the work/break cycle
- [ ] Settings UI for interval, break time, and postpone allowance
- [ ] Full-screen black break overlay with live `MM:SS` countdown
- [ ] Multi-monitor support
- [ ] Limited postpone mechanism
- [ ] Background daemon + autostart on login
- [ ] Persisting user preferences

---

## Contributing

Contributions are welcome. Please open an issue to discuss substantial changes
before submitting a pull request.

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
