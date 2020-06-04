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
  drmModeModeInfo drm_mode;
};

const drmModeModeInfo *
meta_kms_mode_get_drm_mode (MetaKmsMode *mode)
{
  return &mode->drm_mode;
}

void
meta_kms_mode_free (MetaKmsMode *mode)
{
  g_free (mode);
}

MetaKmsMode *
meta_kms_mode_new (MetaKmsImplDevice     *impl_device,
                   const drmModeModeInfo *drm_mode)
{
  MetaKmsMode *mode;

  mode = g_new0 (MetaKmsMode, 1);
  mode->impl_device = impl_device;
  mode->drm_mode = *drm_mode;

  return mode;
}
