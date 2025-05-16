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
#include "backends/meta-monitor-private.h"

struct _MetaColorStore
{
  GObject parent;

  MetaColorManager *color_manager;

  GFileMonitor *icc_directory_monitor;

  GHashTable *profiles;
  GHashTable *device_profiles;
  GHashTable *pending_device_profiles;
  GHashTable *pending_local_profiles;

  GCancellable *cancellable;
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

  g_cancellable_cancel (color_store->cancellable);
  g_clear_object (&color_store->cancellable);

  g_clear_object (&color_store->icc_directory_monitor);
  g_clear_pointer (&color_store->profiles, g_hash_table_unref);
  g_clear_pointer (&color_store->device_profiles, g_hash_table_unref);
  g_clear_pointer (&color_store->pending_device_profiles, g_hash_table_unref);
  g_clear_pointer (&color_store->pending_local_profiles, g_hash_table_unref);

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
  color_store->cancellable = g_cancellable_new ();
}

static void
on_directory_profile_ready (MetaColorProfile *color_profile,
                            gboolean          success,
                            MetaColorStore   *color_store)
{
  g_autoptr (MetaColorProfile) stolen_color_profile = NULL;
  g_autofree char *stolen_file_path = NULL;

  if (!g_hash_table_steal_extended (color_store->pending_local_profiles,
                                    meta_color_profile_get_file_path (color_profile),
                                    (gpointer *) &stolen_file_path,
                                    (gpointer *) &stolen_color_profile))
    g_warn_if_reached ();

  if (!success)
    return;

  g_hash_table_insert (color_store->profiles,
                       g_strdup (meta_color_profile_get_id (color_profile)),
                       g_object_ref (color_profile));

  meta_topic (META_DEBUG_COLOR, "Created colord profile '%s' from '%s'",
              meta_color_profile_get_id (color_profile),
              meta_color_profile_get_file_path (color_profile));
}

static void
create_profile_from_contents (MetaColorStore *color_store,
                              const char     *file_path,
                              const char     *contents,
                              size_t          size)
{
  g_autoptr (CdIcc) cd_icc = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autofree char *file_md5_checksum = NULL;
  MetaColorCalibration *color_calibration;
  MetaColorProfile *color_profile;

  cd_icc = cd_icc_new ();
  if (!cd_icc_load_data (cd_icc,
                         (uint8_t *) contents,
                         size,
                         CD_ICC_LOAD_FLAGS_METADATA,
                         &error))
    {
      g_warning ("Failed to parse ICC profile '%s': %s", file_path, error->message);
      return;
    }

  bytes = g_bytes_new (contents, size);

  cd_icc_add_metadata (cd_icc, CD_PROFILE_PROPERTY_FILENAME,
                       file_path);

  file_md5_checksum = g_compute_checksum_for_bytes (G_CHECKSUM_MD5,
                                                    bytes);
  cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_FILE_CHECKSUM,
                       file_md5_checksum);
  color_calibration = meta_color_calibration_new (cd_icc, NULL);
  color_profile =
    meta_color_profile_new_from_icc (color_store->color_manager,
                                     g_steal_pointer (&cd_icc),
                                     g_steal_pointer (&bytes),
                                     color_calibration);

  g_signal_connect (color_profile, "ready",
                    G_CALLBACK (on_directory_profile_ready),
                    color_store);

  g_hash_table_insert (color_store->pending_local_profiles,
                       g_strdup (file_path),
                       color_profile);
}

static gboolean
should_ignore_store_file (GFile *file)
{
  g_autofree char *file_name = NULL;

  /* Ignore profiles from EDID, as they will always be generated on demand. */
  file_name = g_file_get_basename (file);
  return g_str_has_prefix (file_name, "edid-");
}

static void
process_icc_directory_file (MetaColorStore *color_store,
                            GFile          *file)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *contents = NULL;
  size_t size;

  if (should_ignore_store_file (file))
    return;

  if (!g_file_load_contents (file, NULL, &contents, &size, NULL, &error))
    {
      g_warning ("Failed to read '%s': %s",
                 g_file_peek_path (file), error->message);
      return;
    }

  create_profile_from_contents (color_store, g_file_peek_path (file),
                                contents, size);
}

static gboolean
is_file_info_icc_profile (GFileInfo *info)
{
  const char *content_type;

  content_type =
    g_file_info_get_attribute_string (info,
                                      G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
  if (g_strcmp0 (content_type, "application/vnd.iccprofile") != 0)
    return FALSE;

  if (g_file_info_get_attribute_boolean (info,
                                         G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN))
    return FALSE;

  if (g_file_info_get_attribute_boolean (info,
                                         G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP))
    return FALSE;

  return TRUE;
}

static gboolean
is_file_icc_profile (GFile *file)
{
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GError) error = NULL;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_NAME ","
                            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                            G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
                            G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP ","
                            G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL,
                            &error);
  if (!info)
    {
      g_warning ("Failed to query file info on '%s': %s",
                 g_file_get_path (file),
                 error->message);
      return FALSE;
    }

  return is_file_info_icc_profile (info);
}

