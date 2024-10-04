/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2015-2017 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/meta-dbus-session-watcher.h"

#include <gio/gio.h>

#include "backends/meta-dbus-session-manager.h"

enum
{
  SESSION_SIGNAL_SESSION_CLOSED,

  N_SESSION_SIGNALS
};

static guint session_signals[N_SESSION_SIGNALS];

G_DEFINE_INTERFACE (MetaDbusSession, meta_dbus_session, G_TYPE_OBJECT)

struct _MetaDbusSessionWatcher
{
  GObject parent;

  GHashTable *clients;
};

G_DEFINE_TYPE (MetaDbusSessionWatcher,
               meta_dbus_session_watcher,
               G_TYPE_OBJECT)

typedef struct _MetaDbusSessionClient
{
  MetaDbusSessionWatcher *session_watcher;
  MetaDbusSession *session;
  char *dbus_name;
  guint name_watcher_id;
  GList *sessions;
} MetaDbusSessionClient;

static void
meta_dbus_session_client_destroy (MetaDbusSessionClient *client)
{
  while (TRUE)
    {
      GList *l;
      MetaDbusSession *session;

      l = client->sessions;
      if (!l)
        break;

      session = l->data;

      meta_dbus_session_close (session);
    }

  if (client->name_watcher_id)
    g_bus_unwatch_name (client->name_watcher_id);

  g_free (client->dbus_name);
  g_free (client);
}

static void
meta_dbus_session_watcher_destroy_client (MetaDbusSessionWatcher *session_watcher,
                                          MetaDbusSessionClient  *client)
{
  g_hash_table_remove (session_watcher->clients, client->dbus_name);
}

static void
name_vanished_callback (GDBusConnection *connection,
                        const char      *name,
                        gpointer         user_data)
{
  MetaDbusSessionClient *client = user_data;

  g_warning ("D-Bus client with active sessions vanished");

  client->name_watcher_id = 0;

  meta_dbus_session_watcher_destroy_client (client->session_watcher, client);
}

static MetaDbusSessionClient *
meta_dbus_session_client_new (MetaDbusSessionWatcher *session_watcher,
                              MetaDbusSession        *session,
                              const char             *dbus_name)
{
  GDBusInterfaceSkeleton *interface_skeleton =
    G_DBUS_INTERFACE_SKELETON (session);
  GDBusConnection *connection =
    g_dbus_interface_skeleton_get_connection (interface_skeleton);
  MetaDbusSessionClient *client;

  client = g_new0 (MetaDbusSessionClient, 1);
  client->session_watcher = session_watcher;
  client->session = session;
  client->dbus_name = g_strdup (dbus_name);

  client->name_watcher_id =
    g_bus_watch_name_on_connection (connection,
                                    dbus_name,
                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                    NULL,
                                    name_vanished_callback,
                                    client,
                                    NULL);

  return client;
}

static void
on_session_closed (MetaDbusSession       *session,
                   MetaDbusSessionClient *client)
{
  client->sessions = g_list_remove (client->sessions, session);

  if (!client->sessions)
    meta_dbus_session_watcher_destroy_client (client->session_watcher, client);
}

static void
meta_dbus_session_client_add_session (MetaDbusSessionClient *client,
                                      MetaDbusSession       *session)
{
  client->sessions = g_list_append (client->sessions, session);

  g_signal_connect (session, "session-closed",
                    G_CALLBACK (on_session_closed),
                    client);
}

static MetaDbusSessionClient *
meta_dbus_session_watcher_get_client (MetaDbusSessionWatcher *session_watcher,
                                      const char             *dbus_name)
{
  return g_hash_table_lookup (session_watcher->clients, dbus_name);
}

void
meta_dbus_session_watcher_watch_session (MetaDbusSessionWatcher *session_watcher,
                                         const char             *client_dbus_name,
                                         MetaDbusSession        *session)
{
  MetaDbusSessionClient *client;

  client = meta_dbus_session_watcher_get_client (session_watcher,
                                                 client_dbus_name);
  if (!client)
    {
      client = meta_dbus_session_client_new (session_watcher,
                                             session,
                                             client_dbus_name);
      g_hash_table_insert (session_watcher->clients,
                           g_strdup (client_dbus_name),
                           client);
    }

  meta_dbus_session_client_add_session (client, session);
}

