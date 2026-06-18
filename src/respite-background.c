/* respite-background.c
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

#include "respite-background.h"

#include <errno.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>

/* Background and autostart integration.
 *
 * Inside a Flatpak sandbox these are mediated by the XDG Background portal
 * (org.freedesktop.portal.Background): RequestBackground both asks to keep
 * running without a window and, optionally, registers an autostart entry that
 * relaunches the app at login. The reply is delivered asynchronously through
 * the org.freedesktop.portal.Request "Response" signal.
 */

#define PORTAL_BUS_NAME      "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH   "/org/freedesktop/portal/desktop"
#define PORTAL_BACKGROUND    "org.freedesktop.portal.Background"
#define PORTAL_REQUEST       "org.freedesktop.portal.Request"

gboolean
respite_background_is_sandboxed (void)
{
	return g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS);
}

/* An in-flight RequestBackground call: it owns the subscription on the
 * request's Response signal and is freed once that reply arrives (or the call
 * fails). When @callback is set the caller wants the granted autostart state
 * reported back; otherwise this is a plain run-in-background request. */
typedef struct
{
	GDBusConnection             *connection;
	char                        *request_path;
	gulong                       response_id;
	RespiteBackgroundAutostartCb callback;
	gpointer                     user_data;
} PortalRequest;

static void
portal_request_free (PortalRequest *req)
{
	if (req->response_id != 0)
		g_dbus_connection_signal_unsubscribe (req->connection, req->response_id);

	g_clear_object (&req->connection);
	g_free (req->request_path);
	g_free (req);
}

/* Report the outcome to an autostart caller (if any) and tear the request down.
 * @granted_autostart is meaningful only when the request actually succeeded. */
static void
portal_request_finish (PortalRequest *req,
                       gboolean       granted_autostart)
{
	if (req->callback != NULL)
		req->callback (granted_autostart, req->user_data);

	portal_request_free (req);
}

/* The portal answered. For an autostart request, report whether autostart was
 * actually granted; for a plain run-in-background request, just log a denial. */
static void
on_portal_response (GDBusConnection *connection,
                    const char      *sender_name,
                    const char      *object_path,
                    const char      *interface_name,
                    const char      *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
	PortalRequest *req = user_data;
	guint32 response = 0;
	g_autoptr(GVariant) results = NULL;
	gboolean autostart = FALSE;

	g_variant_get (parameters, "(u@a{sv})", &response, &results);

	if (response == 0 && results != NULL)
		g_variant_lookup (results, "autostart", "b", &autostart);

	if (response != 0)
		g_message ("Background portal request was denied or cancelled");

	portal_request_finish (req, response == 0 && autostart);
}

/* The RequestBackground method returned its request handle. Modern portals
 * route the Response to the path we predicted from our handle token; if an
 * older portal chose a different path, move the subscription there. */
static void
on_request_sent (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
	PortalRequest *req = user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) reply = NULL;
	const char *actual_path = NULL;

	reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

	if (reply == NULL)
	{
		g_warning ("Background portal request failed: %s", error->message);
		portal_request_finish (req, FALSE);
		return;
	}

	g_variant_get (reply, "(&o)", &actual_path);

	if (g_strcmp0 (actual_path, req->request_path) != 0)
	{
		g_dbus_connection_signal_unsubscribe (req->connection, req->response_id);
		g_free (req->request_path);
		req->request_path = g_strdup (actual_path);
		req->response_id =
			g_dbus_connection_signal_subscribe (req->connection,
			                                    PORTAL_BUS_NAME, PORTAL_REQUEST,
			                                    "Response", actual_path, NULL,
			                                    G_DBUS_SIGNAL_FLAGS_NONE,
			                                    on_portal_response, req, NULL);
	}
}

/* Issue a RequestBackground call. @reason is the user-visible justification the
 * portal shows. When @set_autostart is TRUE the call also registers (or, with
 * @autostart_enabled FALSE, removes) an autostart entry launching the daemon,
 * and @callback reports the granted state. Subscribes to the predicted Response
 * path before calling so the reply is never missed. */
