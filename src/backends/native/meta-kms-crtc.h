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

#ifndef META_KMS_CRTC_H
#define META_KMS_CRTC_H

#include <glib-object.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-types.h"
#include "meta/boxes.h"

typedef struct _MetaKmsCrtcState
{
  MetaRectangle rect;
  gboolean is_drm_mode_valid;
  drmModeModeInfo drm_mode;

  uint32_t common_possible_crtcs;
  uint32_t common_possible_clones;
  uint32_t encoder_device_idxs;
} MetaKmsCrtcState;

#define META_TYPE_KMS_CRTC (meta_kms_crtc_get_type ())
G_DECLARE_FINAL_TYPE (MetaKmsCrtc, meta_kms_crtc,
                      META, KMS_CRTC,
                      GObject)

MetaKmsDevice * meta_kms_crtc_get_device (MetaKmsCrtc *crtc);

const MetaKmsCrtcState * meta_kms_crtc_get_current_state (MetaKmsCrtc *crtc);

uint32_t meta_kms_crtc_get_id (MetaKmsCrtc *crtc);

int meta_kms_crtc_get_idx (MetaKmsCrtc *crtc);

#endif /* META_KMS_CRTC_H */
