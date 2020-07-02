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

#ifndef META_KMS_MODE_H
#define META_KMS_MODE_H

#include <glib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-types.h"

typedef enum _MetaKmsModeFlag
{
  META_KMS_MODE_FLAG_NONE = 0,
  META_KMS_MODE_FLAG_FALLBACK_LANDSCAPE = 1 << 0,
  META_KMS_MODE_FLAG_FALLBACK_PORTRAIT = 1 << 1,
} MetaKmsModeFlag;

const char * meta_kms_mode_get_name (MetaKmsMode *mode);

MetaKmsModeFlag meta_kms_mode_get_flags (MetaKmsMode *mode);

const drmModeModeInfo * meta_kms_mode_get_drm_mode (MetaKmsMode *mode);

gboolean meta_kms_mode_equal (MetaKmsMode *mode,
                              MetaKmsMode *other_mode);

unsigned int meta_kms_mode_hash (MetaKmsMode *mode);

#endif /* META_KMS_MODE_H */
