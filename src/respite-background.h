/* respite-background.h
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

#include <gio/gio.h>

G_BEGIN_DECLS

/* TRUE when running inside a Flatpak sandbox, where background and autostart
 * are mediated by XDG portals rather than handled natively. */
gboolean respite_background_is_sandboxed              (void);

/* Daemon startup: ask the Background portal for permission to keep running
 * without a window. A no-op outside the sandbox, where an ordinary process
 * needs no such permission. */
void     respite_background_request_run_in_background (void);

G_END_DECLS
