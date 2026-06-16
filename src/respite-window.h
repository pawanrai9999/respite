/* respite-window.h
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

#include <adwaita.h>

G_BEGIN_DECLS

#define RESPITE_TYPE_WINDOW (respite_window_get_type())

G_DECLARE_FINAL_TYPE (RespiteWindow, respite_window, RESPITE, WINDOW, AdwApplicationWindow)

G_END_DECLS
