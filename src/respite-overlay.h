/* respite-overlay.h
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RESPITE_TYPE_OVERLAY (respite_overlay_get_type())

G_DECLARE_FINAL_TYPE (RespiteOverlay, respite_overlay, RESPITE, OVERLAY, GtkWindow)

RespiteOverlay *respite_overlay_new         (GtkApplication *application,
                                             GdkMonitor     *monitor);
GdkMonitor     *respite_overlay_get_monitor (RespiteOverlay *self);

G_END_DECLS
