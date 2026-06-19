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
#include "respite-background.h"
#include "respite-overlay.h"
#include "respite-strict.h"
#include "respite-timer.h"
#include "respite-window.h"

struct _RespiteApplication
{
	AdwApplication parent_instance;

	/* The single work/break engine, shared across every entry point. */
	RespiteTimer  *timer;

	/* Client for the companion GNOME Shell extension that enforces strict
	 * breaks from inside the compositor. Always present; only acts when the
	 * extension is installed and the strict-break preference is on. */
	RespiteStrict *strict;

	/* TRUE while the current break is being enforced by the extension rather
	 * than the in-app GTK overlays. Determines where ticks are pushed, how the
	 * break is torn down, and whether a dropped grab triggers an overlay
	 * fallback. */
	gboolean       break_is_strict;

	/* App-level view of the preferences, used to act on the autostart toggle.
	 * @autostart_changed_id is the ::changed::autostart handler, blocked while
	 * we write the portal's verdict back so the reconciliation does not loop. */
	GSettings     *settings;
	gulong         autostart_changed_id;

	/* The break overlays currently on screen (one per monitor), live only
	 * for the duration of a break. Empty while working or idle. */
	GPtrArray     *overlays;

	/* ::items-changed handler on the monitor list, connected only while a
	 * break is on screen so hotplugged monitors get covered (and removed
	 * ones uncovered) mid-break. */
	gulong         monitors_changed_id;

	/* TRUE once we have taken a g_application_hold to stay alive headless. */
	gboolean       held;

	/* Non-zero while a break is on screen and we are holding a session
	 * inhibitor against suspend/idle, so the overlay is not cut short by the
	 * machine sleeping or the screensaver engaging. Zero when not inhibiting. */
	guint          inhibit_cookie;

	/* Owning list of media streams currently playing the break start/end
	 * cues. A stream is held here for the duration of its playback and dropped
	 * once it ends (or errors), so a sound is not cut off by losing its last
	 * reference and the app still owns it for teardown on quit. */
	GPtrArray     *sounds;
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
	                     "resource-base-path", "/io/github/pawanrai9999/respite",
	                     NULL);
}

/* Create one overlay covering @monitor, seed it with the current countdown so
 * it does not flash a stale value, and present it. */
static void
respite_application_add_overlay (RespiteApplication *self,
                                 GdkMonitor         *monitor)
{
	RespiteOverlay *overlay;

	overlay = respite_overlay_new (GTK_APPLICATION (self), monitor);
	respite_overlay_set_remaining (overlay,
	                               respite_timer_get_remaining (self->timer));

	g_ptr_array_add (self->overlays, overlay);
	gtk_window_present (GTK_WINDOW (overlay));
}

static gboolean
respite_application_monitor_has_overlay (RespiteApplication *self,
                                         GdkMonitor         *monitor)
{
	for (guint i = 0; i < self->overlays->len; i++)
		if (respite_overlay_get_monitor (self->overlays->pdata[i]) == monitor)
			return TRUE;

	return FALSE;
}

/* Reconcile the set of overlays against the current set of monitors: drop
 * overlays whose monitor has gone away, and add one for any monitor not yet
 * covered. Used both for the initial cover and on every hotplug mid-break. */
static void
respite_application_reconcile_overlays (RespiteApplication *self)
{
	GListModel *monitors = gdk_display_get_monitors (gdk_display_get_default ());
	guint n_monitors = g_list_model_get_n_items (monitors);

	/* Remove overlays for monitors no longer present. Walk backwards because
	 * each removal triggers the array free func and shifts later indices. */
	for (guint i = self->overlays->len; i > 0; i--)
	{
		GdkMonitor *monitor = respite_overlay_get_monitor (self->overlays->pdata[i - 1]);
		gboolean present = FALSE;

		for (guint j = 0; j < n_monitors && !present; j++)
		{
			GdkMonitor *current = g_list_model_get_item (monitors, j);

			present = (current == monitor);
			g_object_unref (current);
		}

		if (!present)
			g_ptr_array_remove_index (self->overlays, i - 1);
	}

	/* Add overlays for monitors that have appeared. */
	for (guint j = 0; j < n_monitors; j++)
	{
		GdkMonitor *monitor = g_list_model_get_item (monitors, j);

		if (!respite_application_monitor_has_overlay (self, monitor))
			respite_application_add_overlay (self, monitor);

		g_object_unref (monitor);
	}
}

