#!/usr/bin/env bash
#
# Install, pack, or remove the Respite Strict Break GNOME Shell extension.
#
# SPDX-License-Identifier: AGPL-3.0-or-later

set -euo pipefail

UUID="respite-strict-break@pawanrai9999.github.io"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${XDG_DATA_HOME:-$HOME/.local/share}/gnome-shell/extensions/$UUID"

# Files that make up the shipped extension (everything else here is tooling).
FILES=(metadata.json extension.js stylesheet.css dbus-interface.xml)

cmd_install() {
    mkdir -p "$DEST"
    for f in "${FILES[@]}"; do
        cp "$HERE/$f" "$DEST/$f"
    done
    echo "Installed to $DEST"
    echo "Now log out/in (Xorg: Alt+F2, r) and enable with:"
    echo "  gnome-extensions enable $UUID"
}

cmd_pack() {
    local out="$HERE/$UUID.shell-extension.zip"
    rm -f "$out"
    ( cd "$HERE" && zip -q "$out" "${FILES[@]}" )
    echo "Wrote $out (upload to extensions.gnome.org)"
}

cmd_uninstall() {
    rm -rf "$DEST"
    echo "Removed $DEST"
}

case "${1:-install}" in
    install)   cmd_install ;;
    pack)      cmd_pack ;;
    uninstall) cmd_uninstall ;;
    *) echo "usage: $0 {install|pack|uninstall}" >&2; exit 2 ;;
esac
