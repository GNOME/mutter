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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>
#include <stdint.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-types.h"
#include "backends/meta-backend-types.h"
#include "core/util-private.h"
#include "meta/boxes.h"

typedef struct _MetaKmsCrtcState
{
  gboolean is_active;

  MtkRectangle rect;
  gboolean is_drm_mode_valid;
  drmModeModeInfo drm_mode;

  struct {
    gboolean enabled;
    gboolean supported;
  } vrr;

  struct {
    MetaGammaLut *value;
    int size;
    gboolean supported;
  } gamma;
} MetaKmsCrtcState;

#define META_TYPE_KMS_CRTC (meta_kms_crtc_get_type ())
META_EXPORT_TEST
G_DECLARE_FINAL_TYPE (MetaKmsCrtc, meta_kms_crtc,
                      META, KMS_CRTC,
                      GObject)

META_EXPORT_TEST
MetaKmsDevice * meta_kms_crtc_get_device (MetaKmsCrtc *crtc);

META_EXPORT_TEST
const MetaKmsCrtcState * meta_kms_crtc_get_current_state (MetaKmsCrtc *crtc);

META_EXPORT_TEST
uint32_t meta_kms_crtc_get_id (MetaKmsCrtc *crtc);

int meta_kms_crtc_get_idx (MetaKmsCrtc *crtc);

META_EXPORT_TEST
gboolean meta_kms_crtc_is_active (MetaKmsCrtc *crtc);
