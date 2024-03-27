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

#include "backends/meta-color-device.h"

#include <colord.h>

#include "backends/meta-color-device.h"
#include "backends/meta-color-manager-private.h"
#include "backends/meta-color-profile.h"
#include "backends/meta-color-store.h"
#include "backends/meta-monitor.h"

#define EFI_PANEL_COLOR_INFO_PATH \
  "/sys/firmware/efi/efivars/INTERNAL_PANEL_COLOR_INFO-01e1ada1-79f2-46b3-8d3e-71fc0996ca6b"

enum
{
  READY,
  CHANGED,
  UPDATED,

  N_SIGNALS
};

static guint signals[N_SIGNALS];

typedef enum
{
  PENDING_EDID_PROFILE = 1 << 0,
  PENDING_PROFILE_READY = 1 << 1,
  PENDING_CONNECTED = 1 << 2,
} PendingState;

struct _MetaColorDevice
{
  GObject parent;

  MetaColorManager *color_manager;

  char *cd_device_id;
  MetaMonitor *monitor;
  CdDevice *cd_device;

  MetaColorProfile *device_profile;
  gulong device_profile_ready_handler_id;

  MetaColorProfile *assigned_profile;
  gulong assigned_profile_ready_handler_id;
  GCancellable *assigned_profile_cancellable;

  GCancellable *cancellable;

  PendingState pending_state;
  gboolean is_ready;
};

G_DEFINE_TYPE (MetaColorDevice, meta_color_device,
               G_TYPE_OBJECT)

static const char *efivar_test_path = NULL;

/*
 * Generate a colord DeviceId according to
 * `device-and-profiling-naming-spec.txt`.
 *
 * A rough summary is that it should use the following format:
 *
 *   xrandr[-{%edid_vendor_name}][-{%edid_product][-{%edid_serial}]
 *
 */
static char *
generate_cd_device_id (MetaMonitor *monitor)
{
  GString *device_id;
  const char *vendor;
  const char *product;
  const char *serial;

  vendor = meta_monitor_get_vendor (monitor);
  product = meta_monitor_get_product (monitor);
  serial = meta_monitor_get_serial (monitor);

  device_id = g_string_new ("xrandr");

  if (!vendor && !product && !serial)
    {
      g_string_append_printf (device_id,
                              "-%s",
                              meta_monitor_get_connector (monitor));
      goto out;
    }

  if (vendor)
    {
      MetaBackend *backend = meta_monitor_get_backend (monitor);
      g_autofree char *vendor_name = NULL;

      vendor_name = meta_backend_get_vendor_name (backend, vendor);
      g_string_append_printf (device_id, "-%s",
                              vendor_name ? vendor_name : vendor);
    }

  if (product)
    g_string_append_printf (device_id, "-%s", product);
  if (serial)
    g_string_append_printf (device_id, "-%s", serial);

out:
  return g_string_free (device_id, FALSE);
}

static void
ensure_default_profile_cb (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  MetaColorStore *color_store = META_COLOR_STORE (source_object);
  MetaColorDevice *color_device;
  g_autoptr (MetaColorProfile) color_profile = NULL;
  g_autoptr (GError) error = NULL;

  color_profile = meta_color_store_ensure_colord_profile_finish (color_store,
                                                                 res,
                                                                 &error);
  if (!color_profile)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create color profile from colord profile: %s",
                 error->message);
    }

  color_device = META_COLOR_DEVICE (user_data);
  if (color_device->assigned_profile == color_profile)
    return;

  g_set_object (&color_device->assigned_profile, color_profile);

  g_signal_emit (color_device, signals[CHANGED], 0);
}

static void
update_assigned_profile (MetaColorDevice *color_device)
{
  MetaColorManager *color_manager = color_device->color_manager;
  MetaColorStore *color_store =
    meta_color_manager_get_color_store (color_manager);
  CdProfile *default_profile;
  GCancellable *cancellable;

  default_profile = cd_device_get_default_profile (color_device->cd_device);

  if (color_device->assigned_profile &&
      meta_color_profile_get_cd_profile (color_device->assigned_profile) ==
      default_profile)
    return;

  if (color_device->assigned_profile_cancellable)
    {
      g_cancellable_cancel (color_device->assigned_profile_cancellable);
      g_clear_object (&color_device->assigned_profile_cancellable);
    }

  if (!default_profile)
    {
      g_clear_object (&color_device->assigned_profile);
      return;
    }

  cancellable = g_cancellable_new ();
  color_device->assigned_profile_cancellable = cancellable;

  meta_color_store_ensure_colord_profile (color_store,
                                          default_profile,
                                          cancellable,
                                          ensure_default_profile_cb,
                                          color_device);
}

