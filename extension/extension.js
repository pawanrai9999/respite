/* extension.js
 *
 * Copyright 2026 pawan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/* Respite Strict Break — the actuator half of strict-break enforcement.
 *
 * The Respite daemon owns all timer state and drives this extension over the
 * session bus. On StartBreak we take a shell-side modal grab (Main.pushModal)
 * and cover every monitor with a black actor showing a countdown; because the
 * grab routes all input to our actor and an actionMode of NONE filters every
 * global keybinding, Alt+Tab, Super, the overview and app switching are all
 * suppressed. EndBreak (or the hold-Esc escape hatch) releases the grab. See
 * dbus-interface.xml for the contract.
 *
 * SAFETY: a modal grab is the strongest thing an extension can do to the
 * session, so this code is built so the grab can never outlive its purpose:
 *
 *   1. The grab handle is stored the instant pushModal returns, and the whole
 *      StartBreak path is wrapped in try/catch, so any later error still
 *      releases the grab (an earlier version threw between grabbing and
 *      storing the handle, which trapped the session).
 *   2. A heartbeat watchdog auto-releases the grab if the daemon stops driving
 *      it. The daemon calls UpdateRemaining every second; each call re-arms the
 *      watchdog. If those stop (daemon crash, disconnect, a raw StartBreak with
 *      no follow-up) the grab self-releases within WATCHDOG_MS.
 *   3. Holding Esc is the manual escape hatch.
 */

import GLib from 'gi://GLib';
import Gio from 'gi://Gio';
import Clutter from 'gi://Clutter';
import St from 'gi://St';
import Shell from 'gi://Shell';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

const BUS_NAME = 'io.github.pawanrai9999.respite.Strict';
const OBJECT_PATH = '/io/github/pawanrai9999/respite/Strict';

/* How long Esc must be held before the emergency escape hatch fires. */
const ESCAPE_HOLD_MS = 3000;

/* Failsafe: if the daemon stops driving the break (no UpdateRemaining/EndBreak)
 * for this long, assume it is gone and release the grab so the user is never
 * trapped. The daemon heartbeats every second, so this only fires on a real
 * fault. */
const WATCHDOG_MS = 10000;

export default class RespiteStrictExtension extends Extension {
    enable() {
        this._grab = null;
        this._overlay = null;
        this._label = null;
        this._capturedEventId = 0;
        this._escapeTimeoutId = 0;
        this._watchdogId = 0;
        this._monitorsChangedId = 0;

        const decoder = new TextDecoder();
        const [, bytes] = this.dir.get_child('dbus-interface.xml').load_contents(null);
        const ifaceXml = decoder.decode(bytes);

        this._dbus = Gio.DBusExportedObject.wrapJSObject(ifaceXml, this);
        this._dbus.export(Gio.DBus.session, OBJECT_PATH);
        this._nameOwnerId = Gio.bus_own_name(
            Gio.BusType.SESSION,
            BUS_NAME,
            Gio.BusNameOwnerFlags.NONE,
            null, null, null);
    }

    disable() {
        /* If we are torn down mid-break (extension disabled, GNOME reload, lock
         * screen) tell the daemon enforcement dropped so it can fall back. */
        if (this._grab || this._overlay)
            this._dbus?.emit_signal('Released', null);

        this._teardown();

        if (this._nameOwnerId) {
            Gio.bus_unown_name(this._nameOwnerId);
            this._nameOwnerId = 0;
        }
        if (this._dbus) {
            this._dbus.unexport();
            this._dbus = null;
        }
    }

    /* ---- D-Bus methods (names must match dbus-interface.xml) ---- */

    StartBreak(seconds) {
        /* Re-entrant StartBreak just refreshes the running break. */
        if (this._grab) {
            this._setRemaining(seconds);
            this._armWatchdog();
            return true;
        }

        try {
            this._buildOverlay();

            /* Arm the watchdog BEFORE grabbing: if pushModal or anything after
             * it throws, the grab (if taken) is still released automatically. */
            this._armWatchdog();

            /* The real lever: actionMode NONE filters out every global
             * keybinding while the grab is held, so the shell never acts on
             * Alt+Tab/Super/etc. */
            const grab = Main.pushModal(this._overlay, {
                actionMode: Shell.ActionMode.NONE,
            });

            /* Store the handle immediately so teardown can always release it,
             * even if a later line throws. */
            this._grab = grab;

            this._capturedEventId = this._overlay.connect(
                'captured-event', this._onCapturedEvent.bind(this));
            this._overlay.grab_key_focus();

            this._setRemaining(seconds);
            return true;
        } catch (e) {
            logError(e, 'Respite: StartBreak failed; releasing any grab');
            this._teardown();
            return false;
        }
    }

