/* respite-timer.c
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

#include "config.h"

#include "respite-timer.h"

#include <gio/gio.h>

/* The headless timer core. It knows nothing about the UI: it reads durations
 * from GSettings, counts down using a monotonic clock so it survives suspend
 * and timer drift, and reports progress purely through signals.
 *
 * Durations are read fresh at the start of each phase, so live settings
 * changes take effect on the next cycle rather than the one in progress.
 */

struct _RespiteTimer
{
	GObject            parent_instance;

	GSettings         *settings;

	RespiteTimerState  state;

	/* Monotonic deadline (microseconds) at which the current phase ends. */
	gint64             deadline;
	/* Last value broadcast through ::tick, so we only emit on change. */
	guint              remaining;

	guint              tick_source_id;
};

G_DEFINE_ENUM_TYPE (RespiteTimerState, respite_timer_state,
                    G_DEFINE_ENUM_VALUE (RESPITE_TIMER_STATE_IDLE, "idle"),
                    G_DEFINE_ENUM_VALUE (RESPITE_TIMER_STATE_WORKING, "working"),
                    G_DEFINE_ENUM_VALUE (RESPITE_TIMER_STATE_WARNING, "warning"),
                    G_DEFINE_ENUM_VALUE (RESPITE_TIMER_STATE_BREAK, "break"))

G_DEFINE_FINAL_TYPE (RespiteTimer, respite_timer, G_TYPE_OBJECT)

enum
{
	SIGNAL_TICK,
	SIGNAL_STATE_CHANGED,
	SIGNAL_BREAK_STARTED,
	SIGNAL_BREAK_ENDED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Convert a remaining microsecond span to whole seconds, rounding up so the
 * displayed countdown only reaches zero once the phase has truly elapsed. */
static guint
remaining_seconds (gint64 remaining_us)
{
	if (remaining_us <= 0)
		return 0;

	return (guint) ((remaining_us + G_USEC_PER_SEC - 1) / G_USEC_PER_SEC);
}

static void
set_state (RespiteTimer      *self,
           RespiteTimerState  state)
{
	if (self->state == state)
		return;

	self->state = state;
	g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, state);
}

/* Recompute and broadcast the remaining time, emitting ::tick only when the
 * whole-second value actually changes. */
static void
emit_tick (RespiteTimer *self)
{
	guint remaining = remaining_seconds (self->deadline - g_get_monotonic_time ());

	if (remaining == self->remaining)
		return;

	self->remaining = remaining;
	g_signal_emit (self, signals[SIGNAL_TICK], 0, remaining);
}

static void
begin_working (RespiteTimer *self)
{
	guint work_interval = g_settings_get_uint (self->settings, "work-interval");

	self->deadline = g_get_monotonic_time () + (gint64) work_interval * G_USEC_PER_SEC;
	self->remaining = work_interval;

	set_state (self, RESPITE_TIMER_STATE_WORKING);
	g_signal_emit (self, signals[SIGNAL_TICK], 0, self->remaining);
}

static void
begin_break (RespiteTimer *self)
{
	guint break_duration = g_settings_get_uint (self->settings, "break-duration");

	self->deadline = g_get_monotonic_time () + (gint64) break_duration * G_USEC_PER_SEC;
	self->remaining = break_duration;

	set_state (self, RESPITE_TIMER_STATE_BREAK);
	g_signal_emit (self, signals[SIGNAL_BREAK_STARTED], 0);
	g_signal_emit (self, signals[SIGNAL_TICK], 0, self->remaining);
}

/* The current phase has elapsed; move WORKING -> BREAK or BREAK -> WORKING. */
static void
advance_phase (RespiteTimer *self)
{
	switch (self->state)
	{
	case RESPITE_TIMER_STATE_WORKING:
	case RESPITE_TIMER_STATE_WARNING:
		begin_break (self);
		break;

	case RESPITE_TIMER_STATE_BREAK:
		g_signal_emit (self, signals[SIGNAL_BREAK_ENDED], 0);
		begin_working (self);
		break;

	case RESPITE_TIMER_STATE_IDLE:
	default:
		g_assert_not_reached ();
		break;
	}
}

static gboolean
tick_cb (gpointer user_data)
{
	RespiteTimer *self = RESPITE_TIMER (user_data);

	emit_tick (self);

	if (g_get_monotonic_time () >= self->deadline)
		advance_phase (self);

	return G_SOURCE_CONTINUE;
}

RespiteTimer *
respite_timer_new (void)
{
	return g_object_new (RESPITE_TYPE_TIMER, NULL);
}

void
respite_timer_start (RespiteTimer *self)
{
	g_return_if_fail (RESPITE_IS_TIMER (self));

	if (self->state != RESPITE_TIMER_STATE_IDLE)
		return;

	begin_working (self);

	self->tick_source_id = g_timeout_add_seconds (1, tick_cb, self);
}

void
respite_timer_stop (RespiteTimer *self)
{
	g_return_if_fail (RESPITE_IS_TIMER (self));

	if (self->state == RESPITE_TIMER_STATE_IDLE)
		return;

	g_clear_handle_id (&self->tick_source_id, g_source_remove);

	self->deadline = 0;
	self->remaining = 0;
	set_state (self, RESPITE_TIMER_STATE_IDLE);
}

RespiteTimerState
respite_timer_get_state (RespiteTimer *self)
{
	g_return_val_if_fail (RESPITE_IS_TIMER (self), RESPITE_TIMER_STATE_IDLE);

	return self->state;
}

guint
respite_timer_get_remaining (RespiteTimer *self)
{
	g_return_val_if_fail (RESPITE_IS_TIMER (self), 0);

	return self->remaining;
}

static void
respite_timer_dispose (GObject *object)
{
	RespiteTimer *self = RESPITE_TIMER (object);

	g_clear_handle_id (&self->tick_source_id, g_source_remove);
	g_clear_object (&self->settings);

	G_OBJECT_CLASS (respite_timer_parent_class)->dispose (object);
}

static void
respite_timer_class_init (RespiteTimerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = respite_timer_dispose;

	/**
	 * RespiteTimer::tick:
	 * @self: the timer
	 * @remaining: seconds left in the current phase
	 *
	 * Emitted roughly once per second (and on each phase transition) with
	 * the whole-second countdown for the current phase.
	 */
	signals[SIGNAL_TICK] =
		g_signal_new ("tick",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * RespiteTimer::state-changed:
	 * @self: the timer
	 * @state: the new #RespiteTimerState
	 *
	 * Emitted whenever the timer moves between IDLE/WORKING/WARNING/BREAK.
	 */
	signals[SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, RESPITE_TYPE_TIMER_STATE);

	/**
	 * RespiteTimer::break-started:
	 * @self: the timer
	 *
	 * Emitted when a break begins, before the first break ::tick.
	 */
	signals[SIGNAL_BREAK_STARTED] =
		g_signal_new ("break-started",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * RespiteTimer::break-ended:
	 * @self: the timer
	 *
	 * Emitted when a break finishes and the next work cycle begins.
	 */
	signals[SIGNAL_BREAK_ENDED] =
		g_signal_new ("break-ended",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
respite_timer_init (RespiteTimer *self)
{
	self->settings = g_settings_new ("com.texoviva.respite");
	self->state = RESPITE_TIMER_STATE_IDLE;
}
