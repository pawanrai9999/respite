# Respite

A simple, distraction-free break reminder for the GNOME Wayland desktop. Respite enforces healthy work intervals by taking over the screen and prompting you to rest, helping reduce eye strain, repetitive strain injury (RSI), and mental fatigue from long, uninterrupted work sessions.

## Idea

Modern work often pulls people into multi-hour stretches in front of a screen without a natural pause. Respite addresses this by acting as a lightweight, system-level interval timer that cannot be easily ignored: when the configured work interval elapses, it blacks out the screen and forces a visible countdown break, then automatically returns control to the user once the break is over.

### Core Behavior

1. The user configures two values:
   - **Work Interval**: how long to work before a break is triggered (e.g. 25 minutes).
   - **Break Duration**: how long the enforced break lasts (e.g. 5 minutes).
2. Respite runs quietly in the background (as a GNOME Shell-integrated background app / tray-style indicator).
3. When the work interval elapses:
   - The entire screen turns black (a fullscreen, always-on-top overlay window on every connected display).
   - Centered text displays the remaining break time in `MM:SS` format, e.g. `Break for 05:00`, counting down live.
   - Keyboard and mouse input to underlying applications is blocked while the overlay is active.
4. When the countdown reaches `00:00`, the overlay is dismissed automatically and the desktop returns to its normal state.
5. The work interval timer then resets and the cycle repeats.

### Snooze / Skip Behavior

Breaks are enforced by default, but Respite allows limited flexibility so it doesn't feel hostile to real workflows:

- A **Snooze** button on the overlay lets the user postpone an upcoming break by a short, configurable amount (e.g. 5 minutes).
- A **Skip** option lets the user skip the current break entirely and resume the work interval immediately.
- Both Snooze and Skip are **rate-limited** per work interval (e.g. a maximum of 1–2 snoozes per cycle, configurable), so the app retains its purpose of encouraging regular rest rather than becoming trivially dismissible.
- All snooze/skip actions are logged for the optional statistics feature (see Roadmap).

## Why GNOME Wayland?

Wayland's security model intentionally restricts global input grabbing and arbitrary window placement compared to X11. Respite is designed to work within these constraints by using GNOME-native and portal-based APIs (e.g. `xdg-desktop-portal`, GNOME Shell extensions where necessary, and layer-shell-style fullscreen overlays) rather than relying on X11-only hacks, ensuring correct behavior across multi-monitor Wayland sessions.

## Planned Tech Stack

- **Language**: C with Vala for higher-level application logic.
- **UI Toolkit**: GTK4 + libadwaita, following GNOME Human Interface Guidelines (HIG) for a native look and feel.
- **Build System**: Meson + Ninja.
- **Packaging**: Flatpak (primary distribution target via Flathub), with the GNOME Platform runtime.
- **Background Behavior**: Implemented as a GNOME background app / autostart service with a status indicator, using GLib's main loop for timer scheduling.
- **Overlay Rendering**: Fullscreen `GtkWindow` instances per monitor, layered above all other windows.

## Features

### Core (v1)
- Configurable work interval and break duration.
- Fullscreen black overlay with centered countdown text (`Break for MM:SS`).
- Automatic return to desktop when the break ends.
- Limited snooze and skip controls during the break/pre-break phase.

### Roadmap
- **Multi-monitor support**: overlay rendered consistently across all connected displays simultaneously.
- **Pre-break warning**: a brief, non-intrusive notification or screen dimming a configurable number of seconds before a break begins, so users can save work in progress.
- **Sound cues**: optional audio notification when a break starts and ends.
- **Statistics tracking**: history of completed breaks, skipped/snoozed breaks, and total focused work time, viewable in-app (e.g. daily/weekly view).
- **Themes**: customizable overlay appearance (colors, fonts, optional background imagery) beyond the default black screen.
- **Autostart integration**: GNOME autostart entry so Respite launches automatically at login.
- **Per-profile schedules**: different interval/break presets for different times of day or activities (e.g. "Deep Work" vs "Casual").
- **GNOME Shell Quick Settings integration**: pause/resume Respite directly from the system menu.

## Installation

> Not yet available. Planned distribution via Flathub once the initial release is ready.

## Building from Source

> Build instructions will be added once the initial Meson project scaffold is in place.

```sh
git clone https://github.com/<your-username>/respite.git
cd respite
meson setup build
ninja -C build
```

## Contributing

Contributions, bug reports, and feature suggestions are welcome. Please open an issue to discuss significant changes before submitting a pull request.

## License

Respite is licensed under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. See [LICENSE](LICENSE) for the full text.