static void
on_store_file_read (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  MetaColorStore *color_store = user_data;
  g_autoptr (GError) error = NULL;
  g_autofree char *contents = NULL;
  size_t size;

  if (!g_file_load_contents_finish (file, res,
                                    &contents,
                                    &size,
                                    NULL,
                                    &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_warning ("Failed to read '%s': %s",
                     g_file_peek_path (file), error->message);
        }
      return;
    }

  create_profile_from_contents (color_store, g_file_peek_path (file),
                                contents, size);
}

static void
query_file_info_cb (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr (GFileInfo) info = NULL;
  g_autoptr (GError) error = NULL;
  MetaColorStore *color_store = user_data;

  info = g_file_query_info_finish (file, res, &error);
  if (!info)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
          g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        return;

      g_warning ("Failed to query file info on '%s': %s",
                 g_file_get_path (file),
                 error->message);
      return;
    }

  if (!is_file_info_icc_profile (info))
    return;

  if (should_ignore_store_file (file))
    return;

  g_file_load_contents_async (file, color_store->cancellable,
                              on_store_file_read, color_store);
}

static void
on_icc_directory_change (GFileMonitor      *monitor,
                         GFile             *file,
                         GFile             *other_file,
                         GFileMonitorEvent  event_type,
                         MetaColorStore    *color_store)
{
  switch (event_type)
    {
    case G_FILE_MONITOR_EVENT_CREATED:
      break;
    default:
      return;
    }

  g_file_query_info_async (file,
                           G_FILE_ATTRIBUTE_STANDARD_NAME ","
                           G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                           G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
                           G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP ","
                           G_FILE_ATTRIBUTE_STANDARD_TYPE,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           color_store->cancellable,
                           query_file_info_cb,
                           color_store);
}

static gboolean
init_profile_directory (MetaColorStore  *color_store,
                        GError         **error)
{
  g_autofree char *icc_directory_path = NULL;
  g_autoptr (GFile) icc_directory = NULL;
  g_autoptr (GError) local_error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  icc_directory_path = g_build_filename (g_get_user_data_dir (),
                                         "icc", NULL);
  icc_directory = g_file_new_for_path (icc_directory_path);

  if (!g_file_query_exists (icc_directory, NULL))
    {
      if (!g_file_make_directory_with_parents (icc_directory, NULL, error))
        return FALSE;
    }

  color_store->icc_directory_monitor =
    g_file_monitor (icc_directory,
                    G_FILE_MONITOR_NONE,
                    NULL, &local_error);
  if (color_store->icc_directory_monitor)
    {
      g_signal_connect (color_store->icc_directory_monitor,
                        "changed", G_CALLBACK (on_icc_directory_change),
                        color_store);
    }
  else
    {
      g_warning ("Failed to monitor ICC profile directory '%s': %s",
                 icc_directory_path,
                 local_error->message);
      g_clear_error (&local_error);
    }

  enumerator = g_file_enumerate_children (icc_directory,
                                          G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                          G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                          G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
                                          G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP ","
                                          G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                          G_FILE_QUERY_INFO_NONE,
                                          NULL,
                                          error);
  if (!enumerator)
    return FALSE;

  while (TRUE)
    {
      g_autoptr (GFileInfo) file_info = NULL;

      file_info = g_file_enumerator_next_file (enumerator, NULL, error);
      if (!file_info)
        {
          if (*error)
            return FALSE;
          else
            break;
        }

     switch (g_file_info_get_file_type (file_info))
       {
       case G_FILE_TYPE_REGULAR:
         {
           g_autoptr (GFile) file = NULL;
           g_autofree char *file_path = NULL;

           file_path = g_build_filename (icc_directory_path,
                                         g_file_info_get_name (file_info),
                                         NULL);
           file = g_file_new_for_path (file_path);
           if (is_file_icc_profile (file))
             process_icc_directory_file (color_store, file);

           break;
         }
       case G_FILE_TYPE_SYMBOLIC_LINK:
         {
           const char *target_path;
           g_autoptr (GFile) target = NULL;

           target_path = g_file_info_get_symlink_target (file_info);
           target = g_file_new_for_path (target_path);

           if (is_file_icc_profile (target))
             process_icc_directory_file (color_store, target);
           break;
         }
       default:
         break;
       }
    }

  return TRUE;
}

MetaColorStore *
meta_color_store_new (MetaColorManager *color_manager)
{
  MetaColorStore *color_store;
  g_autoptr (GError) error = NULL;

  color_store = g_object_new (META_TYPE_COLOR_STORE, NULL);
  color_store->color_manager = color_manager;
  color_store->profiles =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  color_store->device_profiles =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  color_store->pending_device_profiles =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  color_store->pending_local_profiles =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  if (!init_profile_directory (color_store, &error))
    g_warning ("Failed to monitor ICC directory: %s", error->message);

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
  g_autofree char *file_path = NULL;
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
  g_hash_table_insert (color_store->profiles,
                       g_strdup (meta_color_profile_get_id (color_profile)),
                       g_object_ref (color_profile));
  return color_profile;
}