static void
on_cd_device_changed (CdDevice        *cd_device,
                      MetaColorDevice *color_device)
{
  update_assigned_profile (color_device);
}

typedef struct
{
  GMainLoop *loop;
  CdDevice *cd_device;
  GError *error;
} FindDeviceData;

static void
on_find_device (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  CdClient *cd_client = CD_CLIENT (source_object);
  FindDeviceData *data = user_data;

  data->cd_device = cd_client_find_device_finish (cd_client, res, &data->error);
  g_main_loop_quit (data->loop);
}

static CdDevice *
find_device_sync (CdClient    *cd_client,
                  const char  *cd_device_id,
                  GError     **error)
{
  g_autoptr (GMainContext) main_context = NULL;
  g_autoptr (GMainLoop) main_loop = NULL;
  FindDeviceData data = {};

  main_context = g_main_context_new ();
  main_loop = g_main_loop_new (main_context, FALSE);
  g_main_context_push_thread_default (main_context);

  data = (FindDeviceData) {
    .loop = main_loop,
  };
  cd_client_find_device (cd_client, cd_device_id, NULL,
                         on_find_device,
                         &data);
  g_main_loop_run (main_loop);

  g_main_context_pop_thread_default (main_context);

  if (data.error)
    g_propagate_error (error, data.error);
  return data.cd_device;
}

static void
meta_color_device_dispose (GObject *object)
{
  MetaColorDevice *color_device = META_COLOR_DEVICE (object);
  MetaColorManager *color_manager = color_device->color_manager;
  CdClient *cd_client = meta_color_manager_get_cd_client (color_manager);
  CdDevice *cd_device;
  const char *cd_device_id;

  meta_topic (META_DEBUG_COLOR,
              "Removing color device '%s'", color_device->cd_device_id);

  if (color_device->assigned_profile_cancellable)
    {
      g_cancellable_cancel (color_device->assigned_profile_cancellable);
      g_clear_object (&color_device->assigned_profile_cancellable);
    }

  g_cancellable_cancel (color_device->cancellable);
  g_clear_object (&color_device->cancellable);
  g_clear_signal_handler (&color_device->device_profile_ready_handler_id,
                          color_device->device_profile);

  g_clear_object (&color_device->assigned_profile);
  g_clear_object (&color_device->device_profile);

  cd_device = color_device->cd_device;
  cd_device_id = color_device->cd_device_id;
  if (!cd_device && cd_device_id)
    {
      g_autoptr (GError) error = NULL;

      cd_device = find_device_sync (cd_client,
                                    cd_device_id,
                                    &error);
      if (!cd_device &&
          !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          g_warning ("Failed to find colord device %s: %s",
                     cd_device_id, error->message);
        }
    }

  if (cd_device)
    cd_client_delete_device (cd_client, cd_device, NULL, NULL, NULL);

  g_clear_pointer (&color_device->cd_device_id, g_free);
  g_clear_object (&color_device->cd_device);
  g_clear_object (&color_device->monitor);

  G_OBJECT_CLASS (meta_color_device_parent_class)->dispose (object);
}

