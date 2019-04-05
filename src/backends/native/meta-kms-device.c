/*
 * Copyright (C) 2019 Red Hat
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

#include "backends/native/meta-kms-device.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsDevice
{
  GObject parent;

  MetaKms *kms;

  MetaKmsImplDevice *impl_device;

  MetaKmsDeviceFlag flags;
  char *path;
};

G_DEFINE_TYPE (MetaKmsDevice, meta_kms_device, G_TYPE_OBJECT);

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

MetaKmsDeviceFlag
meta_kms_device_get_flags (MetaKmsDevice *device)
{
  return device->flags;
}

typedef struct _CreateImplDeviceData
{
  MetaKmsDevice *device;
  int fd;

  MetaKmsImplDevice *out_impl_device;
} CreateImplDeviceData;

static gboolean
create_impl_device_in_impl (MetaKmsImpl  *impl,
                            gpointer      user_data,
                            GError      **error)
{
  CreateImplDeviceData *data = user_data;
  MetaKmsImplDevice *impl_device;

  impl_device = meta_kms_impl_device_new (data->device, impl, data->fd);

  data->out_impl_device = impl_device;

  return TRUE;
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

  fd = meta_launcher_open_restricted (launcher, path, error);
  if (fd == -1)
    return NULL;

  device = g_object_new (META_TYPE_KMS_DEVICE, NULL);

  data = (CreateImplDeviceData) {
    .device = device,
    .fd = fd,
  };
  if (!meta_kms_run_impl_task_sync (kms, create_impl_device_in_impl, &data,
                                    error))
    {
      meta_launcher_close_restricted (launcher, fd);
      g_object_unref (device);
      return NULL;
    }

  device->kms = kms;
  device->impl_device = data.out_impl_device;
  device->flags = flags;
  device->path = g_strdup (path);

  return device;
}

typedef struct _FreeImplDeviceData
{
  MetaKmsImplDevice *impl_device;

  int out_fd;
} FreeImplDeviceData;

static gboolean
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

  return TRUE;
}

static void
meta_kms_device_finalize (GObject *object)
{
  MetaKmsDevice *device = META_KMS_DEVICE (object);
  MetaBackend *backend = meta_kms_get_backend (device->kms);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaLauncher *launcher = meta_backend_native_get_launcher (backend_native);
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
      meta_launcher_close_restricted (launcher, data.out_fd);
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
