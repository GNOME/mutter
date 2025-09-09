/*
 * Copyright (C) 2015-2017, 2022 Red Hat Inc.
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

#include "backends/meta-dbus-session-manager.h"

#include <gobject/gvaluecollector.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-dbus-session-watcher.h"

enum
{
  ENABLED,
  DISABLED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

enum
{
  PROP_0,

  PROP_BACKEND,
  PROP_SERVICE_NAME,
  PROP_SERVICE_PATH,
  PROP_SESSION_GTYPE,
  PROP_INTERFACE_SKELETON,

  N_PROPS
};

static GParamSpec *obj_props[N_PROPS];

typedef struct _MetaDbusSessionManagerPrivate
{
  MetaBackend *backend;
  char *service_name;
  char *service_path;

  GType session_gtype;

  guint dbus_name_id;
  GDBusInterfaceSkeleton *interface_skeleton;

  gboolean is_enabled;

  int inhibit_count;

  GHashTable *sessions;
} MetaDbusSessionManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (MetaDbusSessionManager,
                            meta_dbus_session_manager,
                            G_TYPE_OBJECT)

static void
on_prepare_shutdown (MetaBackend            *backend,
                     MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, priv->sessions);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      MetaDbusSession *session = META_DBUS_SESSION (value);

      g_hash_table_iter_steal (&iter);
      meta_dbus_session_close (session);
    }
}

static void
meta_dbus_session_manager_finalize (GObject *object)
{
  MetaDbusSessionManager *session_manager = META_DBUS_SESSION_MANAGER (object);
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  g_clear_handle_id (&priv->dbus_name_id, g_bus_unown_name);

  g_assert (g_hash_table_size (priv->sessions) == 0);
  g_hash_table_destroy (priv->sessions);

  g_clear_pointer (&priv->service_name, g_free);
  g_clear_pointer (&priv->service_path, g_free);

  g_clear_object (&priv->interface_skeleton);

  G_OBJECT_CLASS (meta_dbus_session_manager_parent_class)->finalize (object);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (user_data);
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);
  g_autoptr (GError) error = NULL;

  meta_topic (META_DEBUG_BACKEND,
              "Acquired D-Bus name '%s', exporting service on '%s'",
              priv->service_name, priv->service_path);

  if (!g_dbus_interface_skeleton_export (priv->interface_skeleton,
                                         connection,
                                         priv->service_path,
                                         &error))
    {
      g_warning ("Failed to export '%s' object on '%s': %s",
                 priv->service_name,
                 priv->service_path,
                 error->message);
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (user_data);
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  meta_topic (META_DEBUG_DBUS, "Acquired name %s", name);

  priv->is_enabled = TRUE;

  g_signal_emit (session_manager, signals[ENABLED], 0);
}

static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  MetaDbusSessionManager *session_manager =
    META_DBUS_SESSION_MANAGER (user_data);
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  meta_topic (META_DEBUG_DBUS, "Lost or failed to acquire name %s", name);

  priv->is_enabled = FALSE;

  g_signal_emit (session_manager, signals[DISABLED], 0);
}

static void
meta_dbus_session_manager_constructed (GObject *object)
{
  MetaDbusSessionManager *session_manager = META_DBUS_SESSION_MANAGER (object);
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  priv->dbus_name_id =
    g_bus_own_name (G_BUS_TYPE_SESSION,
                    priv->service_name,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    on_bus_acquired,
                    on_name_acquired,
                    on_name_lost,
                    session_manager,
                    NULL);

  g_signal_connect (priv->backend, "prepare-shutdown",
                    G_CALLBACK (on_prepare_shutdown),
                    session_manager);

  G_OBJECT_CLASS (meta_dbus_session_manager_parent_class)->constructed (object);
}

static void
meta_dbus_session_manager_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  MetaDbusSessionManager *session_manager = META_DBUS_SESSION_MANAGER (object);
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      priv->backend = g_value_get_object (value);
      break;
    case PROP_SERVICE_NAME:
      priv->service_name = g_value_dup_string (value);
      break;
    case PROP_SERVICE_PATH:
      priv->service_path = g_value_dup_string (value);
      break;
    case PROP_SESSION_GTYPE:
      priv->session_gtype = g_value_get_gtype (value);
      break;
    case PROP_INTERFACE_SKELETON:
      priv->interface_skeleton = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_dbus_session_manager_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  MetaDbusSessionManager *session_manager = META_DBUS_SESSION_MANAGER (object);
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  switch (prop_id)
    {
    case PROP_BACKEND:
      g_value_set_object (value, priv->backend);
      break;
    case PROP_SERVICE_NAME:
      g_value_set_string (value, priv->service_name);
      break;
    case PROP_SERVICE_PATH:
      g_value_set_string (value, priv->service_path);
      break;
    case PROP_SESSION_GTYPE:
      g_value_set_gtype (value, priv->session_gtype);
      break;
    case PROP_INTERFACE_SKELETON:
      g_value_set_object (value, priv->interface_skeleton);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_dbus_session_manager_class_init (MetaDbusSessionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_dbus_session_manager_finalize;
  object_class->constructed = meta_dbus_session_manager_constructed;
  object_class->set_property = meta_dbus_session_manager_set_property;
  object_class->get_property = meta_dbus_session_manager_get_property;

  obj_props[PROP_BACKEND] =
    g_param_spec_object ("backend", NULL, NULL,
                         META_TYPE_BACKEND,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SERVICE_NAME] =
    g_param_spec_string ("service-name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SERVICE_PATH] =
    g_param_spec_string ("service-path", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  obj_props[PROP_SESSION_GTYPE] =
    g_param_spec_gtype ("session-gtype", NULL, NULL,
                        G_TYPE_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  obj_props[PROP_INTERFACE_SKELETON] =
    g_param_spec_object ("interface-skeleton", NULL, NULL,
                         G_TYPE_DBUS_INTERFACE_SKELETON,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties (object_class, N_PROPS, obj_props);

  signals[ENABLED] = g_signal_new ("enabled",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0,
                                   NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);
  signals[DISABLED] = g_signal_new ("disabled",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL, NULL, NULL,
                                    G_TYPE_NONE, 0);
}

static void
meta_dbus_session_manager_init (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  priv->sessions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
on_session_closed (MetaDbusSession        *session,
                   MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);
  g_autofree char *session_id = NULL;

  session_id = meta_dbus_session_get_id (session);
  g_hash_table_remove (priv->sessions, session_id);
}

static char *
generate_session_id (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);
  char *session_id;

  while (TRUE)
    {
      session_id = g_uuid_string_random ();
      if (g_hash_table_lookup (priv->sessions, session_id))
        g_free (session_id);
      else
        return session_id;
    }
}

static void
append_property (GObjectClass *object_class,
                 GArray       *names,
                 GArray       *values,
                 const char   *name,
                 ...)
{
  va_list var_args;
  GValue value = G_VALUE_INIT;
  GParamSpec *pspec;
  GType ptype;
  char *error = NULL;

  va_start (var_args, name);

  pspec = g_object_class_find_property (object_class, name);
  g_assert (pspec);

  ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);
  G_VALUE_COLLECT_INIT (&value, ptype, var_args, 0, &error);
  g_assert (!error);

  g_array_append_val (names, name);
  g_array_append_val (values, value);
  va_end (var_args);
}

MetaDbusSession *
meta_dbus_session_manager_create_session (MetaDbusSessionManager  *session_manager,
                                          GDBusMethodInvocation   *invocation,
                                          GError                 **error,
                                          ...)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);
  MetaDbusSessionWatcher *session_watcher =
    meta_backend_get_dbus_session_watcher (priv->backend);
  GObject *session;
  va_list var_args;
  GObjectClass *object_class;
  g_autoptr (GArray) names = NULL;
  g_autoptr (GArray) values = NULL;
  const char *property_name;
  const char *peer_name;
  g_autofree char *session_id = NULL;
  const char *client_dbus_name;

  if (priv->inhibit_count > 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Session creation inhibited");
      return NULL;
    }

  peer_name = g_dbus_method_invocation_get_sender (invocation);
  object_class = g_type_class_ref (priv->session_gtype);

  va_start (var_args, error);
  names = g_array_new (FALSE, FALSE, sizeof (const char *));
  values = g_array_new (FALSE, FALSE, sizeof (GValue));
  g_array_set_clear_func (values, (GDestroyNotify) g_value_unset);

  property_name = va_arg (var_args, const char *);
  while (property_name)
    {
      GValue value = G_VALUE_INIT;
      GParamSpec *pspec;
      GType ptype;
      gchar *error_message = NULL;

      pspec = g_object_class_find_property (object_class,
                                            property_name);
      g_assert (pspec);

      ptype = G_PARAM_SPEC_VALUE_TYPE (pspec);
      G_VALUE_COLLECT_INIT (&value, ptype, var_args, 0, &error_message);
      g_assert (!error_message);

      g_array_append_val (names, property_name);
      g_array_append_val (values, value);

      property_name = va_arg (var_args, const char *);
    }

  va_end (var_args);

  append_property (object_class, names, values, "session-manager",
                   session_manager);
  append_property (object_class, names, values, "peer-name", peer_name);

  session_id = generate_session_id (session_manager);
  append_property (object_class, names, values, "id", session_id);

  g_type_class_unref (object_class);

  session = g_object_new_with_properties (priv->session_gtype,
                                          values->len,
                                          (const char **) names->data,
                                          (const GValue *) values->data);
  if (!g_initable_init (G_INITABLE (session), NULL, error))
    {
      g_object_unref (session);
      return NULL;
    }

  g_hash_table_insert (priv->sessions,
                       g_strdup (session_id),
                       session);

  client_dbus_name = g_dbus_method_invocation_get_sender (invocation);
  meta_dbus_session_watcher_watch_session (session_watcher,
                                           client_dbus_name,
                                           META_DBUS_SESSION (session));

  g_signal_connect (session, "session-closed",
                    G_CALLBACK (on_session_closed),
                    session_manager);

  return META_DBUS_SESSION (session);
}

MetaDbusSession *
meta_dbus_session_manager_get_session (MetaDbusSessionManager *session_manager,
                                       const char             *session_id)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  return g_hash_table_lookup (priv->sessions, session_id);
}

void
meta_dbus_session_manager_inhibit (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  priv->inhibit_count++;
  if (priv->inhibit_count == 1)
    {
      GHashTableIter iter;
      gpointer key, value;

      g_hash_table_iter_init (&iter, priv->sessions);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          MetaDbusSession *session = META_DBUS_SESSION (value);

          g_hash_table_iter_steal (&iter);
          meta_dbus_session_close (session);
        }
    }
}

void
meta_dbus_session_manager_uninhibit (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  g_return_if_fail (priv->inhibit_count > 0);

  priv->inhibit_count--;
}

MetaBackend *
meta_dbus_session_manager_get_backend (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  return priv->backend;
}

GDBusConnection *
meta_dbus_session_manager_get_connection (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  return g_dbus_interface_skeleton_get_connection (priv->interface_skeleton);
}

GDBusInterfaceSkeleton *
meta_dbus_session_manager_get_interface_skeleton (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  return priv->interface_skeleton;
}

size_t
meta_dbus_session_manager_get_num_sessions (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  return g_hash_table_size (priv->sessions);
}

gboolean
meta_dbus_session_manager_is_enabled (MetaDbusSessionManager *session_manager)
{
  MetaDbusSessionManagerPrivate *priv =
    meta_dbus_session_manager_get_instance_private (session_manager);

  return priv->is_enabled;
}

MetaDbusSessionManager *
meta_dbus_session_manager_new (MetaBackend            *backend,
                               const char             *service_name,
                               const char             *service_path,
                               GType                   session_gtype,
                               GDBusInterfaceSkeleton *skeleton)
{
  return g_object_new (META_TYPE_DBUS_SESSION_MANAGER,
                       "backend", backend,
                       "service-name", service_name,
                       "service-path", service_path,
                       "session-gtype", session_gtype,
                       "interface-skeleton", skeleton,
                       NULL);
}