static void
meta_color_device_class_init (MetaColorDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = meta_color_device_dispose;

  signals[READY] =
    g_signal_new ("ready",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  G_TYPE_BOOLEAN);
  signals[CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
  signals[UPDATED] =
    g_signal_new ("updated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
meta_color_device_init (MetaColorDevice *color_device)
{
}

static void
meta_color_device_notify_ready (MetaColorDevice *color_device,
                                gboolean         success)
{
  color_device->is_ready = TRUE;
  g_signal_emit (color_device, signals[READY], 0, success);
}

static void
maybe_finish_setup (MetaColorDevice *color_device)
{
  if (color_device->pending_state)
    return;

  meta_topic (META_DEBUG_COLOR, "Color device '%s' is ready",
              color_device->cd_device_id);

  meta_color_device_notify_ready (color_device, TRUE);
}

static void
on_cd_device_connected (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  CdDevice *cd_device = CD_DEVICE (source_object);
  MetaColorDevice *color_device = user_data;
  g_autoptr (GError) error = NULL;

  if (!cd_device_connect_finish (cd_device, res, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      color_device->pending_state &= ~PENDING_CONNECTED;

      g_warning ("Failed to connect to colord device %s: %s",
                 color_device->cd_device_id,
                 error->message);

      g_cancellable_cancel (color_device->cancellable);
      meta_color_device_notify_ready (color_device, FALSE);
      return;
    }
  else
    {
      color_device->pending_state &= ~PENDING_CONNECTED;
      meta_topic (META_DEBUG_COLOR, "Color device '%s' connected",
                  color_device->cd_device_id);
    }

  g_signal_connect (cd_device, "changed",
                    G_CALLBACK (on_cd_device_changed), color_device);
  update_assigned_profile (color_device);

  maybe_finish_setup (color_device);
}

static void
on_profile_ready (MetaColorProfile *color_profile,
                  gboolean          success,
                  MetaColorDevice  *color_device)
{
  color_device->pending_state &= ~PENDING_PROFILE_READY;

  if (!success)
    {
      g_clear_object (&color_device->device_profile);
      g_cancellable_cancel (color_device->cancellable);
      meta_color_device_notify_ready (color_device, FALSE);
      return;
    }

  maybe_finish_setup (color_device);
}

static void
ensure_device_profile_cb (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  MetaColorStore *color_store = META_COLOR_STORE (source_object);
  MetaColorDevice *color_device = META_COLOR_DEVICE (user_data);
  MetaColorProfile *color_profile;
  g_autoptr (GError) error = NULL;

  color_profile = meta_color_store_ensure_device_profile_finish (color_store,
                                                                 res,
                                                                 &error);

  if (!color_profile)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create device color profile: %s", error->message);

      color_device->pending_state &= ~PENDING_EDID_PROFILE;
      g_cancellable_cancel (color_device->cancellable);
      meta_color_device_notify_ready (color_device, FALSE);
      return;
    }

  meta_topic (META_DEBUG_COLOR, "Color device '%s' generated",
              color_device->cd_device_id);

  color_device->pending_state &= ~PENDING_EDID_PROFILE;
  g_set_object (&color_device->device_profile, color_profile);

  if (!meta_color_profile_is_ready (color_profile))
    {
      color_device->device_profile_ready_handler_id =
        g_signal_connect (color_profile, "ready",
                          G_CALLBACK (on_profile_ready),
                          color_device);
      color_device->pending_state |= PENDING_PROFILE_READY;
    }
  else
    {
      maybe_finish_setup (color_device);
    }
}

static void
on_cd_device_created (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CdClient *cd_client = CD_CLIENT (object);
  MetaColorDevice *color_device = user_data;
  MetaColorManager *color_manager;
  MetaColorStore *color_store;
  CdDevice *cd_device;
  g_autoptr (GError) error = NULL;

  cd_device = cd_client_create_device_finish (cd_client, res, &error);
  if (!cd_device)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create colord device for '%s': %s",
                 color_device->cd_device_id,
                 error->message);
      meta_color_device_notify_ready (color_device, FALSE);
      return;
    }

  color_device->cd_device = cd_device;

  cd_device_connect (cd_device, color_device->cancellable,
                     on_cd_device_connected, color_device);
  color_device->pending_state |= PENDING_CONNECTED;

  color_manager = color_device->color_manager;
  color_store = meta_color_manager_get_color_store (color_manager);
  if (meta_color_store_ensure_device_profile (color_store,
                                              color_device,
                                              color_device->cancellable,
                                              ensure_device_profile_cb,
                                              color_device))
    color_device->pending_state |= PENDING_EDID_PROFILE;
}

static void
add_device_property (GHashTable *device_props,
                     const char *key,
                     const char *value)
{
  g_hash_table_insert (device_props,
                       (gpointer) key,
                       g_strdup (value));
}

static GHashTable *
generate_color_device_props (MetaMonitor *monitor)
{
  MetaBackend *backend = meta_monitor_get_backend (monitor);
  GHashTable *device_props;
  g_autofree char *vendor_name = NULL;
  const char *edid_checksum_md5;

  device_props = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

  add_device_property (device_props,
                       CD_DEVICE_PROPERTY_KIND,
                       cd_device_kind_to_string (CD_DEVICE_KIND_DISPLAY));
  add_device_property (device_props,
                       CD_DEVICE_PROPERTY_MODE,
                       meta_monitor_is_virtual (monitor) ?
                         cd_device_mode_to_string (CD_DEVICE_MODE_VIRTUAL) :
                         cd_device_mode_to_string (CD_DEVICE_MODE_PHYSICAL));
  add_device_property (device_props,
                       CD_DEVICE_PROPERTY_COLORSPACE,
                       cd_colorspace_to_string (CD_COLORSPACE_RGB));

  vendor_name =
    meta_backend_get_vendor_name (backend, meta_monitor_get_vendor (monitor));
  add_device_property (device_props,
                       CD_DEVICE_PROPERTY_VENDOR,
                       vendor_name);
  add_device_property (device_props,
                       CD_DEVICE_PROPERTY_MODEL,
                       meta_monitor_get_product (monitor));
  add_device_property (device_props,
                       CD_DEVICE_PROPERTY_SERIAL,
                       meta_monitor_get_serial (monitor));
  add_device_property (device_props,
                       CD_DEVICE_METADATA_XRANDR_NAME,
                       meta_monitor_get_connector (monitor));
  add_device_property (device_props,
                       CD_DEVICE_METADATA_OUTPUT_PRIORITY,
                       meta_monitor_is_primary (monitor) ?
                         CD_DEVICE_METADATA_OUTPUT_PRIORITY_PRIMARY :
                         CD_DEVICE_METADATA_OUTPUT_PRIORITY_SECONDARY);

  edid_checksum_md5 = meta_monitor_get_edid_checksum_md5 (monitor);
  if (edid_checksum_md5)
    {
      add_device_property (device_props,
                           CD_DEVICE_METADATA_OUTPUT_EDID_MD5,
                           edid_checksum_md5);
    }

  if (meta_monitor_is_laptop_panel (monitor))
    {
      add_device_property (device_props,
                           CD_DEVICE_PROPERTY_EMBEDDED,
                           NULL);
    }

  return device_props;
}

MetaColorDevice *
meta_color_device_new (MetaColorManager *color_manager,
                       MetaMonitor      *monitor)
{
  MetaColorDevice *color_device;
  g_autoptr (GHashTable) device_props = NULL;

  device_props = generate_color_device_props (monitor);
  color_device = g_object_new (META_TYPE_COLOR_DEVICE, NULL);
  color_device->cd_device_id = generate_cd_device_id (monitor);
  color_device->monitor = g_object_ref (monitor);
  color_device->cancellable = g_cancellable_new ();
  color_device->color_manager = color_manager;

  cd_client_create_device (meta_color_manager_get_cd_client (color_manager),
                           color_device->cd_device_id,
                           CD_OBJECT_SCOPE_TEMP,
                           device_props,
                           color_device->cancellable,
                           on_cd_device_created,
                           color_device);

  return color_device;
}

void
meta_color_device_destroy (MetaColorDevice *color_device)
{
  g_object_run_dispose (G_OBJECT (color_device));
  g_object_unref (color_device);
}

void
meta_color_device_update_monitor (MetaColorDevice *color_device,
                                  MetaMonitor     *monitor)
{
  g_warn_if_fail (meta_monitor_is_same_as (monitor, color_device->monitor));

  g_set_object (&color_device->monitor, monitor);
}

const char *
meta_color_device_get_id (MetaColorDevice *color_device)
{
  return color_device->cd_device_id;
}

typedef struct
{
  MetaColorDevice *color_device;

  char *file_path;
  GBytes *bytes;
  CdIcc *cd_icc;

  MetaColorCalibration *color_calibration;
} GenerateProfileData;

static void
generate_profile_data_free (GenerateProfileData *data)
{
  g_free (data->file_path);
  g_clear_object (&data->cd_icc);
  g_clear_pointer (&data->bytes, g_bytes_unref);
  g_clear_pointer (&data->color_calibration, meta_color_calibration_free);
  g_free (data);
}

static void
on_profile_written (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  GenerateProfileData *data = g_task_get_task_data (task);
  MetaColorManager *color_manager;
  g_autoptr (GError) error = NULL;
  MetaColorProfile *color_profile;

  if (!g_file_replace_contents_finish (file, res, NULL, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }

      g_prefix_error (&error, "Failed to write ICC profile to %s:",
                      g_file_peek_path (file));
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  meta_topic (META_DEBUG_COLOR, "On-disk device profile '%s' updated",
              g_file_peek_path (file));

  color_manager = data->color_device->color_manager;
  color_profile =
    meta_color_profile_new_from_icc (color_manager,
                                     g_steal_pointer (&data->cd_icc),
                                     g_steal_pointer (&data->bytes),
                                     g_steal_pointer (&data->color_calibration));
  g_task_return_pointer (task, color_profile, g_object_unref);
}

static void
do_save_icc_profile (GTask *task)
{
  GenerateProfileData *data = g_task_get_task_data (task);
  const uint8_t *profile_data;
  size_t profile_data_size;
  g_autoptr (GFile) file = NULL;

  profile_data = g_bytes_get_data (data->bytes, &profile_data_size);

  file = g_file_new_for_path (data->file_path);
  g_file_replace_contents_async  (file,
                                  (const char *) profile_data,
                                  profile_data_size,
                                  NULL,
                                  FALSE,
                                  G_FILE_CREATE_NONE,
                                  g_task_get_cancellable (task),
                                  on_profile_written,
                                  task);
}

static void
on_directories_created (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  GFile *directory = G_FILE (source_object);
  GTask *thread_task = G_TASK (res);
  GTask *task = G_TASK (user_data);

  if (g_cancellable_is_cancelled (g_task_get_cancellable (thread_task)))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                               "Cancelled");
      return;
    }

  meta_topic (META_DEBUG_COLOR, "ICC profile directory '%s' created",
              g_file_peek_path (directory));

  do_save_icc_profile (task);
}

