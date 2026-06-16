/* respite-overlay.c
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

#include "respite-overlay.h"

/* A single break overlay: a fullscreen, undecorated black window carrying a
 * centered countdown, covering exactly one monitor. The break coordinator
 * creates one per monitor on break-start and destroys them on break-end. */

struct _RespiteOverlay
{
	GtkWindow   parent_instance;

	/* The monitor this overlay covers, so the coordinator can reconcile the
	 * set of overlays against the set of monitors on hotplug. */
	GdkMonitor *monitor;

	/* Template widgets */
	GtkLabel   *countdown_label;
};

G_DEFINE_FINAL_TYPE (RespiteOverlay, respite_overlay, GTK_TYPE_WINDOW)

static void
respite_overlay_dispose (GObject *object)
{
	RespiteOverlay *self = RESPITE_OVERLAY (object);

	g_clear_object (&self->monitor);

	G_OBJECT_CLASS (respite_overlay_parent_class)->dispose (object);
}

static void
respite_overlay_class_init (RespiteOverlayClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = respite_overlay_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/com/texoviva/respite/respite-overlay.ui");
	gtk_widget_class_bind_template_child (widget_class, RespiteOverlay, countdown_label);
}

static void
respite_overlay_init (RespiteOverlay *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

RespiteOverlay *
respite_overlay_new (GtkApplication *application,
                     GdkMonitor     *monitor)
{
	RespiteOverlay *self;

	g_return_val_if_fail (GTK_IS_APPLICATION (application), NULL);
	g_return_val_if_fail (GDK_IS_MONITOR (monitor), NULL);

	self = g_object_new (RESPITE_TYPE_OVERLAY,
	                     "application", application,
	                     NULL);
	/* Hold a ref so the GdkMonitor stays alive (valid or not) for as long as
	 * this overlay does; the hotplug reconciler compares monitors by pointer. */
	self->monitor = g_object_ref (monitor);

	/* Fullscreen onto this specific monitor before presenting so the window
	 * maps already covering the right output. */
	gtk_window_fullscreen_on_monitor (GTK_WINDOW (self), monitor);

	return self;
}

GdkMonitor *
respite_overlay_get_monitor (RespiteOverlay *self)
{
	g_return_val_if_fail (RESPITE_IS_OVERLAY (self), NULL);

	return self->monitor;
}

/* Update the countdown to @seconds left in the break, formatted MM:SS. The
 * coordinator drives this from the timer's ::tick signal. */
void
respite_overlay_set_remaining (RespiteOverlay *self,
                               guint           seconds)
{
	g_autofree char *text = NULL;

	g_return_if_fail (RESPITE_IS_OVERLAY (self));

	/* TRANSLATORS: shown on the break overlay; %02u:%02u is MM:SS remaining. */
	text = g_strdup_printf (_("Break for %02u:%02u"), seconds / 60, seconds % 60);
	gtk_label_set_label (self->countdown_label, text);
}