/* Cover every monitor with its own break overlay, so the nudge is total on a
 * multi-head setup rather than leaving secondary displays usable. While the
 * break is up we track monitor hotplug so the cover stays complete. */
static void
respite_application_show_overlays (RespiteApplication *self)
{
	GListModel *monitors;

	if (self->overlays->len > 0)
		return;

	respite_application_reconcile_overlays (self);

	monitors = gdk_display_get_monitors (gdk_display_get_default ());
	self->monitors_changed_id =
		g_signal_connect_swapped (monitors, "items-changed",
		                          G_CALLBACK (respite_application_reconcile_overlays),
		                          self);
}

/* Tear down every break overlay and stop tracking hotplug. The array's free
 * func destroys each window. */
static void
respite_application_hide_overlays (RespiteApplication *self)
{
	if (self->monitors_changed_id != 0)
	{
		GListModel *monitors = gdk_display_get_monitors (gdk_display_get_default ());

		g_clear_signal_handler (&self->monitors_changed_id, monitors);
	}

	g_ptr_array_set_size (self->overlays, 0);
}

/* Hold a session inhibitor for the duration of a break so the overlay is not
 * interrupted by automatic suspend or the screensaver/idle dimming. Under
 * Flatpak this is routed through the Inhibit portal; idempotent. */
static void
respite_application_inhibit (RespiteApplication *self)
{
	if (self->inhibit_cookie != 0)
		return;

	self->inhibit_cookie =
		gtk_application_inhibit (GTK_APPLICATION (self), NULL,
		                         GTK_APPLICATION_INHIBIT_SUSPEND |
		                         GTK_APPLICATION_INHIBIT_IDLE,
		                         _("A break is in progress"));
}

/* Release the break-time inhibitor taken above. Idempotent, so it is safe to
 * call on break end and again during teardown. */
static void
respite_application_uninhibit (RespiteApplication *self)
{
	if (self->inhibit_cookie == 0)
		return;

	gtk_application_uninhibit (GTK_APPLICATION (self), self->inhibit_cookie);
	self->inhibit_cookie = 0;
}

/* Stable id so the warning notification is updated/replaced in place rather
 * than stacking, and can be withdrawn once it is no longer relevant. */
#define RESPITE_WARNING_NOTIFICATION_ID "respite-warning"

/* A break is approaching: raise a notification so the user has a chance to
 * react before every screen blacks out. When postpones remain, attach a
 * Postpone button (wired to app.postpone) and reflect how many are left. */
static void
respite_application_on_warning (RespiteApplication *self,
                                guint               postpones_remaining)
{
	g_autoptr(GNotification) notification = g_notification_new (_("Break coming up"));
	g_autofree char *body = NULL;

	if (postpones_remaining > 0)
	{
		/* TRANSLATORS: %u is the number of postpones still available. */
		body = g_strdup_printf (ngettext ("A break is about to start. You can postpone it %u more time.",
		                                  "A break is about to start. You can postpone it %u more times.",
		                                  postpones_remaining),
		                        postpones_remaining);
		g_notification_add_button (notification, _("Postpone"), "app.postpone");
	}
	else
	{
		body = g_strdup (_("A break is about to start."));
	}

	g_notification_set_body (notification, body);
	g_application_send_notification (G_APPLICATION (self),
	                                 RESPITE_WARNING_NOTIFICATION_ID, notification);
}

/* Resource paths of the break cues, bundled in the gresource. */
#define RESPITE_BREAK_START_SOUND "/io/github/pawanrai9999/respite/sounds/break_time_start_sound.mp3"
#define RESPITE_BREAK_END_SOUND   "/io/github/pawanrai9999/respite/sounds/break_time_end_sound.mp3"

/* A cue has finished (or failed): drop it from the owning list so the stream is
 * freed. The notify holds a reference across emission, so removing (and thus
 * unreffing) here is safe even when this is the last reference. */
static void
respite_application_sound_finished (GtkMediaStream *stream,
                                    GParamSpec     *pspec,
                                    gpointer        user_data)
{
	RespiteApplication *self = user_data;

	if (gtk_media_stream_get_ended (stream) ||
	    gtk_media_stream_get_error (stream) != NULL)
		g_ptr_array_remove (self->sounds, stream);
}

/* Play a break cue, honouring the sound-effects preference. The stream is kept
 * alive in @sounds until it ends, since a GtkMediaStream stops the moment its
 * last reference goes away. A missing audio backend or codec just fails the
 * stream quietly — the break itself is unaffected. */
