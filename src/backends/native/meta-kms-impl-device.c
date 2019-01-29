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

#include "backends/native/meta-kms-impl-device.h"

#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsImplDevice
{
  GObject parent;

  MetaKmsDevice *device;
  MetaKmsImpl *impl;

  int fd;
};

G_DEFINE_TYPE (MetaKmsImplDevice, meta_kms_impl_device, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_impl_device_get_device (MetaKmsImplDevice *impl_device)
{
  return impl_device->device;
}

MetaKmsImplDevice *
meta_kms_impl_device_new (MetaKmsDevice *device,
                          MetaKmsImpl   *impl,
                          int            fd)
{
  MetaKmsImplDevice *impl_device;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  impl_device = g_object_new (META_TYPE_KMS_IMPL_DEVICE, NULL);
  impl_device->device = device;
  impl_device->impl = impl;
  impl_device->fd = fd;

  return impl_device;
}

int
meta_kms_impl_device_get_fd (MetaKmsImplDevice *impl_device)
{
  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  return impl_device->fd;
}

int
meta_kms_impl_device_leak_fd (MetaKmsImplDevice *impl_device)
{
  return impl_device->fd;
}

int
meta_kms_impl_device_close (MetaKmsImplDevice *impl_device)
{
  int fd;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl_device->impl));

  fd = impl_device->fd;
  impl_device->fd = -1;

  return fd;
}

static void
meta_kms_impl_device_init (MetaKmsImplDevice *device)
{
}

static void
meta_kms_impl_device_class_init (MetaKmsImplDeviceClass *klass)
{
}

