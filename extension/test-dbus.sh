#!/usr/bin/env bash
#
# Manually exercise the Strict interface without the daemon, to verify the
# extension round-trip. Run after enabling the extension.
#
#   ./test-dbus.sh ping     presence/version handshake (safe, no grab)
#   ./test-dbus.sh demo      5-second strict break (LOCKS THE SCREEN)
#
# Escaping a demo:
#   - hold Esc for 3s, OR
#   - do nothing: the demo's heartbeat stops after ~5s and the extension's
#     watchdog auto-releases the grab within 10s. You cannot be trapped.
#
# SPDX-License-Identifier: AGPL-3.0-or-later

set -euo pipefail

NAME="io.github.pawanrai9999.respite.Strict"
PATH_="/io/github/pawanrai9999/respite/Strict"

call() { gdbus call --session --dest "$NAME" --object-path "$PATH_" --method "$NAME.$1" "${@:2}"; }

case "${1:-ping}" in
    ping)
        call Ping
        ;;
    demo)
        echo "Starting a 5s strict break in 2s — hold Esc to escape."
        sleep 2
        call StartBreak 5
        for s in 4 3 2 1 0; do sleep 1; call UpdateRemaining "$s" >/dev/null; done
        call EndBreak
        ;;
    *)
        echo "usage: $0 {ping|demo}" >&2; exit 2 ;;
esac