static void
respite_application_play_sound (RespiteApplication *self,
                                const char         *resource_path)
{
	GtkMediaStream *stream;

	if (!g_settings_get_boolean (self->settings, "sound-effects"))
		return;

	stream = gtk_media_file_new_for_resource (resource_path);

	/* The array owns the reference returned by the constructor. */
	g_ptr_array_add (self->sounds, stream);

	g_signal_connect (stream, "notify::ended",
	                  G_CALLBACK (respite_application_sound_finished), self);
	g_signal_connect (stream, "notify::error",
	                  G_CALLBACK (respite_application_sound_finished), self);

	gtk_media_stream_play (stream);
}

/* Tear down whatever is covering the screen for the current break: end the
 * extension lockdown if it was driving this break, drop the GTK overlays, and
 * release the suspend/idle inhibitor. Idempotent, so it is safe on a normal
 * break end, on an early end (escape), on pause, and during shutdown. */
static void
respite_application_end_active_break (RespiteApplication *self)
{
	if (self->break_is_strict)
	{
		respite_strict_end_break (self->strict);
		self->break_is_strict = FALSE;
	}

	respite_application_hide_overlays (self);
	respite_application_uninhibit (self);
}

static void
respite_application_on_break_started (RespiteApplication *self)
{
	respite_application_play_sound (self, RESPITE_BREAK_START_SOUND);

	/* The break is here; the warning (and its Postpone offer) is moot. */
	g_application_withdraw_notification (G_APPLICATION (self),
	                                     RESPITE_WARNING_NOTIFICATION_ID);

	respite_application_inhibit (self);

	/* Prefer in-compositor enforcement when the user asked for it and the
	 * extension is present; otherwise fall back to the best-effort GTK
	 * overlays. A grab that cannot be taken is reported via ::released, which
	 * brings the overlays up for the rest of the break. */
	if (g_settings_get_boolean (self->settings, "strict-break") &&
	    respite_strict_is_available (self->strict))
	{
		self->break_is_strict = TRUE;
		respite_strict_start_break (self->strict,
		                            respite_timer_get_remaining (self->timer));
	}
	else
	{
		respite_application_show_overlays (self);
	}
}

static void
respite_application_on_break_ended (RespiteApplication *self)
{
	respite_application_end_active_break (self);
	respite_application_play_sound (self, RESPITE_BREAK_END_SOUND);
}

/* Push the countdown to the active enforcement: the extension when strict,
 * otherwise every live overlay. Outside a break neither is active, so
 * working-phase ticks harmlessly do nothing. */
static void
respite_application_on_tick (RespiteApplication *self,
                             guint               remaining)
{
	if (self->break_is_strict)
		respite_strict_update_remaining (self->strict, remaining);

	for (guint i = 0; i < self->overlays->len; i++)
		respite_overlay_set_remaining (self->overlays->pdata[i], remaining);
}

/* The extension lost (or never took) the grab mid-break. Fall back to the GTK
 * overlays for the remainder so the break is still enforced as best it can be. */
static void
respite_application_on_strict_released (RespiteApplication *self)
{
	if (!self->break_is_strict)
		return;

	self->break_is_strict = FALSE;

	if (respite_timer_get_state (self->timer) == RESPITE_TIMER_STATE_BREAK)
		respite_application_show_overlays (self);
}

/* The user held Esc inside the lockdown: end the break early through the timer
 * so phase/postpone state stays consistent (this in turn tears the break down
 * via ::break-ended). */
static void
respite_application_on_strict_escape (RespiteApplication *self)
{
	respite_timer_end_break (self->timer);
}

/* The portal (or native fallback) has settled on an autostart state, which may
 * differ from what was requested if the user denied it. Write the real state
 * back into GSettings so the toggle reflects reality, with our own ::changed
 * handler blocked so this correction does not trigger another request. */
static void
respite_application_autostart_settled (gboolean enabled,
                                       gpointer user_data)
{
	RespiteApplication *self = user_data;

	g_signal_handler_block (self->settings, self->autostart_changed_id);
	g_settings_set_boolean (self->settings, "autostart", enabled);
	g_signal_handler_unblock (self->settings, self->autostart_changed_id);
}

/* The autostart preference changed (from this window or another instance):
 * ask the background layer to enable or disable login launch accordingly. */
