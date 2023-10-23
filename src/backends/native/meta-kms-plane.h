/*
 * Copyright (C) 2018 Red Hat
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
#include "backends/meta-monitor-transform.h"

enum _MetaKmsPlaneType
{
  META_KMS_PLANE_TYPE_PRIMARY,
  META_KMS_PLANE_TYPE_CURSOR,
  META_KMS_PLANE_TYPE_OVERLAY,
};

#define META_TYPE_KMS_PLANE meta_kms_plane_get_type ()
G_DECLARE_FINAL_TYPE (MetaKmsPlane, meta_kms_plane,
                      META, KMS_PLANE, GObject)

META_EXPORT_TEST
MetaKmsDevice * meta_kms_plane_get_device (MetaKmsPlane *plane);

META_EXPORT_TEST
uint32_t meta_kms_plane_get_id (MetaKmsPlane *plane);

META_EXPORT_TEST
MetaKmsPlaneType meta_kms_plane_get_plane_type (MetaKmsPlane *plane);

gboolean meta_kms_plane_is_transform_handled (MetaKmsPlane         *plane,
                                              MetaMonitorTransform  transform);

gboolean meta_kms_plane_supports_cursor_hotspot (MetaKmsPlane *plane);

GArray * meta_kms_plane_get_modifiers_for_format (MetaKmsPlane *plane,
                                                  uint32_t      format);

GArray * meta_kms_plane_copy_drm_format_list (MetaKmsPlane *plane);

gboolean meta_kms_plane_is_format_supported (MetaKmsPlane *plane,
                                             uint32_t      format);

META_EXPORT_TEST
gboolean meta_kms_plane_is_usable_with (MetaKmsPlane *plane,
                                        MetaKmsCrtc  *crtc);

void meta_kms_plane_update_set_rotation (MetaKmsPlane           *plane,
                                         MetaKmsPlaneAssignment *plane_assignment,
                                         MetaMonitorTransform    transform);