static void
respite_portal_request_background (const char                   *reason,
                                   gboolean                      set_autostart,
                                   gboolean                      autostart_enabled,
                                   RespiteBackgroundAutostartCb  callback,
                                   gpointer                      user_data)
{
	GApplication *app = g_application_get_default ();
	GDBusConnection *connection = NULL;
	const char *unique_name;
	static guint token_counter = 0;
	g_autofree char *sender = NULL;
	g_autofree char *token = NULL;
	GVariantBuilder options;
	PortalRequest *req;

	if (app != NULL)
		connection = g_application_get_dbus_connection (app);

	if (connection == NULL)
	{
		g_warning ("No D-Bus connection; cannot reach the Background portal");
		if (callback != NULL)
			callback (FALSE, user_data);
		return;
	}

	unique_name = g_dbus_connection_get_unique_name (connection);
	if (unique_name == NULL)
	{
		if (callback != NULL)
			callback (FALSE, user_data);
		return;
	}

	/* The request object path the portal will use is derived from our bus name
	 * (with the leading ':' dropped and '.' turned into '_') and a token we
	 * pick, per the portal request convention. */
	sender = g_strdup (unique_name[0] == ':' ? unique_name + 1 : unique_name);
	g_strdelimit (sender, ".", '_');
	token = g_strdup_printf ("respite_%u", ++token_counter);

	req = g_new0 (PortalRequest, 1);
	req->connection = g_object_ref (connection);
	req->callback = callback;
	req->user_data = user_data;
	req->request_path = g_strdup_printf ("%s/request/%s/%s",
	                                     PORTAL_OBJECT_PATH, sender, token);
	req->response_id =
		g_dbus_connection_signal_subscribe (connection,
		                                    PORTAL_BUS_NAME, PORTAL_REQUEST,
		                                    "Response", req->request_path, NULL,
		                                    G_DBUS_SIGNAL_FLAGS_NONE,
		                                    on_portal_response, req, NULL);

	g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&options, "{sv}", "handle_token",
	                       g_variant_new_string (token));
	g_variant_builder_add (&options, "{sv}", "reason",
	                       g_variant_new_string (reason));

	if (set_autostart)
	{
		/* Launch the daemon directly at login rather than via D-Bus activation,
		 * which would start the app in its windowed mode instead. */
		static const char * const commandline[] = { "respite", "--daemon", NULL };

		g_variant_builder_add (&options, "{sv}", "autostart",
		                       g_variant_new_boolean (autostart_enabled));
		g_variant_builder_add (&options, "{sv}", "commandline",
		                       g_variant_new_strv (commandline, -1));
	}

	g_dbus_connection_call (connection,
	                        PORTAL_BUS_NAME, PORTAL_OBJECT_PATH, PORTAL_BACKGROUND,
	                        "RequestBackground",
	                        g_variant_new ("(sa{sv})", "", &options),
	                        G_VARIANT_TYPE ("(o)"),
	                        G_DBUS_CALL_FLAGS_NONE, -1, NULL,
	                        on_request_sent, req);
}

void
respite_background_request_run_in_background (void)
{
	/* Outside the sandbox a daemon process simply keeps running; only the
	 * portal needs to be asked for permission. */
	if (!respite_background_is_sandboxed ())
		return;

	respite_portal_request_background (
		_("Respite keeps reminding you to take breaks while running in the background."),
		FALSE, FALSE, NULL, NULL);
}

/* The XDG autostart entry used when running outside the sandbox. The desktop
 * id matches the app id so it sits beside other autostart entries cleanly. */
#define AUTOSTART_DESKTOP_ID "io.github.pawanrai9999.respite.desktop"

static char *
native_autostart_path (void)
{
	return g_build_filename (g_get_user_config_dir (), "autostart",
	                         AUTOSTART_DESKTOP_ID, NULL);
}

/* Outside the sandbox autostart is just an XDG .desktop file in the user's
 * autostart directory: write one to enable, delete it to disable. Reports the
 * state actually in effect, which on a failed write/delete is the prior one. */
static void
respite_set_native_autostart (gboolean                     enabled,
                              RespiteBackgroundAutostartCb callback,
                              gpointer                     user_data)
{
	g_autofree char *path = native_autostart_path ();
	gboolean in_effect;

	if (enabled)
	{
		g_autofree char *dir = g_path_get_dirname (path);
		g_autoptr(GKeyFile) keyfile = g_key_file_new ();
		g_autoptr(GError) error = NULL;

		g_key_file_set_string (keyfile, G_KEY_FILE_DESKTOP_GROUP,
		                       G_KEY_FILE_DESKTOP_KEY_TYPE, "Application");
		g_key_file_set_string (keyfile, G_KEY_FILE_DESKTOP_GROUP,
		                       G_KEY_FILE_DESKTOP_KEY_NAME, "Respite");
		/* Launch headless, mirroring the portal autostart commandline. */
		g_key_file_set_string (keyfile, G_KEY_FILE_DESKTOP_GROUP,
		                       G_KEY_FILE_DESKTOP_KEY_EXEC, "respite --daemon");
		g_key_file_set_boolean (keyfile, G_KEY_FILE_DESKTOP_GROUP,
		                        "X-GNOME-Autostart-enabled", TRUE);

		if (g_mkdir_with_parents (dir, 0700) != 0)
		{
			g_warning ("Could not create autostart directory %s: %s",
			           dir, g_strerror (errno));
			in_effect = FALSE;
		}
		else if (!g_key_file_save_to_file (keyfile, path, &error))
		{
			g_warning ("Could not write autostart file %s: %s",
			           path, error->message);
			in_effect = FALSE;
		}
		else
		{
			in_effect = TRUE;
		}
	}
	else
	{
		if (g_unlink (path) == 0 || errno == ENOENT)
		{
			in_effect = FALSE;
		}
		else
		{
			g_warning ("Could not remove autostart file %s: %s",
			           path, g_strerror (errno));
			in_effect = TRUE;
		}
	}

	if (callback != NULL)
		callback (in_effect, user_data);
}

void
respite_background_set_autostart (gboolean                     enabled,
                                  RespiteBackgroundAutostartCb callback,
                                  gpointer                     user_data)
{
	if (respite_background_is_sandboxed ())
		respite_portal_request_background (
			_("Respite starts at login so it can remind you to take breaks."),
			TRUE, enabled, callback, user_data);
	else
		respite_set_native_autostart (enabled, callback, user_data);
}
