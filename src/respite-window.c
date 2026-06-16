/* respite-window.c
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

struct _RespiteWindow
{
	AdwApplicationWindow  parent_instance;

	GSettings           *settings;

	/* The shared work/break engine, borrowed from the application so the
	 * status group can reflect live state. The application outlives every
	 * window, so this is held without a reference. NULL only if the window
	 * is somehow built without an application. */
	RespiteTimer        *timer;

	/* Template widgets */
	GtkImage            *status_icon;
	AdwActionRow        *status_row;
	GtkButton           *toggle_button;
	AdwActionRow        *postpones_row;
	AdwSpinRow          *work_interval_row;
	AdwSpinRow          *break_duration_row;
	AdwSpinRow          *postpone_allowance_row;
	AdwSpinRow          *postpone_duration_row;
	AdwSpinRow          *pre_break_warning_row;
	AdwSwitchRow        *autostart_row;
};

G_DEFINE_FINAL_TYPE (RespiteWindow, respite_window, ADW_TYPE_APPLICATION_WINDOW)

/* Durations are stored in seconds in GSettings but edited in minutes in the UI,
 * so the duration rows bind through these mappings rather than directly. */
#define RESPITE_SECONDS_PER_MINUTE 60

static gboolean
respite_seconds_to_minutes (GValue   *value,
                            GVariant *variant,
                            gpointer  user_data)
{
	guint32 seconds = g_variant_get_uint32 (variant);

	g_value_set_double (value, (gdouble) seconds / RESPITE_SECONDS_PER_MINUTE);
	return TRUE;
}

static GVariant *
respite_minutes_to_seconds (const GValue       *value,
                            const GVariantType *expected_type,
                            gpointer            user_data)
{
	gdouble minutes = g_value_get_double (value);

	return g_variant_new_uint32 ((guint32) (minutes * RESPITE_SECONDS_PER_MINUTE + 0.5));
}

/* Repaint the Status group from the timer's current state: the icon and
 * headline describe the phase, the subtitle counts down to the next
 * transition, and the postpones row appears only while a postpone could still
 * be spent. Called on every state change and tick, and once at construction. */
static void
respite_window_refresh_status (RespiteWindow *self)
{
	RespiteTimerState state;
	guint remaining;
	guint postpones;
	const char *icon_name;
	const char *title;
	g_autofree char *subtitle = NULL;
	gboolean show_postpones;

	if (self->timer == NULL)
		return;

	state = respite_timer_get_state (self->timer);
	remaining = respite_timer_get_remaining (self->timer);
	postpones = respite_timer_get_postpones_remaining (self->timer);

	show_postpones = (state == RESPITE_TIMER_STATE_WORKING ||
	                  state == RESPITE_TIMER_STATE_WARNING);

	switch (state)
	{
	case RESPITE_TIMER_STATE_WORKING:
		icon_name = "media-playback-start-symbolic";
		title = _("Working");
		/* TRANSLATORS: %u:%02u is a minutes:seconds countdown. */
		subtitle = g_strdup_printf (_("Next break in %u:%02u"), remaining / 60, remaining % 60);
		break;

	case RESPITE_TIMER_STATE_WARNING:
		icon_name = "dialog-warning-symbolic";
		title = _("Break starting soon");
		subtitle = g_strdup_printf (_("Next break in %u:%02u"), remaining / 60, remaining % 60);
		break;

	case RESPITE_TIMER_STATE_BREAK:
		icon_name = "media-playback-pause-symbolic";
		title = _("On a break");
		subtitle = g_strdup_printf (_("Break ends in %u:%02u"), remaining / 60, remaining % 60);
		break;

	case RESPITE_TIMER_STATE_IDLE:
	default:
		icon_name = "media-playback-stop-symbolic";
		title = _("Paused");
		subtitle = g_strdup (_("Resume to start the next work interval."));
		break;
	}

	gtk_image_set_from_icon_name (self->status_icon, icon_name);
	adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->status_row), title);
	adw_action_row_set_subtitle (self->status_row, subtitle);

	/* The same control resumes a paused timer or pauses a running one. */
	gtk_button_set_label (self->toggle_button,
	                      state == RESPITE_TIMER_STATE_IDLE ? _("Resume") : _("Pause"));

	if (show_postpones)
	{
		g_autofree char *postpones_title =
			g_strdup_printf (ngettext ("%u postpone remaining",
			                           "%u postpones remaining", postpones),
			                 postpones);

		adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self->postpones_row),
		                               postpones_title);
	}

	gtk_widget_set_visible (GTK_WIDGET (self->postpones_row), show_postpones);
}

