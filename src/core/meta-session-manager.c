/*
 * Copyright (C) 2024 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include "core/meta-session-manager.h"

#include <fcntl.h>
#include <gio/gunixinputstream.h>
#include <glib/gstdio.h>
#include <gvdb/gvdb-builder.h>
#include <gvdb/gvdb-reader.h>

#define SESSION_FILE_NAME "session.gvdb"

typedef struct _MetaSessionData MetaSessionData;

struct _MetaSessionData
{
  const char *name;
  GHashTable *new_table; /* Gvdb table */
  GHashTable *deleted_sessions; /* Set of session names */
  GvdbTable *gvdb_table;
};

struct _MetaSessionManager
{
  GObject parent_instance;
  GMutex mutex;
  GHashTable *sessions; /* Session name -> MetaSessionState */
  GHashTable *deleted_sessions; /* Set of session names */
  GvdbTable *gvdb_table;
  char *name;
  int fd;
  GMappedFile *mapped_file;
};

enum
{
  PROP_0,
  PROP_NAME,
  PROP_FD,
  N_PROPS,
};

static GParamSpec *props[N_PROPS] = { 0, };

enum
{
  SESSION_INSTANTIATED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS] = { 0, };

#define MAX_SIZE (10 * 1024 * 1024)

static void meta_session_manager_initable_iface_init (GInitableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (MetaSessionManager, meta_session_manager,
                               G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                      meta_session_manager_initable_iface_init))

