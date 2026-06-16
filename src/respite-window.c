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

#include "respite-window.h"

struct _RespiteWindow
{
	AdwApplicationWindow  parent_instance;

	GSettings           *settings;

	/* Template widgets */
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
