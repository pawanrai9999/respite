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

/* Refuse to close. Together with deletable=false this stops the break from
 * being dismissed via window controls or the usual close shortcuts. It is a
 * nudge, not a lock: see the focus-assertion note below. */
static gboolean
respite_overlay_close_request (GtkWindow *window)
{
	return GDK_EVENT_STOP;
}

/* Best-effort focus assertion. When the overlay loses focus we ask to be
 * presented again. On Wayland under Mutter, a focus request without a valid
 * activation token only flags the window as demanding attention rather than
 * stealing focus, so this re-assert cannot trap the user: switching workspace
 * or app away from the break still works. We make the break visually total and
 * re-request focus; escaping it remains possible by design. */
static void
respite_overlay_notify_is_active (RespiteOverlay *self)
{
	/* Skip while the window is being torn down: destroying it unmaps it,
	 * which flips is-active, and re-presenting here would fight the teardown. */
	if (gtk_widget_in_destruction (GTK_WIDGET (self)))
		return;

	if (!gtk_window_is_active (GTK_WINDOW (self)))
		gtk_window_present (GTK_WINDOW (self));
}

/* Pull keyboard focus to the overlay as soon as it is shown. */
static void
respite_overlay_map (GtkWidget *widget)
{
	GTK_WIDGET_CLASS (respite_overlay_parent_class)->map (widget);

	gtk_widget_grab_focus (widget);
}

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
	g_autoptr(GtkCssProvider) provider = gtk_css_provider_new ();

	object_class->dispose = respite_overlay_dispose;
	widget_class->map = respite_overlay_map;

	gtk_widget_class_set_template_from_resource (widget_class, "/com/texoviva/respite/respite-overlay.ui");
	gtk_widget_class_bind_template_child (widget_class, RespiteOverlay, countdown_label);

	/* Load the overlay stylesheet once, when the type is first used, and apply
	 * it display-wide; the .respite-overlay scoping keeps it off other widgets. */
	gtk_css_provider_load_from_resource (provider, "/com/texoviva/respite/respite-overlay.css");
	gtk_style_context_add_provider_for_display (gdk_display_get_default (),
	                                            GTK_STYLE_PROVIDER (provider),
	                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
respite_overlay_init (RespiteOverlay *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	g_signal_connect (self, "close-request",
	                  G_CALLBACK (respite_overlay_close_request), NULL);
	g_signal_connect (self, "notify::is-active",
	                  G_CALLBACK (respite_overlay_notify_is_active), NULL);
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
