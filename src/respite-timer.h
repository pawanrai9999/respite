/* respite-timer.h
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

/**
 * RespiteTimerState:
 * @RESPITE_TIMER_STATE_IDLE: the timer is stopped; no cycle is running.
 * @RESPITE_TIMER_STATE_WORKING: counting down the work interval.
 * @RESPITE_TIMER_STATE_WARNING: the pre-break warning window before a break.
 * @RESPITE_TIMER_STATE_BREAK: counting down an active break.
 *
 * The lifecycle is IDLE -> WORKING -> (WARNING) -> BREAK -> WORKING -> ...
 */
typedef enum
{
	RESPITE_TIMER_STATE_IDLE,
	RESPITE_TIMER_STATE_WORKING,
	RESPITE_TIMER_STATE_WARNING,
	RESPITE_TIMER_STATE_BREAK,
} RespiteTimerState;

#define RESPITE_TYPE_TIMER_STATE (respite_timer_state_get_type ())

GType respite_timer_state_get_type (void);

#define RESPITE_TYPE_TIMER (respite_timer_get_type ())

G_DECLARE_FINAL_TYPE (RespiteTimer, respite_timer, RESPITE, TIMER, GObject)

RespiteTimer      *respite_timer_new                     (void);
void               respite_timer_start                   (RespiteTimer *self);
void               respite_timer_stop                    (RespiteTimer *self);
void               respite_timer_end_break               (RespiteTimer *self);
gboolean           respite_timer_postpone                (RespiteTimer *self);
RespiteTimerState  respite_timer_get_state               (RespiteTimer *self);
guint              respite_timer_get_remaining           (RespiteTimer *self);
guint              respite_timer_get_postpones_remaining (RespiteTimer *self);

G_END_DECLS
