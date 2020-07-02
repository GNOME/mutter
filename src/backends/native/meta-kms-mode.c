/*
 * Copyright (C) 2020 Red Hat
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

#include "backends/native/meta-kms-mode-private.h"

#include "backends/native/meta-kms-impl-device.h"

struct _MetaKmsMode
{
  MetaKmsImplDevice *impl_device;
  MetaKmsModeFlag flags;
  drmModeModeInfo drm_mode;
  uint32_t blob_id;
};

uint32_t
meta_kms_mode_ensure_blob_id (MetaKmsMode  *mode,
                              GError      **error)
{
  int fd;
  int ret;

  fd = meta_kms_impl_device_get_fd (mode->impl_device);

  ret = drmModeCreatePropertyBlob (fd,
                                   &mode->drm_mode,
                                   sizeof (mode->drm_mode),
                                   &mode->blob_id);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeCreatePropertyBlob: %s",
                   g_strerror (-ret));
      return 0;
    }

  return mode->blob_id;
}

MetaKmsModeFlag
meta_kms_mode_get_flags (MetaKmsMode *mode)
{
  return mode->flags;
}

const drmModeModeInfo *
meta_kms_mode_get_drm_mode (MetaKmsMode *mode)
{
  return &mode->drm_mode;
}

void
meta_kms_mode_free (MetaKmsMode *mode)
{
  if (mode->blob_id)
    {
      int fd;

      fd = meta_kms_impl_device_get_fd (mode->impl_device);

      drmModeDestroyPropertyBlob (fd, mode->blob_id);
    }

  g_free (mode);
}

MetaKmsMode *
meta_kms_mode_new (MetaKmsImplDevice     *impl_device,
                   const drmModeModeInfo *drm_mode,
                   MetaKmsModeFlag        flags)
{
  MetaKmsMode *mode;

  mode = g_new0 (MetaKmsMode, 1);
  mode->impl_device = impl_device;
  mode->flags = flags;
  mode->drm_mode = *drm_mode;

  return mode;
}