static void
create_directories_in_thread (GTask        *thread_task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable)
{
  GFile *directory = G_FILE (source_object);
  g_autoptr (GError) error = NULL;

  if (!g_file_make_directory_with_parents (directory, cancellable, &error))
    g_task_return_error (thread_task, g_steal_pointer (&error));
  else
    g_task_return_boolean (thread_task, TRUE);
}

static void
create_icc_profiles_directory (GFile *directory,
                               GTask *task)
{
  g_autoptr (GTask) thread_task = NULL;

  thread_task = g_task_new (G_OBJECT (directory),
                            g_task_get_cancellable (task),
                            on_directories_created, task);
  g_task_run_in_thread (thread_task, create_directories_in_thread);
}

static void
on_directory_queried (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  GFile *directory = G_FILE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GFileInfo) file_info = NULL;
  g_autoptr (GError) error = NULL;

  file_info = g_file_query_info_finish (directory, res, &error);
  if (!file_info)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }
      else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          create_icc_profiles_directory (directory, g_steal_pointer (&task));
          return;
        }
      else
        {
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                   "Failed to ensure data directory: %s",
                                   error->message);
          return;
        }
    }

  do_save_icc_profile (g_steal_pointer (&task));
}

static void
save_icc_profile (const char *file_path,
                  GTask      *task)
{
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFile) directory = NULL;

  file = g_file_new_for_path (file_path);
  directory = g_file_get_parent (file);
  g_file_query_info_async (directory,
                           G_FILE_ATTRIBUTE_STANDARD_TYPE,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           g_task_get_cancellable (task),
                           on_directory_queried,
                           task);
}