static void
respite_window_on_tick (RespiteWindow *self,
                        guint          remaining)
{
	respite_window_refresh_status (self);
}

static void
respite_window_on_state_changed (RespiteWindow     *self,
                                 RespiteTimerState  state)
{
	respite_window_refresh_status (self);
}

/* GtkWindow:application is not a construct property, so it is only set after
 * construction; wait for it here, then grab the shared timer and start
 * mirroring its state. The handlers auto-disconnect with the window, so the
 * timer never calls into a finalized window. */
static void
respite_window_application_changed (GObject    *object,
                                    GParamSpec *pspec,
                                    gpointer    user_data)
{
	RespiteWindow *self = RESPITE_WINDOW (object);
	GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));

	/* Only wire up once, the first time a Respite application is attached. */
	if (self->timer != NULL || !RESPITE_IS_APPLICATION (app))
		return;

	self->timer = respite_application_get_timer (RESPITE_APPLICATION (app));

	g_signal_connect_object (self->timer, "tick",
	                         G_CALLBACK (respite_window_on_tick),
	                         self, G_CONNECT_SWAPPED);
	g_signal_connect_object (self->timer, "state-changed",
	                         G_CALLBACK (respite_window_on_state_changed),
	                         self, G_CONNECT_SWAPPED);

	respite_window_refresh_status (self);
}

static void
respite_window_dispose (GObject *object)
{
	RespiteWindow *self = RESPITE_WINDOW (object);

	g_clear_object (&self->settings);

	G_OBJECT_CLASS (respite_window_parent_class)->dispose (object);
}

static void
respite_window_class_init (RespiteWindowClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = respite_window_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/com/texoviva/respite/respite-window.ui");
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, status_icon);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, status_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, toggle_button);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, postpones_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, work_interval_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, break_duration_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, postpone_allowance_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, postpone_duration_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, pre_break_warning_row);
	gtk_widget_class_bind_template_child (widget_class, RespiteWindow, autostart_row);
}

static void
respite_window_init (RespiteWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	self->settings = g_settings_new ("com.texoviva.respite");

	/* The application (and thus the shared timer) is attached after
	 * construction, so wire the status group up once it arrives. */
	g_signal_connect (self, "notify::application",
	                  G_CALLBACK (respite_window_application_changed), NULL);

	/* Duration rows: seconds in the schema <-> minutes in the UI. */
	g_settings_bind_with_mapping (self->settings, "work-interval",
	                              self->work_interval_row, "value",
	                              G_SETTINGS_BIND_DEFAULT,
	                              respite_seconds_to_minutes,
	                              respite_minutes_to_seconds,
	                              NULL, NULL);
	g_settings_bind_with_mapping (self->settings, "break-duration",
	                              self->break_duration_row, "value",
	                              G_SETTINGS_BIND_DEFAULT,
	                              respite_seconds_to_minutes,
	                              respite_minutes_to_seconds,
	                              NULL, NULL);
	g_settings_bind_with_mapping (self->settings, "postpone-duration",
	                              self->postpone_duration_row, "value",
	                              G_SETTINGS_BIND_DEFAULT,
	                              respite_seconds_to_minutes,
	                              respite_minutes_to_seconds,
	                              NULL, NULL);

	/* Direct bindings: count and seconds map straight onto the row value. */
	g_settings_bind (self->settings, "postpone-allowance",
	                 self->postpone_allowance_row, "value",
	                 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (self->settings, "pre-break-warning",
	                 self->pre_break_warning_row, "value",
	                 G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (self->settings, "autostart",
	                 self->autostart_row, "active",
	                 G_SETTINGS_BIND_DEFAULT);
}
