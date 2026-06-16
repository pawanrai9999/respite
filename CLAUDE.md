# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Respite is a break-reminder application for the GNOME desktop, written in **C** using **GTK4 + libadwaita**, targeting **Wayland**. The user sets a work interval and a break length; when the interval elapses Respite blacks out every monitor with a centered `Break for MM:SS` countdown, allows a limited number of postponements, and is meant to run as an autostarting background daemon.

**Current state:** Phases 1 (settings foundation) and 2 (timer engine) are complete. The "Hello World" template has been replaced with an Adwaita preferences window whose controls are bound two-way to GSettings with input constraints enforced, and a UI-agnostic `RespiteTimer` GObject (`src/respite-timer.c`) drives the work/break countdown via a monotonic clock, emitting `tick`/`state-changed`/`break-started`/`break-ended` signals (exercised by the temporary `--debug-timer` console mode in `main.c`). The daemon/process model, break overlay, postpone mechanism, and autostart are not implemented yet (see `PLAN.md` for the phased roadmap and current status). The README's "Behavior in Detail" and "Roadmap" sections describe the intended design.

## Build & Run

The GNOME Builder IDE and the build toolchain (meson, ninja, gtk4/libadwaita dev files) are **not installed natively on the host** — they are provided through Flatpak: GNOME Builder is installed as a Flatpak app, and meson/ninja/etc. come from the `org.gnome.Sdk` runtime (versions **49 and 50** are installed; note the manifest pins 48, which is not). To compile against that toolchain without Builder, run meson inside the SDK runtime:

```sh
flatpak run --filesystem=/home/pawan/Projects/Respite --command=sh org.gnome.Sdk//50 -c \
  'meson setup _build && meson compile -C _build'
```

Standard Meson workflow (used inside Builder or any environment where the toolchain is on `PATH`):

```sh
meson setup _build
meson compile -C _build
./_build/src/respite          # run the built binary
meson install -C _build       # optional, installs system-wide
```

Flatpak build (manifest `com.texoviva.respite.json`, GNOME Platform/SDK 48):

```sh
flatpak-builder --user --install _flatpak com.texoviva.respite.json
```

Note: the manifest's git source points at `file:///home/pawan/Projects`, so a Flatpak build pulls committed code from that path rather than the working tree.

There is no test suite or linter configured. Build warnings are extensive (see the `test_c_args` list in the top-level `meson.build`) but `werror=false`, so warnings do not fail the build.

## Architecture & Conventions

- **App ID / namespace:** everything uses `com.texoviva.respite`. GResource base path is `/com/texoviva/respite`; C symbols use the `respite_` prefix and `RESPITE_` / `Respite` type macros.
- **Object model:** GObject-based. `RespiteApplication` (subclasses `AdwApplication`) owns app-level actions (`app.quit`, `app.about`) and creates `RespiteWindow` (subclasses `AdwApplicationWindow`) on `activate`. Entry point is `src/main.c`.
- **UI is defined in `.ui` templates, not C.** `src/respite-window.ui` and `src/shortcuts-dialog.ui` are GTK Builder XML, bundled via `src/respite.gresource.xml` and loaded with `gtk_widget_class_set_template_from_resource`. Widgets are wired into the struct with `gtk_widget_class_bind_template_child`. To add UI, edit the `.ui` file, register the resource in the gresource XML, and bind template children in the corresponding `*_class_init`.
- **Three meson layers:** root `meson.build` sets up i18n/gnome modules, generates `config.h`, and recurses into `src/`, `data/`, and `po/`. Source files and dependencies are declared in `src/meson.build`.
- **Settings:** a GSettings schema exists at `data/com.texoviva.respite.gschema.xml` — this is where persisted preferences (interval, break time, postpone allowance, autostart) belong.
- **Data/integration files** live in `data/`: `.desktop`, D-Bus `.service`, AppStream `.metainfo.xml`, icons under `data/icons/hicolor/`.
- **i18n:** gettext package is `respite`. New translatable source files must be added to `po/POTFILES.in`; wrap user-facing strings in `_()`.
- **License:** AGPL-3.0-or-later. New source files should carry the same SPDX header block as existing files in `src/`.