static CdIcc *
create_icc_profile_from_edid (MetaColorDevice     *color_device,
                              const MetaEdidInfo  *edid_info,
                              const char          *file_path,
                              GError             **error)
{
  MetaColorManager *color_manager = color_device->color_manager;
  MetaMonitor *monitor = color_device->monitor;
  g_autoptr (CdIcc) cd_icc = NULL;
  cmsCIExyYTRIPLE chroma;
  cmsCIExyY white_point;
  cmsToneCurve *transfer_curve[3] = { NULL, NULL, NULL };
  cmsContext lcms_context;
  const char *product;
  const char *vendor;
  const char *serial;
  g_autofree char *vendor_name = NULL;
  cmsHPROFILE lcms_profile;

  if (G_APPROX_VALUE (edid_info->red_x, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (edid_info->red_y, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (edid_info->green_x, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (edid_info->green_y, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (edid_info->blue_x, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (edid_info->blue_y, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (edid_info->white_x, 0.0, FLT_EPSILON) ||
      G_APPROX_VALUE (edid_info->white_y, 0.0, FLT_EPSILON))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "EDID for %s contains bogus Color Characteristics",
                   meta_color_device_get_id (color_device));
      return NULL;
    }

  if (edid_info->gamma + FLT_EPSILON < 1.0 ||
      edid_info->gamma > 4.0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "EDID for %s contains bogus Display Transfer "
                   "Characteristics (GAMMA)",
                   meta_color_device_get_id (color_device));
      return NULL;
    }

  lcms_context = meta_color_manager_get_lcms_context (color_manager);
  if (!lcms_context)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Internal error: no LCMS context available");
      return NULL;
    }

  cd_icc = cd_icc_new ();

  chroma.Red.x = edid_info->red_x;
  chroma.Red.y = edid_info->red_y;
  chroma.Green.x = edid_info->green_x;
  chroma.Green.y = edid_info->green_y;
  chroma.Blue.x = edid_info->blue_x;
  chroma.Blue.y = edid_info->blue_y;
  white_point.x = edid_info->white_x;
  white_point.y = edid_info->white_y;
  white_point.Y = 1.0;

  /* Estimate the transfer function for the gamma */
  transfer_curve[0] = cmsBuildGamma (NULL, edid_info->gamma);
  transfer_curve[1] = transfer_curve[0];
  transfer_curve[2] = transfer_curve[0];

  lcms_profile = cmsCreateRGBProfileTHR (lcms_context,
                                         &white_point,
                                         &chroma,
                                         transfer_curve);

  cmsFreeToneCurve (transfer_curve[0]);

  if (!lcms_profile)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "cmsCreateRGBProfileTHR for %s failed",
                   meta_color_device_get_id (color_device));
      return NULL;
    }

  cmsSetHeaderRenderingIntent (lcms_profile, INTENT_PERCEPTUAL);
  cmsSetDeviceClass (lcms_profile, cmsSigDisplayClass);

  g_warn_if_fail (cmsGetProfileContextID (lcms_profile));
  if (!cd_icc_load_handle (cd_icc, g_steal_pointer (&lcms_profile),
                           CD_ICC_LOAD_FLAGS_PRIMARIES, error))
    return NULL;

  cd_icc_add_metadata (cd_icc, CD_PROFILE_PROPERTY_FILENAME, file_path);
  cd_icc_add_metadata (cd_icc,
                       CD_PROFILE_METADATA_DATA_SOURCE,
                       CD_PROFILE_METADATA_DATA_SOURCE_EDID);
  cd_icc_set_copyright (cd_icc, NULL,
                        "This profile is free of known copyright restrictions.");

  product = meta_monitor_get_product (monitor);
  vendor = meta_monitor_get_vendor (monitor);
  serial = meta_monitor_get_serial (monitor);
  if (vendor)
    {
      MetaBackend *backend = meta_monitor_get_backend (monitor);

      vendor_name = meta_backend_get_vendor_name (backend, vendor);
    }

  /* set 'ICC meta Tag for Monitor Profiles' data */
  cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_EDID_MD5,
                       meta_monitor_get_edid_checksum_md5 (monitor));
  if (product)
    cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_EDID_MODEL, product);
  if (serial)
    cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_EDID_SERIAL, serial);
  if (vendor)
    cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_EDID_MNFT, vendor);
  if (vendor_name)
    {
      cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_EDID_VENDOR,
                           vendor_name);
    }

  /* Set high level monitor details metadata */
  if (!product)
    product = "Unknown monitor";
  cd_icc_set_model (cd_icc, NULL, product);
  cd_icc_set_description (cd_icc, NULL,
                          meta_monitor_get_display_name (monitor));

  if (!vendor_name)
    {
      if (vendor)
        vendor_name = g_strdup (vendor);
      else
        vendor_name = g_strdup ("Unknown vendor");
    }
  cd_icc_set_manufacturer (cd_icc, NULL, vendor_name);

  /* Set the framework creator metadata */
  cd_icc_add_metadata (cd_icc,
                       CD_PROFILE_METADATA_CMF_PRODUCT,
                       PACKAGE_NAME);
  cd_icc_add_metadata (cd_icc,
                       CD_PROFILE_METADATA_CMF_BINARY,
                       PACKAGE_NAME);
  cd_icc_add_metadata (cd_icc,
                       CD_PROFILE_METADATA_CMF_VERSION,
                       PACKAGE_VERSION);
  cd_icc_add_metadata (cd_icc,
                       CD_PROFILE_METADATA_MAPPING_DEVICE_ID,
                       color_device->cd_device_id);

  return g_steal_pointer (&cd_icc);
}