static void
respite_application_autostart_changed (RespiteApplication *self,
                                       const char         *key)
{
	gboolean enabled = g_settings_get_boolean (self->settings, "autostart");

	respite_background_set_autostart (enabled,
	                                  respite_application_autostart_settled,
	                                  self);
}

/* Create the shared timer once per primary instance, before any activation or
 * command line is handled, so every entry point operates on the same engine
 * regardless of whether the first launch was --daemon or a plain window. */
static void
respite_application_startup (GApplication *app)
{
	RespiteApplication *self = RESPITE_APPLICATION (app);

	G_APPLICATION_CLASS (respite_application_parent_class)->startup (app);

	self->settings = g_settings_new ("io.github.pawanrai9999.respite");
	self->autostart_changed_id =
		g_signal_connect_swapped (self->settings, "changed::autostart",
		                          G_CALLBACK (respite_application_autostart_changed),
		                          self);

	/* Reconcile the login-launch entry with the saved preference. We only ever
	 * write it in response to the toggle changing, so if the portal-managed
	 * autostart file is later lost (logout/cleanup/portal revalidation) the
	 * preference stays "on" while login start is silently broken. Inside the
	 * sandbox the file lives on the host and cannot be inspected directly, so we
	 * simply re-assert the request: it is idempotent and, for an already-granted
	 * permission, does not re-prompt. */
	if (g_settings_get_boolean (self->settings, "autostart"))
		respite_background_set_autostart (TRUE,
		                                  respite_application_autostart_settled,
		                                  self);

	self->timer = respite_timer_new ();

	g_signal_connect_swapped (self->timer, "warning",
	                          G_CALLBACK (respite_application_on_warning), self);
	g_signal_connect_swapped (self->timer, "break-started",
	                          G_CALLBACK (respite_application_on_break_started), self);
	g_signal_connect_swapped (self->timer, "break-ended",
	                          G_CALLBACK (respite_application_on_break_ended), self);
	g_signal_connect_swapped (self->timer, "tick",
	                          G_CALLBACK (respite_application_on_tick), self);

	self->strict = respite_strict_new ();

	g_signal_connect_swapped (self->strict, "released",
	                          G_CALLBACK (respite_application_on_strict_released), self);
	g_signal_connect_swapped (self->strict, "escape-requested",
	                          G_CALLBACK (respite_application_on_strict_escape), self);
}

/* Stop the engine and drop the daemon hold. Idempotent so it can run from
 * both the explicit quit action and the shutdown vfunc without double work. */
static void
respite_application_teardown (RespiteApplication *self)
{
	if (self->timer != NULL)
		respite_timer_stop (self->timer);

	respite_application_end_active_break (self);

	if (self->held)
	{
		g_application_release (G_APPLICATION (self));
		self->held = FALSE;
	}
}

/* Turn the break engine on or off. Enabling also promotes the process to a
 * persistent background daemon: it takes a g_application_hold (and asks the
 * portal to allow running windowless) so the timer keeps counting after the
 * settings window is closed. Pausing only stops the countdown; the hold is
 * kept so a paused daemon stays alive and ready to resume, and is released
 * only when the application actually quits. Idempotent either way. */
static void
respite_application_set_active (RespiteApplication *self,
                                gboolean            active)
{
	g_assert (RESPITE_IS_APPLICATION (self));

	if (active)
	{
		if (!self->held)
		{
			g_application_hold (G_APPLICATION (self));
			self->held = TRUE;

			/* Ask the portal to let us keep running with no window (sandbox only). */
			respite_background_request_run_in_background ();
		}

		respite_timer_start (self->timer);
	}
	else
	{
		respite_timer_stop (self->timer);

		/* Pausing mid-break must not leave the screen covered — and for a
		 * strict break it must release the compositor grab promptly rather than
		 * trapping the user behind a frozen countdown until the extension's
		 * watchdog fires. */
		respite_application_end_active_break (self);
	}
}

/* Enter background/daemon mode for a --daemon invocation: identical to the user
 * enabling the timer, so the process runs headless and persists. Idempotent,
 * so repeated --daemon launches collapse onto one hold. */