static void
meta_session_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  MetaSessionManager *session_manager = META_SESSION_MANAGER (object);

  switch (prop_id)
    {
    case PROP_NAME:
      session_manager->name = g_value_dup_string (value);
      break;
    case PROP_FD:
      session_manager->fd = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_session_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  MetaSessionManager *session_manager = META_SESSION_MANAGER (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, session_manager->name);
      break;
    case PROP_FD:
      g_value_set_int (value, session_manager->fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
meta_session_manager_finalize (GObject *object)
{
  MetaSessionManager *session_manager = META_SESSION_MANAGER (object);

  g_clear_pointer (&session_manager->sessions, g_hash_table_unref);
  g_clear_pointer (&session_manager->deleted_sessions, g_hash_table_unref);
  g_clear_pointer (&session_manager->mapped_file, g_mapped_file_unref);
  g_clear_pointer (&session_manager->gvdb_table, gvdb_table_free);
  g_clear_pointer (&session_manager->name, g_free);
  g_clear_fd (&session_manager->fd, NULL);
  g_mutex_clear (&session_manager->mutex);

  G_OBJECT_CLASS (meta_session_manager_parent_class)->finalize (object);
}

static void
meta_session_manager_class_init (MetaSessionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = meta_session_manager_set_property;
  object_class->get_property = meta_session_manager_get_property;
  object_class->finalize = meta_session_manager_finalize;

  signals[SESSION_INSTANTIATED] =
    g_signal_new ("session-instantiated",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 2,
                  G_TYPE_STRING,
                  META_TYPE_SESSION_STATE);

  props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY);
  props[PROP_FD] =
    g_param_spec_int ("fd", NULL, NULL,
                      G_MININT, G_MAXINT, -1,
                      G_PARAM_READWRITE |
                      G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
meta_session_manager_init (MetaSessionManager *session_manager)
{
  session_manager->fd = -1;
  session_manager->sessions =
    g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
  session_manager->deleted_sessions =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_mutex_init (&session_manager->mutex);
}

static gboolean
meta_session_manager_initable_init (GInitable     *initable,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
  MetaSessionManager *manager = META_SESSION_MANAGER (initable);
  g_autoptr (GBytes) bytes = NULL;

  if (manager->name && manager->fd < 0)
    {
      g_autofree char *session_dir = NULL, *session_file = NULL;

      session_dir = g_build_filename (g_get_user_data_dir (),
                                      manager->name, NULL);

      if (g_mkdir_with_parents (session_dir, 0700) < 0)
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "Could not create directory for session data: %m");
          return FALSE;
        }

      session_file = g_build_filename (session_dir,
                                       SESSION_FILE_NAME,
                                       NULL);

      manager->fd = open (session_file,
                          O_CREAT | O_RDWR | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    }

  if (manager->fd < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Error opening session file: %m");
      return FALSE;
    }

  manager->mapped_file = g_mapped_file_new_from_fd (manager->fd, TRUE, error);
  if (!manager->mapped_file)
    return FALSE;

  if (g_mapped_file_get_length (manager->mapped_file) > 0)
    {
      bytes = g_mapped_file_get_bytes (manager->mapped_file);
      manager->gvdb_table = gvdb_table_new_from_bytes (bytes, FALSE, error);
      if (!manager->gvdb_table)
        return FALSE;
    }

  return TRUE;
}

static void
meta_session_manager_initable_iface_init (GInitableIface *iface)
{
  iface->init = meta_session_manager_initable_init;
}

MetaSessionManager *
meta_session_manager_new (const gchar  *name,
                          GError      **error)
{
  return g_initable_new (META_TYPE_SESSION_MANAGER, NULL, error,
                         "name", name,
                         NULL);
}

MetaSessionManager *
meta_session_manager_new_for_fd (const gchar  *name,
                                 int           fd,
                                 GError      **error)
{
  return g_initable_new (META_TYPE_SESSION_MANAGER,
                         NULL, error,
                         "name", name,
                         "fd", fd,
                         NULL);
}

int
meta_session_manager_get_fd (MetaSessionManager *manager)
{
  return manager->fd;
}

gboolean
meta_session_manager_get_session_exists (MetaSessionManager *manager,
                                         const char         *name)
{
  if (g_hash_table_contains (manager->sessions, name))
    return TRUE;

  if (g_hash_table_contains (manager->deleted_sessions, name))
    return FALSE;

  if (manager->gvdb_table)
    {
      GvdbTable *table;

      table = gvdb_table_get_table (manager->gvdb_table, name);
      if (table)
        {
          gvdb_table_free (table);
          return TRUE;
        }
    }

  return FALSE;
}

MetaSessionState *
meta_session_manager_get_session (MetaSessionManager  *manager,
                                  GType                type,
                                  const gchar         *name)
{
  g_autoptr (MetaSessionState) session_state = NULL;
  GvdbTable *table = NULL;

  g_assert (g_type_is_a (type, META_TYPE_SESSION_STATE));

  session_state = g_hash_table_lookup (manager->sessions, name);
  if (session_state)
    return g_steal_pointer (&session_state);

  session_state = g_object_new (type, "name", name, NULL);

  if (manager->gvdb_table)
    table = gvdb_table_get_table (manager->gvdb_table, name);

  if (table)
    {
      g_autoptr (GError) error = NULL;

      meta_session_state_parse (session_state, table, &error);
      g_clear_pointer (&table, gvdb_table_free);

      if (error)
        {
          g_critical ("Error parsing session data: %s\n", error->message);
          /* Ensure to return a pristine state */
          g_clear_object (&session_state);
          session_state = g_object_new (type, "name", name, NULL);
        }
    }

  g_hash_table_insert (manager->sessions,
                       (gpointer) meta_session_state_get_name (session_state),
                       g_object_ref (session_state));

  g_signal_emit (manager, signals[SESSION_INSTANTIATED], 0,
                 meta_session_state_get_name (session_state),
                 session_state);

  return g_steal_pointer (&session_state);
}

void
meta_session_manager_delete_session (MetaSessionManager *manager,
                                     const char         *name)
{
  g_hash_table_add (manager->deleted_sessions, g_strdup (name));
  g_hash_table_remove (manager->sessions, name);
}

static void
snapshot_gvdb_recursively (GvdbTable   *table,
                           GHashTable  *dest,
                           const gchar *name)
{
  g_autoptr (GVariant) value = NULL;

  value = gvdb_table_get_value (table, name);

  if (value)
    {
      GvdbItem *item;

      item = gvdb_hash_table_insert (dest, name);
      gvdb_item_set_value (item, value);
    }
  else
    {
      GvdbTable *subtable;
      GHashTable *dest_subtable;
      g_auto (GStrv) names;
      size_t len, i;

      subtable = gvdb_table_get_table (table, name);
      names = gvdb_table_get_names (subtable, &len);
      dest_subtable = gvdb_hash_table_new (dest, name);

      for (i = 0; i < len; i++)
        snapshot_gvdb_recursively (subtable, dest_subtable, names[i]);

      gvdb_table_free (subtable);
    }
}

static void
meta_session_data_free (MetaSessionData *session_data)
{
  g_clear_pointer (&session_data->new_table, g_hash_table_unref);
  g_clear_pointer (&session_data->deleted_sessions, g_hash_table_unref);
  g_free (session_data);
}

static gboolean
meta_session_data_save (MetaSessionData  *session_data,
                        GError          **error)
{
  g_autofree char *session_dir = NULL, *session_file = NULL;

  if (!session_data->name)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Context does not have a name");
      return FALSE;
    }

  session_dir = g_build_filename (g_get_user_data_dir (),
                                  session_data->name, NULL);

  if (g_mkdir_with_parents (session_dir, 0700) < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Could not create directory for session data: %m");
      return FALSE;
    }

  session_file = g_build_filename (session_dir,
                                   SESSION_FILE_NAME,
                                   NULL);

  if (session_data->gvdb_table)
    {
      g_auto (GStrv) names;
      gsize len, i;

      names = gvdb_table_get_names (session_data->gvdb_table, &len);

      for (i = 0; i < len; i++)
        {
          if (g_hash_table_contains (session_data->new_table, names[i]))
            continue;
          if (g_hash_table_contains (session_data->deleted_sessions, names[i]))
            continue;

          snapshot_gvdb_recursively (session_data->gvdb_table,
                                     session_data->new_table, names[i]);
        }
    }

  return gvdb_table_write_contents (session_data->new_table, session_file,
                                    FALSE, error);
}

static void
save_session_async (GTask        *task,
                    gpointer      source_object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  MetaSessionManager *session_manager = source_object;
  g_autoptr (GError) error = NULL;

  g_mutex_lock (&session_manager->mutex);

  if (meta_session_data_save (task_data, &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, g_steal_pointer (&error));

  g_mutex_unlock (&session_manager->mutex);
}

static MetaSessionData *
meta_session_manager_snapshot (MetaSessionManager *manager)
{
  MetaSessionState *session_state;
  MetaSessionData *session_data;
  GHashTableIter iter;
  const gchar *name;

  session_data = g_new0 (MetaSessionData, 1);
  session_data->gvdb_table = manager->gvdb_table;
  session_data->new_table = gvdb_hash_table_new (NULL, NULL);
  session_data->name = manager->name;

  session_data->deleted_sessions =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_hash_table_iter_init (&iter, manager->deleted_sessions);
  while (g_hash_table_iter_next (&iter, (gpointer *) &name, NULL))
    g_hash_table_add (session_data->deleted_sessions, g_strdup (name));

  g_hash_table_iter_init (&iter, manager->sessions);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &session_state))
    {
      GHashTable *session_table;

      session_table =
        gvdb_hash_table_new (session_data->new_table,
                             meta_session_state_get_name (session_state));
      meta_session_state_serialize (session_state, session_table);
    }

  return session_data;
}

void
meta_session_manager_save (MetaSessionManager  *manager,
                           GAsyncReadyCallback  cb,
                           gpointer             user_data)
{
  MetaSessionData *session_data;
  g_autoptr (GTask) task = NULL;

  task = g_task_new (manager, NULL, cb, user_data);
  session_data = meta_session_manager_snapshot (manager);
  if (session_data)
    {
      g_task_set_task_data (task, session_data,
                            (GDestroyNotify) meta_session_data_free);
      g_task_run_in_thread (task, save_session_async);
    }
  else
    {
      g_task_return_boolean (task, TRUE);
    }
}

gboolean
meta_session_manager_save_finish (MetaSessionManager  *manager,
                                  GAsyncResult        *res,
                                  GError             **error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

gboolean
meta_session_manager_save_sync (MetaSessionManager  *manager,
                                GError             **error)
{
  MetaSessionData *session_data;
  gboolean retval = TRUE;

  g_mutex_lock (&manager->mutex);

  session_data = meta_session_manager_snapshot (manager);
  if (session_data)
    {
      retval = meta_session_data_save (session_data, error);
      meta_session_data_free (session_data);
    }

  g_mutex_unlock (&manager->mutex);

  return retval;
}