void
meta_dbus_session_notify_closed (MetaDbusSession *session)
{
  g_signal_emit (session, session_signals[SESSION_SIGNAL_SESSION_CLOSED], 0);
}

void
meta_dbus_session_install_properties (GObjectClass *object_class,
                                      unsigned int  first_prop)
{
  g_object_class_override_property (object_class,
                                    first_prop + META_DBUS_SESSION_PROP_SESSION_MANAGER,
                                    "session-manager");
  g_object_class_override_property (object_class,
                                    first_prop + META_DBUS_SESSION_PROP_PEER_NAME,
                                    "peer-name");
  g_object_class_override_property (object_class,
                                    first_prop + META_DBUS_SESSION_PROP_ID,
                                    "id");
}

static void
meta_dbus_session_default_init (MetaDbusSessionInterface *iface)
{
  g_object_interface_install_property (
    iface,
    g_param_spec_object ("session-manager", NULL, NULL,
                         META_TYPE_DBUS_SESSION_MANAGER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));
  g_object_interface_install_property (
    iface,
    g_param_spec_string ("peer-name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));
  g_object_interface_install_property (
    iface,
    g_param_spec_string ("id", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  session_signals[SESSION_SIGNAL_SESSION_CLOSED] =
    g_signal_new ("session-closed",
                  G_TYPE_FROM_INTERFACE (iface),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_dbus_session_watcher_finalize (GObject *object)
{
  MetaDbusSessionWatcher *session_watcher = META_DBUS_SESSION_WATCHER (object);

  g_hash_table_destroy (session_watcher->clients);

  G_OBJECT_CLASS (meta_dbus_session_watcher_parent_class)->finalize (object);
}

static void
meta_dbus_session_watcher_init (MetaDbusSessionWatcher *session_watcher)
{
  session_watcher->clients =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           g_free,
                           (GDestroyNotify) meta_dbus_session_client_destroy);
}

static void
meta_dbus_session_watcher_class_init (MetaDbusSessionWatcherClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_dbus_session_watcher_finalize;
}

void
meta_dbus_session_close (MetaDbusSession *session)
{
  META_DBUS_SESSION_GET_IFACE (session)->close (session);
}

typedef struct _CloseClosure
{
  MetaDbusSession *session;
  glong stopped_handler_id;
  guint callback_id;
} CloseClosure;

static void
close_cb (gpointer user_data)
{
  CloseClosure *closure = user_data;

  g_signal_handler_disconnect (closure->session,
                               closure->stopped_handler_id);

  meta_dbus_session_close (closure->session);

  g_free (closure);
}

static void
on_session_stopped (MetaDbusSession *session,
                    CloseClosure    *closure)
{
  g_source_remove (closure->callback_id);
  g_free (closure);
}

void
meta_dbus_session_queue_close (MetaDbusSession *session)
{
  CloseClosure *closure;

  closure = g_new0 (CloseClosure, 1);
  closure->session = session;
  closure->stopped_handler_id = g_signal_connect (session,
                                                  "session-closed",
                                                  G_CALLBACK (on_session_stopped),
                                                  closure);
  closure->callback_id = g_idle_add_once (close_cb, closure);
}

MetaDbusSessionManager *
meta_dbus_session_manager (MetaDbusSessionManager *session)
{
  MetaDbusSessionManager *manager;

  g_object_get (session, "session-manager", &manager, NULL);

  return manager;
}

char *
meta_dbus_session_get_peer_name (MetaDbusSession *session)
{
  char *peer_name;

  g_object_get (session, "peer-name", &peer_name, NULL);

  return peer_name;
}

char *
meta_dbus_session_get_id (MetaDbusSession *session)
{
  char *id;

  g_object_get (session, "id", &id, NULL);

  return id;
}
