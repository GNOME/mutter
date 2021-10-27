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

#include "backends/meta-color-manager-private.h"
#include "backends/meta-monitor.h"

struct _MetaColorDevice
{
  GObject parent;

  MetaColorManager *color_manager;

  char *cd_device_id;
  MetaMonitor *monitor;
  CdDevice *cd_device;

  GCancellable *cancellable;
};

G_DEFINE_TYPE (MetaColorDevice, meta_color_device,
               G_TYPE_OBJECT)

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

  g_cancellable_cancel (color_device->cancellable);
  g_clear_object (&color_device->cancellable);

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
}

static void
meta_color_device_init (MetaColorDevice *color_device)
{
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

      g_warning ("Failed to connect to colord device %s: %s",
                 color_device->cd_device_id,
                 error->message);
      return;
    }
}

static void
on_cd_device_created (GObject      *object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  CdClient *cd_client = CD_CLIENT (object);
  MetaColorDevice *color_device = user_data;
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
      return;
    }

  color_device->cd_device = cd_device;

  cd_device_connect (cd_device, color_device->cancellable,
                     on_cd_device_connected, color_device);
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