static void
create_device_profile_from_edid (MetaColorDevice *color_device,
                                 GTask           *task)
{
  const MetaEdidInfo *edid_info =
    meta_monitor_get_edid_info (color_device->monitor);
  GenerateProfileData *data = g_task_get_task_data (task);
  g_autoptr (CdIcc) cd_icc = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autofree char *file_md5_checksum = NULL;
  g_autoptr (GError) error = NULL;

  if (edid_info)
    {
      meta_topic (META_DEBUG_COLOR,
                  "Generating ICC profile for '%s' from EDID",
                  meta_color_device_get_id (color_device));

      cd_icc = create_icc_profile_from_edid (color_device,
                                             edid_info, data->file_path,
                                             &error);
    }
  else
    {
      meta_topic (META_DEBUG_COLOR,
                  "Generating sRGB ICC profile for '%s' because EDID is missing",
                  meta_color_device_get_id (color_device));

      cd_icc = cd_icc_new ();

      if (!cd_icc_create_default_full (cd_icc,
                                       CD_ICC_LOAD_FLAGS_PRIMARIES,
                                       &error))
        g_clear_object (&cd_icc);
    }

  if (!cd_icc)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      g_object_unref (task);
      return;
    }

  bytes = cd_icc_save_data (cd_icc, CD_ICC_SAVE_FLAGS_NONE, &error);
  if (!bytes)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      g_object_unref (task);
      return;
    }

  file_md5_checksum = g_compute_checksum_for_bytes (G_CHECKSUM_MD5, bytes);
  cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_FILE_CHECKSUM,
                       file_md5_checksum);

  data->color_calibration =
    meta_color_calibration_new (cd_icc, NULL);
  data->cd_icc = g_steal_pointer (&cd_icc);
  data->bytes = g_steal_pointer (&bytes);
  save_icc_profile (data->file_path, task);
}