typedef struct
{
  MetaColorStore *color_store;
  CdProfile *cd_profile;

  MetaColorProfile *created_profile;
} EnsureColordProfileData;

static void
ensure_colord_profile_data_free (EnsureColordProfileData *data)
{
  g_clear_object (&data->cd_profile);
  g_free (data);
}

static void
on_cd_profile_contents_loaded (GObject      *source_object,
                               GAsyncResult *res,
                               gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  EnsureColordProfileData *data = g_task_get_task_data (task);
  MetaColorStore *color_store = data->color_store;
  MetaColorManager *color_manager = color_store->color_manager;
  CdProfile *cd_profile = data->cd_profile;
  g_autoptr (GError) error = NULL;
  g_autofree char *contents = NULL;
  size_t length;
  g_autoptr (CdIcc) cd_icc = NULL;
  g_autofree char *file_md5_checksum = NULL;
  GBytes *bytes;
  MetaColorCalibration *color_calibration;
  MetaColorProfile *color_profile;

  if (!g_file_load_contents_finish (file, res,
                                    &contents,
                                    &length,
                                    NULL,
                                    &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  cd_icc = cd_icc_new ();
  if (!cd_icc_load_data (cd_icc,
                         (uint8_t *) contents,
                         length,
                         CD_ICC_LOAD_FLAGS_METADATA,
                         &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  cd_icc_add_metadata (cd_icc, CD_PROFILE_PROPERTY_FILENAME,
                       g_file_peek_path (file));

  file_md5_checksum = g_compute_checksum_for_data (G_CHECKSUM_MD5,
                                                   (uint8_t *) contents,
                                                   length);
  cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_FILE_CHECKSUM,
                       file_md5_checksum);

  bytes = g_bytes_new_take (g_steal_pointer (&contents), length);
  color_calibration = meta_color_calibration_new (cd_icc, NULL);
  color_profile =
    meta_color_profile_new_from_cd_profile (color_manager,
                                            cd_profile,
                                            g_steal_pointer (&cd_icc),
                                            bytes,
                                            color_calibration);

  g_hash_table_insert (color_store->profiles,
                       g_strdup (meta_color_profile_get_id (color_profile)),
                       color_profile);

  meta_topic (META_DEBUG_COLOR, "Created colord profile '%s' from '%s'",
              cd_profile_get_id (cd_profile),
              cd_profile_get_filename (cd_profile));

  g_task_return_pointer (task, g_object_ref (color_profile), g_object_unref);
}

static void
on_cd_profile_connected (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  CdProfile *cd_profile = CD_PROFILE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  EnsureColordProfileData *data = g_task_get_task_data (task);
  MetaColorStore *color_store = data->color_store;
  g_autoptr (GError) error = NULL;
  const char *file_path;
  g_autoptr (GFile) file = NULL;
  MetaColorProfile *color_profile;
  GCancellable *cancellable;

  if (!cd_profile_connect_finish (cd_profile, res, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  color_profile = g_hash_table_lookup (color_store->profiles,
                                       cd_profile_get_id (cd_profile));
  if (color_profile)
    {
      meta_topic (META_DEBUG_COLOR, "Found existing colord profile '%s'",
                  cd_profile_get_id (cd_profile));
      g_task_return_pointer (task,
                             g_object_ref (color_profile),
                             g_object_unref);
      return;
    }

  file_path = cd_profile_get_filename (cd_profile);
  if (!file_path)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Tried to assign non-local profile");
      return;
    }

  file = g_file_new_for_path (file_path);
  cancellable = g_task_get_cancellable (task);
  g_file_load_contents_async (file,
                              cancellable,
                              on_cd_profile_contents_loaded,
                              g_steal_pointer (&task));
}

void
meta_color_store_ensure_colord_profile (MetaColorStore      *color_store,
                                        CdProfile           *cd_profile,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  GTask *task;
  EnsureColordProfileData *data;

  task = g_task_new (G_OBJECT (color_store), cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_color_store_ensure_colord_profile);

  data = g_new0 (EnsureColordProfileData, 1);
  data->color_store = color_store;
  data->cd_profile = g_object_ref (cd_profile);
  g_task_set_task_data (task, data,
                        (GDestroyNotify) ensure_colord_profile_data_free);

  cd_profile_connect (cd_profile,
                      cancellable,
                      on_cd_profile_connected,
                      task);
}

MetaColorProfile *
meta_color_store_ensure_colord_profile_finish (MetaColorStore  *color_store,
                                               GAsyncResult    *res,
                                               GError         **error)
{
  GTask *task = G_TASK (res);

  g_assert (g_task_get_source_tag (task) ==
            meta_color_store_ensure_colord_profile);

  return g_task_propagate_pointer (G_TASK (res), error);
}

MetaColorProfile *
meta_color_store_get_profile (MetaColorStore *color_store,
                              const char     *profile_id)
{
  return g_hash_table_lookup (color_store->profiles, profile_id);
}

gboolean
meta_color_store_has_pending_profiles (MetaColorStore *color_store)
{
  return (g_hash_table_size (color_store->pending_local_profiles) > 0 ||
          g_hash_table_size (color_store->pending_device_profiles) > 0);
}
