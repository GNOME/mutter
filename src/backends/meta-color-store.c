/*
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 NVIDIA CORPORATION
 * Copyright (C) 2021 Red Hat Inc.
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
 */

#include "config.h"

#include "backends/meta-color-store.h"

#include <colord.h>
#include <stdint.h>

#include "backends/meta-color-device.h"
#include "backends/meta-color-profile.h"
#include "backends/meta-monitor.h"

struct _MetaColorStore
{
  GObject parent;

  MetaColorManager *color_manager;

  GHashTable *profiles;
  GHashTable *device_profiles;
  GHashTable *pending_device_profiles;
};

typedef struct
{
  MetaColorStore *color_store;
  char *key;
} EnsureDeviceProfileData;

G_DEFINE_TYPE (MetaColorStore, meta_color_store,
               G_TYPE_OBJECT)

static void
meta_color_store_finalize (GObject *object)
{
  MetaColorStore *color_store = META_COLOR_STORE (object);

  g_clear_pointer (&color_store->device_profiles, g_hash_table_unref);
  g_clear_pointer (&color_store->pending_device_profiles, g_hash_table_unref);

  G_OBJECT_CLASS (meta_color_store_parent_class)->finalize (object);
}

static void
meta_color_store_class_init (MetaColorStoreClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_color_store_finalize;
}

static void
meta_color_store_init (MetaColorStore *color_store)
{
}

MetaColorStore *
meta_color_store_new (MetaColorManager *color_manager)
{
  MetaColorStore *color_store;

  color_store = g_object_new (META_TYPE_COLOR_STORE, NULL);
  color_store->color_manager = color_manager;
  color_store->device_profiles =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  color_store->pending_device_profiles =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  return color_store;
}

static void
ensure_device_profile_data_free (EnsureDeviceProfileData *data)
{
  g_free (data->key);
  g_free (data);
}

static void
on_profile_generated (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  MetaColorDevice *color_device = META_COLOR_DEVICE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GError) error = NULL;
  MetaColorProfile *color_profile;

  color_profile = meta_color_device_generate_profile_finish (color_device,
                                                             res,
                                                             &error);
  if (!color_profile)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }

      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Failed to generate and read ICC profile: %s",
                               error->message);
      return;
    }

  g_task_return_pointer (task, color_profile, g_object_unref);
}

gboolean
meta_color_store_ensure_device_profile (MetaColorStore      *color_store,
                                        MetaColorDevice     *color_device,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  MetaMonitor *monitor;
  const char *edid_checksum_md5;
  g_autoptr (GTask) task = NULL;
  g_autofree char *file_name = NULL;
  char *file_path;
  EnsureDeviceProfileData *data;
  MetaColorProfile *color_profile;

  monitor = meta_color_device_get_monitor (color_device);
  edid_checksum_md5 = meta_monitor_get_edid_checksum_md5 (monitor);
  if (!edid_checksum_md5)
    return FALSE;

  task = g_task_new (G_OBJECT (color_store), cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_color_store_ensure_device_profile);

  file_name = g_strdup_printf ("edid-%s.icc", edid_checksum_md5);
  file_path = g_build_filename (g_get_user_data_dir (),
                                "icc", file_name, NULL);

  data = g_new0 (EnsureDeviceProfileData, 1);
  data->color_store = color_store;
  data->key = g_strdup (meta_color_device_get_id (color_device));
  g_task_set_task_data (task, data,
                        (GDestroyNotify) ensure_device_profile_data_free);

  color_profile = g_hash_table_lookup (color_store->device_profiles, data->key);
  if (color_profile)
    {
      g_task_return_pointer (task,
                             g_object_ref (color_profile),
                             g_object_unref);
      return TRUE;
    }

  if (g_hash_table_contains (color_store->pending_device_profiles, data->key))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Profile generation already in progress");
      return TRUE;
    }

  g_hash_table_add (color_store->pending_device_profiles, g_strdup (data->key));

  meta_color_device_generate_profile (color_device,
                                      file_path,
                                      cancellable,
                                      on_profile_generated,
                                      g_steal_pointer (&task));
  return TRUE;
}

MetaColorProfile *
meta_color_store_ensure_device_profile_finish (MetaColorStore  *color_store,
                                               GAsyncResult    *res,
                                               GError         **error)
{
  GTask *task = G_TASK (res);
  EnsureDeviceProfileData *data = g_task_get_task_data (task);
  g_autoptr (MetaColorProfile) color_profile = NULL;

  g_assert (g_task_get_source_tag (task) ==
            meta_color_store_ensure_device_profile);

  g_hash_table_remove (color_store->pending_device_profiles, data->key);

  color_profile = g_task_propagate_pointer (task, error);
  if (!color_profile)
    return NULL;

  g_hash_table_insert (color_store->device_profiles,
                       g_steal_pointer (&data->key),
                       g_object_ref (color_profile));
  return color_profile;
}
