/*
 * Copyright (C) 2021 Red Hat
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

#include "backends/native/meta-kms-impl-device-dummy.h"

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-kms.h"

struct _MetaKmsImplDeviceDummy
{
  MetaKmsImplDevice parent;
};

static GInitableIface *initable_parent_iface;

static void
initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MetaKmsImplDeviceDummy,
                         meta_kms_impl_device_dummy,
                         META_TYPE_KMS_IMPL_DEVICE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_iface_init))

static void
meta_kms_impl_device_dummy_discard_pending_page_flips (MetaKmsImplDevice *impl_device)
{
}

static void
meta_kms_impl_device_dummy_disable (MetaKmsImplDevice *impl_device)
{
}

static MetaDeviceFile *
meta_kms_impl_device_dummy_open_device_file (MetaKmsImplDevice  *impl_device,
                                             const char         *path,
                                             GError            **error)
{
  MetaKmsDevice *device = meta_kms_impl_device_get_device (impl_device);
  MetaKms *kms = meta_kms_device_get_kms (device);
  MetaBackend *backend = meta_kms_get_backend (kms);
  MetaDevicePool *device_pool =
    meta_backend_native_get_device_pool (META_BACKEND_NATIVE (backend));

  return meta_device_pool_open (device_pool, path,
                                META_DEVICE_FILE_FLAG_NONE,
                                error);
}

static gboolean
meta_kms_impl_device_dummy_initable_init (GInitable     *initable,
                                          GCancellable  *cancellable,
                                          GError       **error)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (initable);

  if (!initable_parent_iface->init (initable, cancellable, error))
    return FALSE;

  g_message ("Added device '%s' (%s) using no mode setting.",
             meta_kms_impl_device_get_path (impl_device),
             meta_kms_impl_device_get_driver_name (impl_device));

  return TRUE;
}

static void
initable_iface_init (GInitableIface *iface)
{
  initable_parent_iface = g_type_interface_peek_parent (iface);

  iface->init = meta_kms_impl_device_dummy_initable_init;
}

static void
meta_kms_impl_device_dummy_init (MetaKmsImplDeviceDummy *impl_device_dummy)
{
}

static void
meta_kms_impl_device_dummy_class_init (MetaKmsImplDeviceDummyClass *klass)
{
  MetaKmsImplDeviceClass *impl_device_class =
    META_KMS_IMPL_DEVICE_CLASS (klass);

  impl_device_class->open_device_file =
    meta_kms_impl_device_dummy_open_device_file;
  impl_device_class->discard_pending_page_flips =
    meta_kms_impl_device_dummy_discard_pending_page_flips;
  impl_device_class->disable =
    meta_kms_impl_device_dummy_disable;
}