    UpdateRemaining(seconds) {
        this._setRemaining(seconds);
        /* Heartbeat: prove the daemon is still alive and driving the break. */
        if (this._grab)
            this._armWatchdog();
    }

    EndBreak() {
        this._teardown();
    }

    Ping() {
        return this.metadata['version-name'] ?? String(this.metadata.version ?? '');
    }

    /* ---- internals ---- */

    _buildOverlay() {
        this._overlay = new St.Widget({
            style_class: 'respite-strict-overlay',
            reactive: true,
            can_focus: true,
            layout_manager: new Clutter.BinLayout(),
        });

        this._label = new St.Label({
            style_class: 'respite-strict-countdown',
            x_align: Clutter.ActorAlign.CENTER,
            y_align: Clutter.ActorAlign.CENTER,
        });
        this._overlay.add_child(this._label);

        Main.layoutManager.addTopChrome(this._overlay);
        this._updateGeometry();
        this._monitorsChangedId = Main.layoutManager.connect(
            'monitors-changed', () => this._updateGeometry());
    }

    _armWatchdog() {
        if (this._watchdogId)
            GLib.source_remove(this._watchdogId);

        this._watchdogId = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT, WATCHDOG_MS, () => {
                this._watchdogId = 0;
                log('Respite: strict-break watchdog fired; daemon went quiet, releasing grab');
                this._dbus?.emit_signal('Released', null);
                this._teardown();
                return GLib.SOURCE_REMOVE;
            });
    }

    _updateGeometry() {
        if (!this._overlay)
            return;

        /* One stage-sized actor spans every monitor; the shell reconciles
         * monitor hotplug for us, so there is no per-monitor bookkeeping. */
        const [width, height] = global.stage.get_size();
        this._overlay.set_position(0, 0);
        this._overlay.set_size(width, height);
    }

    _setRemaining(seconds) {
        if (!this._label)
            return;

        const total = Math.max(0, Math.trunc(seconds));
        const mm = Math.trunc(total / 60).toString().padStart(2, '0');
        const ss = (total % 60).toString().padStart(2, '0');
        this._label.set_text(`Break for ${mm}:${ss}`);
    }

    _onCapturedEvent(_actor, event) {
        const type = event.type();

        if (type === Clutter.EventType.KEY_PRESS) {
            if (event.get_key_symbol() === Clutter.KEY_Escape && !this._escapeTimeoutId) {
                this._escapeTimeoutId = GLib.timeout_add(
                    GLib.PRIORITY_DEFAULT, ESCAPE_HOLD_MS, () => {
                        this._escapeTimeoutId = 0;
                        this._dbus?.emit_signal('EscapeRequested', null);
                        this._teardown();
                        return GLib.SOURCE_REMOVE;
                    });
            }
            return Clutter.EVENT_STOP;
        }

        if (type === Clutter.EventType.KEY_RELEASE) {
            if (event.get_key_symbol() === Clutter.KEY_Escape && this._escapeTimeoutId) {
                GLib.source_remove(this._escapeTimeoutId);
                this._escapeTimeoutId = 0;
            }
            return Clutter.EVENT_STOP;
        }

        /* Only swallow keyboard input (the modal grab already keeps everything
         * else from reaching other windows). Crossing/motion events must keep
         * propagating: stopping them trips a Clutter EVENT_PROPAGATE check and
         * spams the journal. */
        return Clutter.EVENT_PROPAGATE;
    }

    _teardown() {
        if (this._watchdogId) {
            GLib.source_remove(this._watchdogId);
            this._watchdogId = 0;
        }
        if (this._escapeTimeoutId) {
            GLib.source_remove(this._escapeTimeoutId);
            this._escapeTimeoutId = 0;
        }
        if (this._monitorsChangedId) {
            Main.layoutManager.disconnect(this._monitorsChangedId);
            this._monitorsChangedId = 0;
        }
        if (this._capturedEventId && this._overlay) {
            this._overlay.disconnect(this._capturedEventId);
            this._capturedEventId = 0;
        }
        if (this._grab) {
            try {
                Main.popModal(this._grab);
            } catch (e) {
                logError(e, 'Respite: popModal failed during teardown');
            }
            this._grab = null;
        }
        if (this._overlay) {
            Main.layoutManager.removeChrome(this._overlay);
            this._overlay.destroy();
            this._overlay = null;
            this._label = null;
        }
    }
}