static void
respite_application_start_daemon (RespiteApplication *self)
{
	respite_application_set_active (self, TRUE);
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

/* Last chance to tear down cleanly however the process is ending (quit action,
 * window close, or signal); safe to repeat after the quit action ran. */
static void
respite_application_shutdown (GApplication *app)
{
	respite_application_teardown (RESPITE_APPLICATION (app));

	G_APPLICATION_CLASS (respite_application_parent_class)->shutdown (app);
}

static void
respite_application_dispose (GObject *object)
{
	RespiteApplication *self = RESPITE_APPLICATION (object);

	g_clear_pointer (&self->overlays, g_ptr_array_unref);
	g_clear_pointer (&self->sounds, g_ptr_array_unref);
	g_clear_object (&self->strict);
	g_clear_object (&self->timer);
	g_clear_object (&self->settings);

	G_OBJECT_CLASS (respite_application_parent_class)->dispose (object);
}

static void
respite_application_class_init (RespiteApplicationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

	object_class->dispose = respite_application_dispose;

	app_class->startup = respite_application_startup;
	app_class->shutdown = respite_application_shutdown;
	app_class->activate = respite_application_activate;
	app_class->command_line = respite_application_command_line;
}

RespiteTimer *
respite_application_get_timer (RespiteApplication *self)
{
	g_return_val_if_fail (RESPITE_IS_APPLICATION (self), NULL);

	return self->timer;
}

RespiteStrict *
respite_application_get_strict (RespiteApplication *self)
{
	g_return_val_if_fail (RESPITE_IS_APPLICATION (self), NULL);

	return self->strict;
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
	                       "application-name", _("Respite"),
	                       "application-icon", "io.github.pawanrai9999.respite",
	                       "developer-name", "pawan",
	                       "comments", _("A break reminder that blacks out the screen so you actually rest."),
	                       "website", "https://github.com/pawanrai9999/respite",
	                       "issue-url", "https://github.com/pawanrai9999/respite/issues",
	                       "license-type", GTK_LICENSE_AGPL_3_0,
	                       "translator-credits", _("translator-credits"),
	                       "version", PACKAGE_VERSION,
	                       "developers", developers,
	                       "copyright", "© 2026 pawan",
	                       NULL);
}

/* Activated from the warning notification's Postpone button: push the break
 * back if any allowance remains, then withdraw the notification either way so
 * it does not linger after being acted on. */
static void
respite_application_postpone_action (GSimpleAction *action,
                                     GVariant      *parameter,
                                     gpointer       user_data)
{
	RespiteApplication *self = user_data;

	g_assert (RESPITE_IS_APPLICATION (self));

	if (self->timer != NULL)
		respite_timer_postpone (self->timer);

	g_application_withdraw_notification (G_APPLICATION (self),
	                                     RESPITE_WARNING_NOTIFICATION_ID);
}

/* Flip the break engine between running and paused from the status control.
 * Resuming re-takes the daemon hold if needed so the timer survives the window
 * closing; pausing leaves the process alive but no longer counting down. */
static void
respite_application_toggle_timer_action (GSimpleAction *action,
                                         GVariant      *parameter,
                                         gpointer       user_data)
{
	RespiteApplication *self = user_data;
	gboolean running;

	g_assert (RESPITE_IS_APPLICATION (self));

	running = respite_timer_get_state (self->timer) != RESPITE_TIMER_STATE_IDLE;
	respite_application_set_active (self, !running);
}

static void
respite_application_quit_action (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
	RespiteApplication *self = user_data;

	g_assert (RESPITE_IS_APPLICATION (self));

	respite_application_teardown (self);
	g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
	{ "quit", respite_application_quit_action },
	{ "about", respite_application_about_action },
	{ "postpone", respite_application_postpone_action },
	{ "toggle-timer", respite_application_toggle_timer_action },
};

static const GOptionEntry app_options[] = {
	{ "daemon", 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, NULL,
	  N_("Run in the background without showing a window"), NULL },
	{ NULL },
};

static void
respite_application_init (RespiteApplication *self)
{
	/* Overlays are destroyed (not just unreffed) on removal, so the array's
	 * free func tears down each window. */
	self->overlays = g_ptr_array_new_with_free_func ((GDestroyNotify) gtk_window_destroy);
	self->sounds = g_ptr_array_new_with_free_func (g_object_unref);

	g_application_add_main_option_entries (G_APPLICATION (self), app_options);

	g_action_map_add_action_entries (G_ACTION_MAP (self),
	                                 app_actions,
	                                 G_N_ELEMENTS (app_actions),
	                                 self);
	gtk_application_set_accels_for_action (GTK_APPLICATION (self),
	                                       "app.quit",
	                                       (const char *[]) { "<control>q", NULL });
}
