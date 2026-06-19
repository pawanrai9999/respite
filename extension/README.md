# Respite Strict Break — GNOME Shell extension

Companion **actuator** for [Respite](../README.md). Respite's in-app overlay is a
best-effort nudge: on Wayland under Mutter an ordinary client window cannot grab
global input, so Alt+Tab, Super and the overview still work during a break. The
only robust enforcement is code running *inside* `gnome-shell`, which is what
this extension provides.

It is a **dumb actuator**: all timer state stays in the `respite --daemon`
process. The daemon drives this extension over the session bus — it locks and
unlocks on command and reports back. Nothing here keeps time.

## How it works

When the daemon calls `StartBreak`, the extension:

1. takes a shell modal grab (`Main.pushModal`) with `actionMode: NONE`, which
   routes all input to our actor and filters every global keybinding — Alt+Tab,
   Super, the overview, the hot corner and app switching are all suppressed;
2. covers every monitor with one black, stage-sized actor showing a centered
   `Break for MM:SS` countdown.

`EndBreak` releases the grab and removes the actor. See `dbus-interface.xml`
for the full contract — it is the single source of truth shared with the C
daemon (`src/respite-strict.c`).

## Safety: you cannot get trapped

A modal grab is the strongest thing an extension can do to a session, so the
grab is built to never outlive its purpose:

- **Heartbeat watchdog.** The daemon calls `UpdateRemaining` every second while
  a break runs; each call re-arms a 10-second watchdog. If the daemon crashes,
  disconnects, or never follows up a raw `StartBreak`, the watchdog fires and
  releases the grab automatically. Worst case is ~10 seconds of black screen,
  then recovery — no permanent lock.
- **Crash-safe grab.** The grab handle is stored the instant it is taken and the
  whole start path is wrapped so any error still releases it.

## Emergency escape hatch

**Hold `Esc` for 3 seconds** to end a strict break early. There is no on-screen
hint (that would weaken enforcement), but the hatch always exists: an
undiscoverable exit is no exit in a real emergency.

This is *not* a kiosk lock. Switching to a TTY (Ctrl+Alt+F2) or killing
`gnome-shell` always exits a break — unavoidable, and arguably correct.

## Install (development)

The extension cannot ship inside the Respite Flatpak — sandboxed apps cannot
install Shell extensions into the live session. Install it separately:

```sh
./install.sh            # copy into ~/.local/share/gnome-shell/extensions
# log out/in (or on Xorg: Alt+F2, r), then:
gnome-extensions enable respite-strict-break@pawanrai9999.github.io
```

Watch its logs while testing:

```sh
journalctl -f -o cat /usr/bin/gnome-shell
```

## Pack for extensions.gnome.org

```sh
./install.sh pack       # writes respite-strict-break@pawanrai9999.github.io.shell-extension.zip
```

## Supported GNOME versions

Targets GNOME Shell 45–50 (the ESM era). Developed and tested against 50; the
`Main.pushModal` / grab API has been stable across this range, but each major
GNOME bump may need the `shell-version` list in `metadata.json` revisited.
