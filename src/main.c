/* main.c
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

#include <glib/gi18n.h>

#include "respite-application.h"
#include "respite-timer.h"

/* --- Temporary --debug-timer scaffolding (Phase 2.6) ----------------------
 *
 * Runs the headless RespiteTimer with no GApplication and no UI, logging
 * every state change and tick to the console so the engine can be validated
 * before the overlay exists. This whole block is removed in Phase 7.5.
 */

static const char *
debug_state_name (RespiteTimerState state)
{
	switch (state)
	{
	case RESPITE_TIMER_STATE_IDLE:    return "IDLE";
	case RESPITE_TIMER_STATE_WORKING: return "WORKING";
	case RESPITE_TIMER_STATE_WARNING: return "WARNING";
	case RESPITE_TIMER_STATE_BREAK:   return "BREAK";
	default:                          return "?";
	}
}

static void
debug_on_tick (RespiteTimer *timer,
               guint         remaining,
               gpointer      user_data)
{
	g_print ("[%s] tick: %02u:%02u\n",
	         debug_state_name (respite_timer_get_state (timer)),
	         remaining / 60, remaining % 60);
}

static void
debug_on_state_changed (RespiteTimer      *timer,
                        RespiteTimerState  state,
                        gpointer           user_data)
{
	g_print ("== state-changed: %s\n", debug_state_name (state));
}

static void
debug_on_break_started (RespiteTimer *timer,
                        gpointer      user_data)
{
	g_print (">> break-started\n");
}

static void
debug_on_break_ended (RespiteTimer *timer,
                      gpointer      user_data)
{
	g_print ("<< break-ended\n");
}

static int
run_debug_timer (void)
{
	g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
	g_autoptr(RespiteTimer) timer = respite_timer_new ();

	g_print ("respite --debug-timer: console engine test, Ctrl+C to quit\n");

	g_signal_connect (timer, "tick", G_CALLBACK (debug_on_tick), NULL);
	g_signal_connect (timer, "state-changed", G_CALLBACK (debug_on_state_changed), NULL);
	g_signal_connect (timer, "break-started", G_CALLBACK (debug_on_break_started), NULL);
	g_signal_connect (timer, "break-ended", G_CALLBACK (debug_on_break_ended), NULL);

	respite_timer_start (timer);
	g_main_loop_run (loop);

	return 0;
}

/* --- end --debug-timer scaffolding --------------------------------------- */

int
main (int   argc,
      char *argv[])
{
	g_autoptr(RespiteApplication) app = NULL;
	int ret;

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	for (int i = 1; i < argc; i++)
		if (g_strcmp0 (argv[i], "--debug-timer") == 0)
			return run_debug_timer ();

	app = respite_application_new ("com.texoviva.respite", G_APPLICATION_DEFAULT_FLAGS);
	ret = g_application_run (G_APPLICATION (app), argc, argv);

	return ret;
}
