/* respite-application.c
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
#include "respite-window.h"

struct _RespiteApplication
{
	AdwApplication parent_instance;

	/* The single work/break engine, shared across every entry point. */
	RespiteTimer  *timer;

	/* TRUE once we have taken a g_application_hold to stay alive headless. */
	gboolean       held;
};

G_DEFINE_FINAL_TYPE (RespiteApplication, respite_application, ADW_TYPE_APPLICATION)

RespiteApplication *
respite_application_new (const char        *application_id,
                         GApplicationFlags  flags)
{
	g_return_val_if_fail (application_id != NULL, NULL);

	return g_object_new (RESPITE_TYPE_APPLICATION,
	                     "application-id", application_id,
	                     "flags", flags,
	                     "resource-base-path", "/com/texoviva/respite",
	                     NULL);
}

/* Create the shared timer once per primary instance, before any activation or
 * command line is handled, so every entry point operates on the same engine
 * regardless of whether the first launch was --daemon or a plain window. */
static void
respite_application_startup (GApplication *app)
{
	RespiteApplication *self = RESPITE_APPLICATION (app);

	G_APPLICATION_CLASS (respite_application_parent_class)->startup (app);

	self->timer = respite_timer_new ();
}

/* Enter background/daemon mode: take a hold so the process stays alive with no
 * window. Idempotent, so repeated --daemon launches collapse onto one hold. */
static void
respite_application_start_daemon (RespiteApplication *self)
{
	g_assert (RESPITE_IS_APPLICATION (self));

	if (self->held)
		return;

	g_application_hold (G_APPLICATION (self));
	self->held = TRUE;

	respite_timer_start (self->timer);
}

/* The primary instance processes every invocation's command line here, whether
 * it was the first launch or a forwarded one from a second process. A
 * --daemon invocation runs headless; any other presents the settings window. */
static int
respite_application_command_line (GApplication            *app,
                                  GApplicationCommandLine *command_line)
{
	RespiteApplication *self = RESPITE_APPLICATION (app);
	GVariantDict *options;

	g_assert (RESPITE_IS_APPLICATION (self));

	options = g_application_command_line_get_options_dict (command_line);

	if (g_variant_dict_contains (options, "daemon"))
		respite_application_start_daemon (self);
	else
		g_application_activate (app);

	return 0;
}

/* Find the existing settings window, ignoring any transient surfaces, so a
 * forwarded activation raises the real window rather than creating a second. */
static RespiteWindow *
respite_application_find_window (RespiteApplication *self)
{
	for (GList *l = gtk_application_get_windows (GTK_APPLICATION (self));
	     l != NULL; l = l->next)
		if (RESPITE_IS_WINDOW (l->data))
			return RESPITE_WINDOW (l->data);

	return NULL;
}

static void
respite_application_activate (GApplication *app)
{
	RespiteApplication *self = RESPITE_APPLICATION (app);
	RespiteWindow *window;

	g_assert (RESPITE_IS_APPLICATION (self));

	/* Single instance: reuse the open settings window if there is one,
	 * including when the process is otherwise running headless as a daemon. */
	window = respite_application_find_window (self);

	if (window == NULL)
		window = g_object_new (RESPITE_TYPE_WINDOW,
		                       "application", app,
		                       NULL);

	gtk_window_present (GTK_WINDOW (window));
}

static void
respite_application_dispose (GObject *object)
{
	RespiteApplication *self = RESPITE_APPLICATION (object);

	g_clear_object (&self->timer);

	G_OBJECT_CLASS (respite_application_parent_class)->dispose (object);
}

static void
respite_application_class_init (RespiteApplicationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

	object_class->dispose = respite_application_dispose;

	app_class->startup = respite_application_startup;
	app_class->activate = respite_application_activate;
	app_class->command_line = respite_application_command_line;
}

RespiteTimer *
respite_application_get_timer (RespiteApplication *self)
{
	g_return_val_if_fail (RESPITE_IS_APPLICATION (self), NULL);

	return self->timer;
}

static void
respite_application_about_action (GSimpleAction *action,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
	static const char *developers[] = {"pawan", NULL};
	RespiteApplication *self = user_data;
	GtkWindow *window = NULL;

	g_assert (RESPITE_IS_APPLICATION (self));

	window = gtk_application_get_active_window (GTK_APPLICATION (self));

	adw_show_about_dialog (GTK_WIDGET (window),
	                       "application-name", "Respite",
	                       "application-icon", "com.texoviva.respite",
	                       "developer-name", "pawan",
	                       "translator-credits", _("translator-credits"),
	                       "version", "0.1.0",
	                       "developers", developers,
	                       "copyright", "© 2026 pawan",
	                       NULL);
}

static void
respite_application_quit_action (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
	RespiteApplication *self = user_data;

	g_assert (RESPITE_IS_APPLICATION (self));

	g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
	{ "quit", respite_application_quit_action },
	{ "about", respite_application_about_action },
};

static const GOptionEntry app_options[] = {
	{ "daemon", 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, NULL,
	  N_("Run in the background without showing a window"), NULL },
	{ NULL },
};

static void
respite_application_init (RespiteApplication *self)
{
	g_application_add_main_option_entries (G_APPLICATION (self), app_options);

	g_action_map_add_action_entries (G_ACTION_MAP (self),
	                                 app_actions,
	                                 G_N_ELEMENTS (app_actions),
	                                 self);
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.quit",
	                                       (const char *[]) { "<control>q", NULL });
}
