/* respite-strict.h
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

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define RESPITE_TYPE_STRICT (respite_strict_get_type ())

G_DECLARE_FINAL_TYPE (RespiteStrict, respite_strict, RESPITE, STRICT, GObject)

/* Client for the companion GNOME Shell extension that enforces strict breaks
 * from inside the compositor. The daemon stays the brain: this object only
 * tracks whether the extension is present and tells it to lock/unlock.
 *
 * Signals:
 *   availability-changed (gboolean available) — the extension started or
 *       stopped owning its bus name; drives the preferences toggle's gating.
 *   released ()  — enforcement dropped (grab lost, extension disabled, GNOME
 *       reload) or a lockdown could not be taken; the caller should fall back
 *       to its own overlay for the remainder of the break.
 *   escape-requested () — the user invoked the in-shell escape hatch; the
 *       caller should end the break early.
 */

RespiteStrict *respite_strict_new              (void);

/* TRUE while the extension owns its well-known name and can enforce a break. */
gboolean       respite_strict_is_available     (RespiteStrict *self);

/* Fire-and-forget lockdown control. All are no-ops when the extension is not
 * available, so callers need not guard every call. */
void           respite_strict_start_break      (RespiteStrict *self,
                                                guint          seconds);
void           respite_strict_update_remaining (RespiteStrict *self,
                                                guint          seconds);
void           respite_strict_end_break        (RespiteStrict *self);

G_END_DECLS
