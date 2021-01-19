/*
 * Copyright (C) 2019 Red Hat
 * Copyright (C) 2019 DisplayLink (UK) Ltd.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-device.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <xf86drm.h>

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-impl-device-atomic.h"
#include "backends/native/meta-kms-impl-device-dummy.h"
#include "backends/native/meta-kms-impl-device-simple.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsDevice
{
  GObject parent;

  MetaKms *kms;

  MetaKmsImplDevice *impl_device;

  MetaKmsDeviceFlag flags;
  char *path;
  char *driver_name;
  char *driver_description;

  GList *crtcs;
  GList *connectors;
  GList *planes;

  MetaKmsDeviceCaps caps;

  GList *fallback_modes;
};

G_DEFINE_TYPE (MetaKmsDevice, meta_kms_device, G_TYPE_OBJECT);

MetaKms *
meta_kms_device_get_kms (MetaKmsDevice *device)
{
  return device->kms;
}

MetaKmsImplDevice *
meta_kms_device_get_impl_device (MetaKmsDevice *device)
{
  return device->impl_device;
}

int
meta_kms_device_leak_fd (MetaKmsDevice *device)
{
  return meta_kms_impl_device_leak_fd (device->impl_device);
}

const char *
meta_kms_device_get_path (MetaKmsDevice *device)
{
  return device->path;
}

const char *
meta_kms_device_get_driver_name (MetaKmsDevice *device)
{
  return device->driver_name;
}

const char *
meta_kms_device_get_driver_description (MetaKmsDevice *device)
{
  return device->driver_description;
}

MetaKmsDeviceFlag
meta_kms_device_get_flags (MetaKmsDevice *device)
{
  return device->flags;
}

gboolean
meta_kms_device_get_cursor_size (MetaKmsDevice *device,
                                 uint64_t      *out_cursor_width,
                                 uint64_t      *out_cursor_height)
{
  if (device->caps.has_cursor_size)
    {
      *out_cursor_width = device->caps.cursor_width;
      *out_cursor_height = device->caps.cursor_height;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

GList *
meta_kms_device_get_connectors (MetaKmsDevice *device)
{
  return device->connectors;
}

GList *
meta_kms_device_get_crtcs (MetaKmsDevice *device)
{
  return device->crtcs;
}

GList *
meta_kms_device_get_planes (MetaKmsDevice *device)
{
  return device->planes;
}

static MetaKmsPlane *
get_plane_with_type_for (MetaKmsDevice    *device,
                         MetaKmsCrtc      *crtc,
                         MetaKmsPlaneType  type)
{
  GList *l;

  for (l = meta_kms_device_get_planes (device); l; l = l->next)
    {
      MetaKmsPlane *plane = l->data;

      if (meta_kms_plane_get_plane_type (plane) != type)
        continue;

      if (meta_kms_plane_is_usable_with (plane, crtc))
        return plane;
    }

  return NULL;
}

MetaKmsPlane *
meta_kms_device_get_primary_plane_for (MetaKmsDevice *device,
                                       MetaKmsCrtc   *crtc)
{
  return get_plane_with_type_for (device, crtc, META_KMS_PLANE_TYPE_PRIMARY);
}

MetaKmsPlane *
meta_kms_device_get_cursor_plane_for (MetaKmsDevice *device,
                                      MetaKmsCrtc   *crtc)
{
  return get_plane_with_type_for (device, crtc, META_KMS_PLANE_TYPE_CURSOR);
}

GList *
meta_kms_device_get_fallback_modes (MetaKmsDevice *device)
{
  return device->fallback_modes;
}

void
meta_kms_device_update_states_in_impl (MetaKmsDevice *device)
{
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  meta_assert_in_kms_impl (device->kms);
  meta_assert_is_waiting_for_kms_impl_task (device->kms);

  meta_kms_impl_device_update_states (impl_device);

  g_list_free (device->crtcs);
  device->crtcs = meta_kms_impl_device_copy_crtcs (impl_device);

  g_list_free (device->connectors);
  device->connectors = meta_kms_impl_device_copy_connectors (impl_device);

  g_list_free (device->planes);
  device->planes = meta_kms_impl_device_copy_planes (impl_device);
}

void
meta_kms_device_predict_states_in_impl (MetaKmsDevice *device,
                                        MetaKmsUpdate *update)
{
  MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

  meta_assert_in_kms_impl (device->kms);

  meta_kms_impl_device_predict_states (impl_device, update);
}

void
meta_kms_device_add_fake_plane_in_impl (MetaKmsDevice    *device,
                                        MetaKmsPlaneType  plane_type,
                                        MetaKmsCrtc      *crtc)
{
  MetaKmsImplDevice *impl_device = device->impl_device;
  MetaKmsPlane *plane;

  meta_assert_in_kms_impl (device->kms);

  plane = meta_kms_impl_device_add_fake_plane (impl_device,
                                               plane_type,
                                               crtc);
  device->planes = g_list_append (device->planes, plane);
}

typedef struct _CreateImplDeviceData
{
  MetaKmsDevice *device;
  int fd;
  const char *path;
  MetaKmsDeviceFlag flags;

  MetaKmsImplDevice *out_impl_device;
  GList *out_crtcs;
  GList *out_connectors;
  GList *out_planes;
  MetaKmsDeviceCaps out_caps;
  GList *out_fallback_modes;
  char *out_driver_name;
  char *out_driver_description;
  char *out_path;
} CreateImplDeviceData;

static gboolean
is_atomic_allowed (const char *driver_name)
{
  const char *atomic_driver_deny_list[] = {
    "qxl",
    "vmwgfx",
    "vboxvideo",
    "nvidia-drm",
    NULL,
  };

  return !g_strv_contains (atomic_driver_deny_list, driver_name);
}

static gboolean
get_driver_info (int    fd,
                 char **name,
                 char **description)
{
  drmVersion *drm_version;

  drm_version = drmGetVersion (fd);
  if (!drm_version)
    return FALSE;

  *name = g_strndup (drm_version->name,
                     drm_version->name_len);
  *description = g_strndup (drm_version->desc,
                            drm_version->desc_len);
  drmFreeVersion (drm_version);

  return TRUE;
}

static MetaKmsImplDevice *
meta_create_kms_impl_device (MetaKmsDevice      *device,
                             MetaKmsImpl        *impl,
                             int                 fd,
                             const char         *path,
                             MetaKmsDeviceFlag   flags,
                             GError            **error)
{
  GType impl_device_type;
  gboolean supports_atomic_mode_setting;
  g_autofree char *driver_name = NULL;
  g_autofree char *driver_description = NULL;
  const char *atomic_kms_enable_env;
  int impl_fd;
  g_autofree char *impl_path = NULL;
  MetaKmsImplDevice *impl_device;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  if (!get_driver_info (fd, &driver_name, &driver_description))
    {
      driver_name = g_strdup ("unknown");
      driver_description = g_strdup ("Unknown");
    }

  atomic_kms_enable_env = getenv ("MUTTER_DEBUG_ENABLE_ATOMIC_KMS");

  if (flags & META_KMS_DEVICE_FLAG_NO_MODE_SETTING)
    {
      g_autofree char *render_node_path = NULL;

      render_node_path = drmGetRenderDeviceNameFromFd (fd);
      if (!render_node_path)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Couldn't find render node device for '%s' (%s)",
                       path, driver_name);
          return NULL;
        }

      impl_fd = open (render_node_path, O_RDWR | O_CLOEXEC, 0);
      if (impl_fd == -1)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to open render node '%s': %s",
                       render_node_path, g_strerror (errno));
          return NULL;
        }

      g_message ("Adding device '%s' (from '%s', %s) using no mode setting.",
                 render_node_path, path, driver_name);

      impl_path = g_steal_pointer (&render_node_path);
      impl_device_type = META_TYPE_KMS_IMPL_DEVICE_DUMMY;
    }
  else if (atomic_kms_enable_env)
    {
      if (g_strcmp0 (atomic_kms_enable_env, "1") == 0)
        {
          impl_device_type = META_TYPE_KMS_IMPL_DEVICE_ATOMIC;
          if (drmSetClientCap (fd, DRM_CLIENT_CAP_ATOMIC, 1) != 0)
            {
              g_error ("Failed to force atomic mode setting on '%s' (%s).",
                       path, driver_name);
            }
        }
      else if (g_strcmp0 (atomic_kms_enable_env, "0") == 0)
        {
          impl_device_type = META_TYPE_KMS_IMPL_DEVICE_SIMPLE;
        }
      else
        {
          g_error ("Invalid value '%s' for MUTTER_DEBUG_ENABLE_ATOMIC_KMS, "
                   "bailing.",
                   atomic_kms_enable_env);
        }

      impl_fd = dup (fd);
      impl_path = g_strdup (path);

      g_message ("Mode setting implementation for '%s' (%s) forced (%s).",
                 path, driver_name,
                 impl_device_type == META_TYPE_KMS_IMPL_DEVICE_ATOMIC ?
                   "atomic" : "non-atomic");
    }
  else if (!is_atomic_allowed (driver_name))
    {
      g_message ("Adding device '%s' (%s) using non-atomic mode setting"
                 " (using atomic mode setting not allowed).",
                 path, driver_name);
      impl_fd = dup (fd);
      impl_path = g_strdup (path);
      impl_device_type = META_TYPE_KMS_IMPL_DEVICE_SIMPLE;
    }
  else
    {
      int ret;

      ret = drmSetClientCap (fd, DRM_CLIENT_CAP_ATOMIC, 1);
      if (ret == 0)
        supports_atomic_mode_setting = TRUE;
      else
        supports_atomic_mode_setting = FALSE;

      if (supports_atomic_mode_setting)
        {
          g_message ("Adding device '%s' (%s) using atomic mode setting.",
                     path, driver_name);
          impl_device_type = META_TYPE_KMS_IMPL_DEVICE_ATOMIC;
        }
      else
        {
          g_message ("Adding device '%s' (%s) using non-atomic mode setting.",
                     path, driver_name);
          impl_device_type = META_TYPE_KMS_IMPL_DEVICE_SIMPLE;
        }

      impl_fd = dup (fd);
      impl_path = g_strdup (path);
    }

  impl_device = g_initable_new (impl_device_type, NULL, error,
                                "device", device,
                                "impl", impl,
                                "fd", impl_fd,
                                "path", impl_path,
                                "flags", flags,
                                "driver-name", driver_name,
                                "driver-description", driver_description,
                                NULL);
  if (!impl_device)
    {
      close (impl_fd);
      return NULL;
    }

  close (fd);

  return impl_device;
}

static gpointer
create_impl_device_in_impl (MetaKmsImpl  *impl,
                            gpointer      user_data,
                            GError      **error)
{
  CreateImplDeviceData *data = user_data;
  MetaKmsImplDevice *impl_device;

  impl_device = meta_create_kms_impl_device (data->device,
                                             impl,
                                             data->fd,
                                             data->path,
                                             data->flags,
                                             error);
  if (!impl_device)
    return FALSE;

  meta_kms_impl_add_impl_device (impl, impl_device);

  data->out_impl_device = impl_device;
  data->out_crtcs = meta_kms_impl_device_copy_crtcs (impl_device);
  data->out_connectors = meta_kms_impl_device_copy_connectors (impl_device);
  data->out_planes = meta_kms_impl_device_copy_planes (impl_device);
  data->out_caps = *meta_kms_impl_device_get_caps (impl_device);
  data->out_fallback_modes =
    meta_kms_impl_device_copy_fallback_modes (impl_device);
  data->out_driver_name =
    g_strdup (meta_kms_impl_device_get_driver_name (impl_device));
  data->out_driver_description =
    g_strdup (meta_kms_impl_device_get_driver_description (impl_device));
  data->out_path = g_strdup (meta_kms_impl_device_get_path (impl_device));

  return GINT_TO_POINTER (TRUE);
}

MetaKmsDevice *
meta_kms_device_new (MetaKms            *kms,
                     const char         *path,
                     MetaKmsDeviceFlag   flags,
                     GError            **error)
{
  MetaBackend *backend = meta_kms_get_backend (kms);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaLauncher *launcher = meta_backend_native_get_launcher (backend_native);
  MetaKmsDevice *device;
  CreateImplDeviceData data;
  int fd;

  if (flags & META_KMS_DEVICE_FLAG_NO_MODE_SETTING)
    {
      fd = open (path, O_RDWR | O_CLOEXEC, 0);
      if (fd == -1)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "Failed to open DRM device: %s", g_strerror (errno));
          return NULL;
        }
    }
  else
    {
      fd = meta_launcher_open_restricted (launcher, path, error);
      if (fd == -1)
        return NULL;
    }

  device = g_object_new (META_TYPE_KMS_DEVICE, NULL);
  device->kms = kms;

  data = (CreateImplDeviceData) {
    .device = device,
    .fd = fd,
    .path = path,
    .flags = flags,
  };
  if (!meta_kms_run_impl_task_sync (kms, create_impl_device_in_impl, &data,
                                    error))
    {
      if (flags & META_KMS_DEVICE_FLAG_NO_MODE_SETTING)
        close (fd);
      else
        meta_launcher_close_restricted (launcher, fd);
      g_object_unref (device);
      return NULL;
    }

  device->impl_device = data.out_impl_device;
  device->flags = flags;
  device->path = g_strdup (path);
  device->crtcs = data.out_crtcs;
  device->connectors = data.out_connectors;
  device->planes = data.out_planes;
  device->caps = data.out_caps;
  device->fallback_modes = data.out_fallback_modes;
  device->driver_name = data.out_driver_name;
  device->driver_description = data.out_driver_description;
  free (device->path);
  device->path = data.out_path;

  return device;
}

typedef struct _FreeImplDeviceData
{
  MetaKmsImplDevice *impl_device;

  int out_fd;
} FreeImplDeviceData;

static gpointer
free_impl_device_in_impl (MetaKmsImpl  *impl,
                          gpointer      user_data,
                          GError      **error)
{
  FreeImplDeviceData *data = user_data;
  MetaKmsImplDevice *impl_device = data->impl_device;
  int fd;

  fd = meta_kms_impl_device_close (impl_device);
  g_object_unref (impl_device);

  data->out_fd = fd;

  return GINT_TO_POINTER (TRUE);
}

static void
meta_kms_device_finalize (GObject *object)
{
  MetaKmsDevice *device = META_KMS_DEVICE (object);
  MetaBackend *backend = meta_kms_get_backend (device->kms);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaLauncher *launcher = meta_backend_native_get_launcher (backend_native);

  g_free (device->path);
  g_list_free (device->crtcs);
  g_list_free (device->connectors);
  g_list_free (device->planes);

  if (device->impl_device)
    {
      FreeImplDeviceData data;
      GError *error = NULL;

      data = (FreeImplDeviceData) {
        .impl_device = device->impl_device,
      };
      if (!meta_kms_run_impl_task_sync (device->kms, free_impl_device_in_impl, &data,
                                        &error))
        {
          g_warning ("Failed to close KMS impl device: %s", error->message);
          g_error_free (error);
        }
      else
        {
          if (device->flags & META_KMS_DEVICE_FLAG_NO_MODE_SETTING)
            close (data.out_fd);
          else
            meta_launcher_close_restricted (launcher, data.out_fd);
        }
    }
  G_OBJECT_CLASS (meta_kms_device_parent_class)->finalize (object);
}

static void
meta_kms_device_init (MetaKmsDevice *device)
{
}

static void
meta_kms_device_class_init (MetaKmsDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_device_finalize;
}