static void
set_icc_checksum (CdIcc  *cd_icc,
                  GBytes *bytes)
{
  g_autofree char *md5_checksum = NULL;

  md5_checksum = g_compute_checksum_for_bytes (G_CHECKSUM_MD5, bytes);
  cd_icc_add_metadata (cd_icc, CD_PROFILE_METADATA_FILE_CHECKSUM, md5_checksum);
}

static void
on_efi_panel_color_info_loaded (GObject      *source_object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  GFile *file = G_FILE (source_object);
  g_autoptr (GTask) task = G_TASK (user_data);
  MetaColorDevice *color_device =
    META_COLOR_DEVICE (g_task_get_source_object (task));
  g_autoptr (GError) error = NULL;
  g_autofree char *contents = NULL;
  size_t length;

  if (g_file_load_contents_finish (file, res,
                                   &contents,
                                   &length,
                                   NULL,
                                   &error))
    {
      g_autoptr (CdIcc) calibration_cd_icc = NULL;
      g_autoptr (CdIcc) srgb_cd_icc = NULL;

      meta_topic (META_DEBUG_COLOR,
                  "Generating ICC profile for '%s' from EFI variable",
                  meta_color_device_get_id (color_device));

      srgb_cd_icc = cd_icc_new ();
      if (!cd_icc_create_default_full (srgb_cd_icc,
                                       CD_ICC_LOAD_FLAGS_PRIMARIES,
                                       &error))
        {
          g_warning ("Failed to generate sRGB profile: %s",
                     error->message);
          goto out;
        }

      calibration_cd_icc = cd_icc_new ();
      if (cd_icc_load_data (calibration_cd_icc,
                            (uint8_t *) contents,
                            length,
                            (CD_ICC_LOAD_FLAGS_METADATA |
                             CD_ICC_LOAD_FLAGS_PRIMARIES),
                            &error))
        {
          GenerateProfileData *data = g_task_get_task_data (task);
          const char *file_path = data->file_path;
          g_autoptr (GBytes) calibration_bytes = NULL;
          g_autoptr (GBytes) srgb_bytes = NULL;
          CdMat3x3 csc;

          srgb_bytes = cd_icc_save_data (srgb_cd_icc,
                                         CD_ICC_SAVE_FLAGS_NONE,
                                         &error);
          if (!srgb_bytes)
            {
              g_warning ("Failed to save sRGB profile: %s",
                         error->message);
              goto out;
            }

          calibration_bytes = g_bytes_new_take (g_steal_pointer (&contents), length);

          /* Set metadata needed by colord */

          cd_icc_add_metadata (calibration_cd_icc, CD_PROFILE_PROPERTY_FILENAME,
                               "/dev/null");
          set_icc_checksum (calibration_cd_icc, calibration_bytes);

          cd_icc_add_metadata (srgb_cd_icc, CD_PROFILE_PROPERTY_FILENAME,
                               file_path);
          cd_icc_add_metadata (srgb_cd_icc, CD_PROFILE_PROPERTY_TITLE,
                               "Factory calibrated (sRGB)");
          set_icc_checksum (srgb_cd_icc, srgb_bytes);

          if (!cd_icc_utils_get_adaptation_matrix (calibration_cd_icc,
                                                   srgb_cd_icc,
                                                   &csc,
                                                   &error))
            {
              g_warning ("Failed to calculate adaption matrix: %s",
                         error->message);
              goto out;
            }

          data->color_calibration =
            meta_color_calibration_new (calibration_cd_icc, &csc);
          data->cd_icc = g_steal_pointer (&srgb_cd_icc);
          data->bytes = g_steal_pointer (&srgb_bytes);
          save_icc_profile (file_path, g_steal_pointer (&task));
          return;
        }
      else
        {
          g_warning ("Failed to parse EFI panel color ICC profile: %s",
                     error->message);
        }
    }
  else
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }

      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to read EFI panel color info: %s", error->message);
    }

