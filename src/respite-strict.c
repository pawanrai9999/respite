/* respite-strict.c
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

#include "respite-strict.h"

#include <gio/gio.h>

/* Talks to the companion GNOME Shell extension over the session bus. The
 * extension owns a well-known name and exports the lockdown interface (see
 * extension/dbus-interface.xml, the shared source of truth); we watch that
 * name for presence and issue fire-and-forget StartBreak/UpdateRemaining/
 * EndBreak calls, mirroring the raw-GDBus style of respite-background.c.
 *
 * The Strict name lives under the application's own app-id prefix, so the
 * Flatpak sandbox's D-Bus filter lets the daemon reach it without extra
 * permissions (the same way the Background portal calls already work). */

#define STRICT_BUS_NAME    "io.github.pawanrai9999.respite.Strict"
#define STRICT_OBJECT_PATH "/io/github/pawanrai9999/respite/Strict"
#define STRICT_INTERFACE   "io.github.pawanrai9999.respite.Strict"

struct _RespiteStrict
{
	GObject          parent_instance;

	/* Watch on STRICT_BUS_NAME; drives availability as the extension is
	 * enabled/disabled while the daemon runs. */
	guint            watch_id;

	/* The session bus connection, borrowed from the watch's name-appeared
	 * callback and held only while the extension owns the name. NULL when the
	 * extension is absent, which is exactly when calls must be suppressed. */
	GDBusConnection *connection;

	guint            released_sub_id;
	guint            escape_sub_id;

	gboolean         available;
};

G_DEFINE_FINAL_TYPE (RespiteStrict, respite_strict, G_TYPE_OBJECT)

enum
{
	SIGNAL_AVAILABILITY_CHANGED,
	SIGNAL_RELEASED,
	SIGNAL_ESCAPE_REQUESTED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
on_released (GDBusConnection *connection,
             const char      *sender_name,
             const char      *object_path,
             const char      *interface_name,
             const char      *signal_name,
             GVariant        *parameters,
             gpointer         user_data)
{
	RespiteStrict *self = user_data;

	g_signal_emit (self, signals[SIGNAL_RELEASED], 0);
}

static void
on_escape_requested (GDBusConnection *connection,
                     const char      *sender_name,
                     const char      *object_path,
                     const char      *interface_name,
                     const char      *signal_name,
                     GVariant        *parameters,
                     gpointer         user_data)
{
	RespiteStrict *self = user_data;

	g_signal_emit (self, signals[SIGNAL_ESCAPE_REQUESTED], 0);
}

/* The extension appeared: remember its connection, subscribe to its signals,
 * and mark strict enforcement available. */
static void
on_name_appeared (GDBusConnection *connection,
                  const char      *name,
                  const char      *name_owner,
                  gpointer         user_data)
{
	RespiteStrict *self = user_data;

	g_set_object (&self->connection, connection);

	self->released_sub_id =
		g_dbus_connection_signal_subscribe (connection,
		                                    STRICT_BUS_NAME, STRICT_INTERFACE,
		                                    "Released", STRICT_OBJECT_PATH, NULL,
		                                    G_DBUS_SIGNAL_FLAGS_NONE,
		                                    on_released, self, NULL);
	self->escape_sub_id =
		g_dbus_connection_signal_subscribe (connection,
		                                    STRICT_BUS_NAME, STRICT_INTERFACE,
		                                    "EscapeRequested", STRICT_OBJECT_PATH, NULL,
		                                    G_DBUS_SIGNAL_FLAGS_NONE,
		                                    on_escape_requested, self, NULL);

	if (!self->available)
	{
		self->available = TRUE;
		g_signal_emit (self, signals[SIGNAL_AVAILABILITY_CHANGED], 0, TRUE);
	}
}

/* The extension vanished (disabled, crashed, or the bus went away): drop the
 * subscriptions and connection and mark enforcement unavailable. */
static void
on_name_vanished (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
	RespiteStrict *self = user_data;

	if (self->connection != NULL)
	{
		if (self->released_sub_id != 0)
			g_dbus_connection_signal_unsubscribe (self->connection, self->released_sub_id);
		if (self->escape_sub_id != 0)
			g_dbus_connection_signal_unsubscribe (self->connection, self->escape_sub_id);
	}
	self->released_sub_id = 0;
	self->escape_sub_id = 0;
	g_clear_object (&self->connection);

	if (self->available)
	{
		self->available = FALSE;
		g_signal_emit (self, signals[SIGNAL_AVAILABILITY_CHANGED], 0, FALSE);
	}
}

gboolean
respite_strict_is_available (RespiteStrict *self)
{
	g_return_val_if_fail (RESPITE_IS_STRICT (self), FALSE);

	return self->available;
}

/* The reply to StartBreak: the extension returns FALSE when it could not take
 * the modal grab (another system modal already holds the shell). Either way an
 * error reaching the extension is treated as enforcement having failed, so the
 * caller falls back to its own overlay. */
static void
on_start_break_reply (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
	RespiteStrict *self = user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) reply = NULL;
	gboolean started = FALSE;

	reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);

	if (reply != NULL)
		g_variant_get (reply, "(b)", &started);
	else
		g_warning ("Strict StartBreak call failed: %s", error->message);

	if (!started)
		g_signal_emit (self, signals[SIGNAL_RELEASED], 0);

	g_object_unref (self);
}

void
respite_strict_start_break (RespiteStrict *self,
                            guint          seconds)
{
	g_return_if_fail (RESPITE_IS_STRICT (self));

	if (self->connection == NULL)
		return;

	g_dbus_connection_call (self->connection,
	                        STRICT_BUS_NAME, STRICT_OBJECT_PATH, STRICT_INTERFACE,
	                        "StartBreak",
	                        g_variant_new ("(u)", seconds),
	                        G_VARIANT_TYPE ("(b)"),
	                        G_DBUS_CALL_FLAGS_NONE, -1, NULL,
	                        on_start_break_reply, g_object_ref (self));
}

void
respite_strict_update_remaining (RespiteStrict *self,
                                 guint          seconds)
{
	g_return_if_fail (RESPITE_IS_STRICT (self));

	if (self->connection == NULL)
		return;

	g_dbus_connection_call (self->connection,
	                        STRICT_BUS_NAME, STRICT_OBJECT_PATH, STRICT_INTERFACE,
	                        "UpdateRemaining",
	                        g_variant_new ("(u)", seconds),
	                        NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

void
respite_strict_end_break (RespiteStrict *self)
{
	g_return_if_fail (RESPITE_IS_STRICT (self));

	if (self->connection == NULL)
		return;

	g_dbus_connection_call (self->connection,
	                        STRICT_BUS_NAME, STRICT_OBJECT_PATH, STRICT_INTERFACE,
	                        "EndBreak",
	                        NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

RespiteStrict *
respite_strict_new (void)
{
	return g_object_new (RESPITE_TYPE_STRICT, NULL);
}

static void
respite_strict_dispose (GObject *object)
{
	RespiteStrict *self = RESPITE_STRICT (object);

	g_clear_handle_id (&self->watch_id, g_bus_unwatch_name);

	if (self->connection != NULL)
	{
		if (self->released_sub_id != 0)
			g_dbus_connection_signal_unsubscribe (self->connection, self->released_sub_id);
		if (self->escape_sub_id != 0)
			g_dbus_connection_signal_unsubscribe (self->connection, self->escape_sub_id);
		self->released_sub_id = 0;
		self->escape_sub_id = 0;
	}
	g_clear_object (&self->connection);

	G_OBJECT_CLASS (respite_strict_parent_class)->dispose (object);
}

static void
respite_strict_class_init (RespiteStrictClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = respite_strict_dispose;

	/**
	 * RespiteStrict::availability-changed:
	 * @self: the strict client
	 * @available: whether the extension now owns its bus name
	 */
	signals[SIGNAL_AVAILABILITY_CHANGED] =
		g_signal_new ("availability-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	/**
	 * RespiteStrict::released:
	 * @self: the strict client
	 *
	 * Enforcement dropped or could not be taken; fall back to the GTK overlay.
	 */
	signals[SIGNAL_RELEASED] =
		g_signal_new ("released",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * RespiteStrict::escape-requested:
	 * @self: the strict client
	 *
	 * The user invoked the in-shell escape hatch; end the break early.
	 */
	signals[SIGNAL_ESCAPE_REQUESTED] =
		g_signal_new ("escape-requested",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);
}

static void
respite_strict_init (RespiteStrict *self)
{
	/* Watch (do not auto-start: the extension cannot be D-Bus activated) so the
	 * toggle reflects the extension being enabled or disabled live. */
	self->watch_id =
		g_bus_watch_name (G_BUS_TYPE_SESSION, STRICT_BUS_NAME,
		                  G_BUS_NAME_WATCHER_FLAGS_NONE,
		                  on_name_appeared, on_name_vanished,
		                  self, NULL);
}
