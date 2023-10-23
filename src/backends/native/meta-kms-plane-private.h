/*
 * Copyright (C) 2018-2019 Red Hat
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

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms-types.h"

typedef enum _MetaKmsPlaneProp
{
  META_KMS_PLANE_PROP_TYPE = 0,
  META_KMS_PLANE_PROP_ROTATION,
  META_KMS_PLANE_PROP_IN_FORMATS,
  META_KMS_PLANE_PROP_SRC_X,
  META_KMS_PLANE_PROP_SRC_Y,
  META_KMS_PLANE_PROP_SRC_W,
  META_KMS_PLANE_PROP_SRC_H,
  META_KMS_PLANE_PROP_CRTC_X,
  META_KMS_PLANE_PROP_CRTC_Y,
  META_KMS_PLANE_PROP_CRTC_W,
  META_KMS_PLANE_PROP_CRTC_H,
  META_KMS_PLANE_PROP_FB_ID,
  META_KMS_PLANE_PROP_CRTC_ID,
  META_KMS_PLANE_PROP_FB_DAMAGE_CLIPS_ID,
  META_KMS_PLANE_PROP_IN_FENCE_FD,
  META_KMS_PLANE_PROP_HOTSPOT_X,
  META_KMS_PLANE_PROP_HOTSPOT_Y,
  META_KMS_PLANE_N_PROPS
} MetaKmsPlaneProp;

typedef enum _MetaKmsPlaneRotationBit
{
  META_KMS_PLANE_ROTATION_BIT_ROTATE_0 = 0,
  META_KMS_PLANE_ROTATION_BIT_ROTATE_90,
  META_KMS_PLANE_ROTATION_BIT_ROTATE_180,
  META_KMS_PLANE_ROTATION_BIT_ROTATE_270,
  META_KMS_PLANE_ROTATION_BIT_REFLECT_X,
  META_KMS_PLANE_ROTATION_BIT_REFLECT_Y,
  META_KMS_PLANE_ROTATION_BIT_N_PROPS,
} MetaKmsPlaneRotationBit;

typedef enum _MetaKmsPlaneRotation
{
  META_KMS_PLANE_ROTATION_ROTATE_0 = (1 << 0),
  META_KMS_PLANE_ROTATION_ROTATE_90 = (1 << 1),
  META_KMS_PLANE_ROTATION_ROTATE_180 = (1 << 2),
  META_KMS_PLANE_ROTATION_ROTATE_270 = (1 << 3),
  META_KMS_PLANE_ROTATION_REFLECT_X = (1 << 4),
  META_KMS_PLANE_ROTATION_REFLECT_Y = (1 << 5),
  META_KMS_PLANE_ROTATION_UNKNOWN = (1 << 6),
} MetaKmsPlaneRotation;

MetaKmsPlane * meta_kms_plane_new (MetaKmsPlaneType         type,
                                   MetaKmsImplDevice       *impl_device,
                                   drmModePlane            *drm_plane,
                                   drmModeObjectProperties *drm_plane_props);

MetaKmsPlane * meta_kms_plane_new_fake (MetaKmsPlaneType  type,
                                        MetaKmsCrtc      *crtc);

uint32_t meta_kms_plane_get_prop_id (MetaKmsPlane     *plane,
                                     MetaKmsPlaneProp  prop);

const char * meta_kms_plane_get_prop_name (MetaKmsPlane     *plane,
                                           MetaKmsPlaneProp  prop);

uint64_t meta_kms_plane_get_prop_drm_value (MetaKmsPlane     *plane,
                                            MetaKmsPlaneProp  prop,
                                            uint64_t          value);

MetaKmsPropType meta_kms_plane_get_prop_internal_type (MetaKmsPlane     *plane,
                                                       MetaKmsPlaneProp  prop);