out:
  create_device_profile_from_edid (color_device, g_steal_pointer (&task));
}

void
meta_set_color_efivar_test_path (const char *path)
{
  efivar_test_path = path;
}

void
meta_color_device_generate_profile (MetaColorDevice     *color_device,
                                    const char          *file_path,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  GTask *task;
  GenerateProfileData *data;

  task = g_task_new (G_OBJECT (color_device), cancellable, callback, user_data);
  g_task_set_source_tag (task, meta_color_device_generate_profile);

  data = g_new0 (GenerateProfileData, 1);
  data->color_device = color_device;
  data->file_path = g_strdup (file_path);
  g_task_set_task_data (task, data,
                        (GDestroyNotify) generate_profile_data_free);

  if ((meta_monitor_is_laptop_panel (color_device->monitor) &&
       meta_monitor_supports_color_transform (color_device->monitor)) ||
      efivar_test_path)
    {
      g_autoptr (GFile) file = NULL;

      if (efivar_test_path)
        file = g_file_new_for_path (efivar_test_path);
      else
        file = g_file_new_for_path (EFI_PANEL_COLOR_INFO_PATH);

      g_file_load_contents_async (file,
                                  cancellable,
                                  on_efi_panel_color_info_loaded,
                                  task);
    }
  else
    {
      create_device_profile_from_edid (color_device, task);
    }
}

MetaColorProfile *
meta_color_device_generate_profile_finish (MetaColorDevice  *color_device,
                                           GAsyncResult     *res,
                                           GError          **error)
{
  g_assert (g_task_get_source_tag (G_TASK (res)) ==
            meta_color_device_generate_profile);
  return g_task_propagate_pointer (G_TASK (res), error);
}

MetaMonitor *
meta_color_device_get_monitor (MetaColorDevice *color_device)
{
  return color_device->monitor;
}

MetaColorProfile *
meta_color_device_get_device_profile (MetaColorDevice *color_device)
{
  return color_device->device_profile;
}

gboolean
meta_color_device_is_ready (MetaColorDevice *color_device)
{
  return color_device->is_ready;
}

MetaColorProfile *
meta_color_device_get_assigned_profile (MetaColorDevice *color_device)
{
  return color_device->assigned_profile;
}

void
meta_color_device_update (MetaColorDevice *color_device,
                          unsigned int     temperature)
{
  MetaColorProfile *color_profile;
  MetaMonitor *monitor;
  size_t lut_size;

  color_profile = meta_color_device_get_assigned_profile (color_device);
  if (!color_profile)
    return;

  monitor = color_device->monitor;
  if (!meta_monitor_is_active (monitor))
    return;

  meta_topic (META_DEBUG_COLOR,
              "Updating device '%s' (%s) using color profile '%s' "
              "and temperature %uK",
              meta_color_device_get_id (color_device),
              meta_monitor_get_connector (monitor),
              meta_color_profile_get_id (color_profile),
              temperature);

  if (meta_monitor_is_laptop_panel (monitor))
    {
      const char *brightness_profile;

      brightness_profile =
        meta_color_profile_get_brightness_profile (color_profile);
      if (brightness_profile)
        {
          meta_topic (META_DEBUG_COLOR,
                      "Setting brightness to %s%% from brightness profile",
                      brightness_profile);
          meta_color_manager_set_brightness (color_device->color_manager,
                                             atoi (brightness_profile));
        }
    }

  lut_size = meta_monitor_get_gamma_lut_size (monitor);
  if (lut_size > 0)
    {
      g_autoptr (MetaGammaLut) lut = NULL;

      lut = meta_color_profile_generate_gamma_lut (color_profile,
                                                   temperature,
                                                   lut_size);

      meta_monitor_set_gamma_lut (monitor, lut);
    }

  g_signal_emit (color_device, signals[UPDATED], 0);
}
