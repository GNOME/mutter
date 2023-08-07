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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/native/meta-kms-mode-private.h"

#include "backends/native/meta-kms-impl-device.h"

struct _MetaKmsMode
{
  MetaKmsImplDevice *impl_device;
  MetaKmsModeFlag flags;
  drmModeModeInfo drm_mode;
};

int
meta_kms_mode_get_width (MetaKmsMode *mode)
{
  return mode->drm_mode.hdisplay;
}

int
meta_kms_mode_get_height (MetaKmsMode *mode)
{
  return mode->drm_mode.vdisplay;
}

uint32_t
meta_kms_mode_create_blob_id (MetaKmsMode  *mode,
                              GError      **error)
{
  int fd;
  int ret;
  uint32_t blob_id;

  fd = meta_kms_impl_device_get_fd (mode->impl_device);

  ret = drmModeCreatePropertyBlob (fd,
                                   &mode->drm_mode,
                                   sizeof (mode->drm_mode),
                                   &blob_id);
  if (ret < 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (-ret),
                   "drmModeCreatePropertyBlob: %s",
                   g_strerror (-ret));
      return 0;
    }

  return blob_id;
}

const char *
meta_kms_mode_get_name (MetaKmsMode *mode)
{
  return mode->drm_mode.name;
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

gboolean
meta_drm_mode_equal (const drmModeModeInfo *one,
                     const drmModeModeInfo *two)
{
  return (one->clock == two->clock &&
          one->hdisplay == two->hdisplay &&
          one->hsync_start == two->hsync_start &&
          one->hsync_end == two->hsync_end &&
          one->htotal == two->htotal &&
          one->hskew == two->hskew &&
          one->vdisplay == two->vdisplay &&
          one->vsync_start == two->vsync_start &&
          one->vsync_end == two->vsync_end &&
          one->vtotal == two->vtotal &&
          one->vscan == two->vscan &&
          one->vrefresh == two->vrefresh &&
          one->flags == two->flags &&
          one->type == two->type &&
          strncmp (one->name, two->name, DRM_DISPLAY_MODE_LEN) == 0);
}

gboolean
meta_kms_mode_equal (MetaKmsMode *mode,
                     MetaKmsMode *other_mode)
{
  return meta_drm_mode_equal (&mode->drm_mode, &other_mode->drm_mode);
}

unsigned int
meta_kms_mode_hash (MetaKmsMode *mode)
{
  const drmModeModeInfo *drm_mode = &mode->drm_mode;
  unsigned int hash = 0;

  /*
   * We don't include the name in the hash because it's generally
   * derived from the other fields (hdisplay, vdisplay and flags)
   */

  hash ^= drm_mode->clock;
  hash ^= drm_mode->hdisplay ^ drm_mode->hsync_start ^ drm_mode->hsync_end;
  hash ^= drm_mode->vdisplay ^ drm_mode->vsync_start ^ drm_mode->vsync_end;
  hash ^= drm_mode->vrefresh;
  hash ^= drm_mode->flags ^ drm_mode->type;

  return hash;
}

MetaKmsMode *
meta_kms_mode_clone (MetaKmsMode *mode)
{
  MetaKmsMode *new_mode;

  new_mode = g_new0 (MetaKmsMode, 1);
  *new_mode = *mode;

  return new_mode;
}

void
meta_kms_mode_free (MetaKmsMode *mode)
{
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
