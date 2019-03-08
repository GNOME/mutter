/*
 * Copyright (C) 2018 Red Hat
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

#include "backends/native/meta-kms-private.h"

#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-impl-simple.h"
#include "backends/native/meta-udev.h"

struct _MetaKms
{
  GObject parent;

  MetaBackend *backend;

  guint hotplug_handler_id;

  MetaKmsImpl *impl;
  gboolean in_impl_task;

  GList *devices;
};

G_DEFINE_TYPE (MetaKms, meta_kms, G_TYPE_OBJECT)

gboolean
meta_kms_run_impl_task_sync (MetaKms              *kms,
                             MetaKmsImplTaskFunc   func,
                             gpointer              user_data,
                             GError              **error)
{
  gboolean ret;

  kms->in_impl_task = TRUE;
  ret = func (kms->impl, user_data, error);
  kms->in_impl_task = FALSE;

  return ret;
}

gboolean
meta_kms_in_impl_task (MetaKms *kms)
{
  return kms->in_impl_task;
}

static gboolean
update_states_in_impl (MetaKmsImpl  *impl,
                       gpointer      user_data,
                       GError      **error)
{
  MetaKms *kms = user_data;
  GList *l;

  for (l = kms->devices; l; l = l->next)
    {
      MetaKmsDevice *device = l->data;
      MetaKmsImplDevice *impl_device = meta_kms_device_get_impl_device (device);

      meta_kms_impl_device_update_states (impl_device);
    }

  return TRUE;
}

static gboolean
meta_kms_update_states_sync (MetaKms  *kms,
                             GError  **error)
{
  return meta_kms_run_impl_task_sync (kms,
                                      update_states_in_impl,
                                      kms,
                                      error);
}

static void
on_udev_hotplug (MetaUdev *udev,
                 MetaKms  *kms)
{
  g_autoptr (GError) error = NULL;

  if (!meta_kms_update_states_sync (kms, &error))
    g_warning ("Updating KMS state failed: %s", error->message);
}

MetaBackend *
meta_kms_get_backend (MetaKms *kms)
{
  return kms->backend;
}

MetaKmsDevice *
meta_kms_create_device (MetaKms            *kms,
                        const char         *path,
                        MetaKmsDeviceFlag   flags,
                        GError            **error)
{
  MetaKmsDevice *device;

  device = meta_kms_device_new (kms, path, flags, error);
  if (!device)
    return NULL;

  kms->devices = g_list_append (kms->devices, device);

  return device;
}

MetaKms *
meta_kms_new (MetaBackend  *backend,
              GError      **error)
{
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaUdev *udev = meta_backend_native_get_udev (backend_native);
  MetaKms *kms;

  kms = g_object_new (META_TYPE_KMS, NULL);
  kms->backend = backend;
  kms->impl = META_KMS_IMPL (meta_kms_impl_simple_new (kms, error));
  if (!kms->impl)
    {
      g_object_unref (kms);
      return NULL;
    }

  kms->hotplug_handler_id =
    g_signal_connect (udev, "hotplug", G_CALLBACK (on_udev_hotplug), kms);

  return kms;
}

static void
meta_kms_finalize (GObject *object)
{
  MetaKms *kms = META_KMS (object);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (kms->backend);
  MetaUdev *udev = meta_backend_native_get_udev (backend_native);

  g_list_free_full (kms->devices, g_object_unref);

  if (kms->hotplug_handler_id)
    g_signal_handler_disconnect (udev, kms->hotplug_handler_id);

  G_OBJECT_CLASS (meta_kms_parent_class)->finalize (object);
}

static void
meta_kms_init (MetaKms *kms)
{
}

static void
meta_kms_class_init (MetaKmsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_finalize;
}
