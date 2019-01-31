/*
 * Copyright (C) 2013-2019 Red Hat
 * Copyright (C) 2018 DisplayLink (UK) Ltd.
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

#include "backends/native/meta-kms-plane.h"

#include <stdio.h>

#include "backends/meta-monitor-transform.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl-device.h"

struct _MetaKmsPlane
{
  GObject parent;

  MetaKmsPlaneType type;
  uint32_t id;

  uint32_t possible_crtcs;

  uint32_t rotation_prop_id;
  uint32_t rotation_map[META_MONITOR_N_TRANSFORMS];
  uint32_t all_hw_transforms;

  MetaKmsDevice *device;
};

G_DEFINE_TYPE (MetaKmsPlane, meta_kms_plane, G_TYPE_OBJECT)

MetaKmsPlaneType
meta_kms_plane_get_plane_type (MetaKmsPlane *plane)
{
  return plane->type;
}

gboolean
meta_kms_plane_is_transform_handled (MetaKmsPlane         *plane,
                                     MetaMonitorTransform  transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
    case META_MONITOR_TRANSFORM_180:
    case META_MONITOR_TRANSFORM_FLIPPED:
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      break;
    case META_MONITOR_TRANSFORM_90:
    case META_MONITOR_TRANSFORM_270:
    case META_MONITOR_TRANSFORM_FLIPPED_90:
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      /*
       * Blacklist these transforms as testing shows that they don't work
       * anyway, e.g. due to the wrong buffer modifiers. They might as well be
       * less optimal due to the complexity dealing with rotation at scan-out,
       * potentially resulting in higher power consumption.
       */
      return FALSE;
    }

  return plane->all_hw_transforms & (1 << transform);
}

gboolean
meta_kms_plane_is_usable_with (MetaKmsPlane *plane,
                               MetaKmsCrtc  *crtc)
{
  return !!(plane->possible_crtcs & (1 << meta_kms_crtc_get_idx (crtc)));
}

static void
parse_rotations (MetaKmsPlane       *plane,
                 MetaKmsImplDevice  *impl_device,
                 drmModePropertyPtr  prop)
{
  int i;

  for (i = 0; i < prop->count_enums; i++)
    {
      MetaMonitorTransform transform = -1;

      if (strcmp (prop->enums[i].name, "rotate-0") == 0)
        transform = META_MONITOR_TRANSFORM_NORMAL;
      else if (strcmp (prop->enums[i].name, "rotate-90") == 0)
        transform = META_MONITOR_TRANSFORM_90;
      else if (strcmp (prop->enums[i].name, "rotate-180") == 0)
        transform = META_MONITOR_TRANSFORM_180;
      else if (strcmp (prop->enums[i].name, "rotate-270") == 0)
        transform = META_MONITOR_TRANSFORM_270;

      if (transform != -1)
        {
          plane->all_hw_transforms |= 1 << transform;
          plane->rotation_map[transform] = 1 << prop->enums[i].value;
        }
    }
}

static void
init_rotations (MetaKmsPlane            *plane,
                MetaKmsImplDevice       *impl_device,
                drmModeObjectProperties *drm_plane_props)
{
  drmModePropertyPtr prop;
  int idx;

  prop = meta_kms_impl_device_find_property (impl_device, drm_plane_props,
                                             "rotation", &idx);
  if (prop)
    {
      plane->rotation_prop_id = drm_plane_props->props[idx];
      parse_rotations (plane, impl_device, prop);
      drmModeFreeProperty (prop);
    }
}

MetaKmsPlane *
meta_kms_plane_new (MetaKmsPlaneType         type,
                    MetaKmsImplDevice       *impl_device,
                    drmModePlane            *drm_plane,
                    drmModeObjectProperties *drm_plane_props)
{
  MetaKmsPlane *plane;

  plane = g_object_new (META_TYPE_KMS_PLANE, NULL);
  plane->type = type;
  plane->id = drm_plane->plane_id;
  plane->possible_crtcs = drm_plane->possible_crtcs;
  plane->device = meta_kms_impl_device_get_device (impl_device);

  init_rotations (plane, impl_device, drm_plane_props);

  return plane;
}

static void
meta_kms_plane_init (MetaKmsPlane *plane)
{
}

static void
meta_kms_plane_class_init (MetaKmsPlaneClass *klass)
{
}
