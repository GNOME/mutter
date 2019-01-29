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

#include <xf86drm.h>

#include "backends/native/meta-kms-crtc-private.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl.h"
#include "backends/native/meta-kms-private.h"

struct _MetaKmsImplDevice
{
  GObject parent;

  MetaKmsDevice *device;
  MetaKmsImpl *impl;

  int fd;

  GList *crtcs;
};

G_DEFINE_TYPE (MetaKmsImplDevice, meta_kms_impl_device, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_impl_device_get_device (MetaKmsImplDevice *impl_device)
{
  return impl_device->device;
}

GList *
meta_kms_impl_device_copy_crtcs (MetaKmsImplDevice *impl_device)
{
  return g_list_copy (impl_device->crtcs);
}

static void
init_crtcs (MetaKmsImplDevice *impl_device,
            drmModeRes        *drm_resources)
{
  int idx;

  for (idx = 0; idx < drm_resources->count_crtcs; idx++)
    {
      drmModeCrtc *drm_crtc;
      MetaKmsCrtc *crtc;

      drm_crtc = drmModeGetCrtc (impl_device->fd, drm_resources->crtcs[idx]);
      crtc = meta_kms_crtc_new (impl_device, drm_crtc, idx);
      drmModeFreeCrtc (drm_crtc);

      impl_device->crtcs = g_list_prepend (impl_device->crtcs, crtc);
    }
  impl_device->crtcs = g_list_reverse (impl_device->crtcs);
}

MetaKmsImplDevice *
meta_kms_impl_device_new (MetaKmsDevice *device,
                          MetaKmsImpl   *impl,
                          int            fd)
{
  MetaKmsImplDevice *impl_device;
  drmModeRes *drm_resources;

  meta_assert_in_kms_impl (meta_kms_impl_get_kms (impl));

  impl_device = g_object_new (META_TYPE_KMS_IMPL_DEVICE, NULL);
  impl_device->device = device;
  impl_device->impl = impl;
  impl_device->fd = fd;

  drm_resources = drmModeGetResources (fd);

  init_crtcs (impl_device, drm_resources);

  drmModeFreeResources (drm_resources);

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
meta_kms_impl_device_finalize (GObject *object)
{
  MetaKmsImplDevice *impl_device = META_KMS_IMPL_DEVICE (object);

  g_list_free_full (impl_device->crtcs, g_object_unref);

  G_OBJECT_CLASS (meta_kms_impl_device_parent_class)->finalize (object);
}

static void
meta_kms_impl_device_init (MetaKmsImplDevice *device)
{
}

static void
meta_kms_impl_device_class_init (MetaKmsImplDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_impl_device_finalize;
}

