/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013-2017 Red Hat
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

#include "backends/native/meta-crtc-kms.h"

#include <drm_fourcc.h>
#include <drm_mode.h>

#include "backends/meta-backend-private.h"
#include "backends/native/meta-gpu-kms.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"

/* added in libdrm 2.4.95 */
#ifndef DRM_FORMAT_INVALID
#define DRM_FORMAT_INVALID 0
#endif

#define ALL_TRANSFORMS_MASK ((1 << META_MONITOR_N_TRANSFORMS) - 1)

typedef struct _MetaCrtcKms
{
  MetaKmsCrtc *kms_crtc;

  uint32_t rotation_prop_id;
  uint32_t rotation_map[META_MONITOR_N_TRANSFORMS];

  MetaKmsPlane *primary_plane;
} MetaCrtcKms;

/**
 * meta_drm_format_to_string:
 * @tmp: temporary buffer
 * @drm_format: DRM fourcc pixel format
 *
 * Returns a pointer to a string naming the given pixel format,
 * usually a pointer to the temporary buffer but not always.
 * Invalid formats may return nonsense names.
 *
 * When calling this, allocate one MetaDrmFormatBuf on the stack to
 * be used as the temporary buffer.
 */
const char *
meta_drm_format_to_string (MetaDrmFormatBuf *tmp,
                           uint32_t          drm_format)
{
  int i;

  if (drm_format == DRM_FORMAT_INVALID)
    return "INVALID";

  G_STATIC_ASSERT (sizeof (tmp->s) == 5);
  for (i = 0; i < 4; i++)
    {
      char c = (drm_format >> (i * 8)) & 0xff;
      tmp->s[i] = g_ascii_isgraph (c) ? c : '.';
    }

  tmp->s[i] = 0;

  return tmp->s;
}

gboolean
meta_crtc_kms_is_transform_handled (MetaCrtc             *crtc,
                                    MetaMonitorTransform  transform)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  if (!crtc_kms->primary_plane)
    return FALSE;

  return meta_kms_plane_is_transform_handled (crtc_kms->primary_plane,
                                              transform);
}

void
meta_crtc_kms_apply_transform (MetaCrtc *crtc)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;
  MetaGpu *gpu = meta_crtc_get_gpu (crtc);
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);
  int kms_fd;
  MetaMonitorTransform hw_transform;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  hw_transform = crtc->transform;
  if (!meta_crtc_kms_is_transform_handled (crtc, hw_transform))
    hw_transform = META_MONITOR_TRANSFORM_NORMAL;
  if (!meta_crtc_kms_is_transform_handled (crtc, hw_transform))
    return;

  if (drmModeObjectSetProperty (kms_fd,
                                meta_kms_plane_get_id (crtc_kms->primary_plane),
                                DRM_MODE_OBJECT_PLANE,
                                crtc_kms->rotation_prop_id,
                                crtc_kms->rotation_map[hw_transform]) != 0)
    g_warning ("Failed to apply DRM plane transform %d: %m", hw_transform);
}

static int
find_property_index (MetaGpu                    *gpu,
                     drmModeObjectPropertiesPtr  props,
                     const char                 *prop_name,
                     drmModePropertyPtr         *out_prop)
{
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);
  int kms_fd;
  unsigned int i;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  for (i = 0; i < props->count_props; i++)
    {
      drmModePropertyPtr prop;

      prop = drmModeGetProperty (kms_fd, props->props[i]);
      if (!prop)
        continue;

      if (strcmp (prop->name, prop_name) == 0)
        {
          *out_prop = prop;
          return i;
        }

      drmModeFreeProperty (prop);
    }

  return -1;
}

MetaKmsCrtc *
meta_crtc_kms_get_kms_crtc (MetaCrtc *crtc)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return crtc_kms->kms_crtc;
}

/**
 * meta_crtc_kms_get_modifiers:
 * @crtc: a #MetaCrtc object that has to be a #MetaCrtcKms
 * @format: a DRM pixel format
 *
 * Returns a pointer to a #GArray containing all the supported
 * modifiers for the given DRM pixel format on the CRTC's primary
 * plane. The array element type is uint64_t.
 *
 * The caller must not modify or destroy the array or its contents.
 *
 * Returns NULL if the modifiers are not known or the format is not
 * supported.
 */
GArray *
meta_crtc_kms_get_modifiers (MetaCrtc *crtc,
                             uint32_t  format)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return meta_kms_plane_get_modifiers_for_format (crtc_kms->primary_plane,
                                                  format);
}

/**
 * meta_crtc_kms_copy_drm_format_list:
 * @crtc: a #MetaCrtc object that has to be a #MetaCrtcKms
 *
 * Returns a new #GArray that the caller must destroy. The array
 * contains all the DRM pixel formats the CRTC supports on
 * its primary plane. The array element type is uint32_t.
 */
GArray *
meta_crtc_kms_copy_drm_format_list (MetaCrtc *crtc)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return meta_kms_plane_copy_drm_format_list (crtc_kms->primary_plane);
}

/**
 * meta_crtc_kms_supports_format:
 * @crtc: a #MetaCrtc object that has to be a #MetaCrtcKms
 * @drm_format: a DRM pixel format
 *
 * Returns true if the CRTC supports the format on its primary plane.
 */
gboolean
meta_crtc_kms_supports_format (MetaCrtc *crtc,
                               uint32_t  drm_format)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  return meta_kms_plane_is_format_supported (crtc_kms->primary_plane,
                                             drm_format);
}

static void
parse_transforms (MetaCrtc          *crtc,
                  drmModePropertyPtr prop)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;
  int i;

  for (i = 0; i < prop->count_enums; i++)
    {
      int transform = -1;

      if (strcmp (prop->enums[i].name, "rotate-0") == 0)
        transform = META_MONITOR_TRANSFORM_NORMAL;
      else if (strcmp (prop->enums[i].name, "rotate-90") == 0)
        transform = META_MONITOR_TRANSFORM_90;
      else if (strcmp (prop->enums[i].name, "rotate-180") == 0)
        transform = META_MONITOR_TRANSFORM_180;
      else if (strcmp (prop->enums[i].name, "rotate-270") == 0)
        transform = META_MONITOR_TRANSFORM_270;

      if (transform != -1)
        crtc_kms->rotation_map[transform] = 1 << prop->enums[i].value;
    }
}

static void
init_crtc_rotations (MetaCrtc *crtc,
                     MetaGpu  *gpu)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);
  int kms_fd;
  uint32_t primary_plane_id;
  drmModePlane *drm_plane;
  drmModeObjectPropertiesPtr props;
  drmModePropertyPtr prop;
  int rotation_idx;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);
  primary_plane_id = meta_kms_plane_get_id (crtc_kms->primary_plane);
  drm_plane = drmModeGetPlane (kms_fd, primary_plane_id);
  props = drmModeObjectGetProperties (kms_fd,
                                      primary_plane_id,
                                      DRM_MODE_OBJECT_PLANE);

  rotation_idx = find_property_index (gpu, props,
                                      "rotation", &prop);
  if (rotation_idx >= 0)
    {
      crtc_kms->rotation_prop_id = props->props[rotation_idx];
      parse_transforms (crtc, prop);
      drmModeFreeProperty (prop);
    }

  drmModeFreeObjectProperties (props);
  drmModeFreePlane (drm_plane);
}

static void
meta_crtc_destroy_notify (MetaCrtc *crtc)
{
  g_free (crtc->driver_private);
}

MetaCrtc *
meta_create_kms_crtc (MetaGpuKms  *gpu_kms,
                      MetaKmsCrtc *kms_crtc)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaKmsDevice *kms_device;
  MetaCrtc *crtc;
  MetaCrtcKms *crtc_kms;
  MetaKmsPlane *primary_plane;
  const MetaKmsCrtcState *crtc_state;

  kms_device = meta_gpu_kms_get_kms_device (gpu_kms);
  primary_plane = meta_kms_device_get_primary_plane_for (kms_device,
                                                         kms_crtc);
  crtc_state = meta_kms_crtc_get_current_state (kms_crtc);

  crtc = g_object_new (META_TYPE_CRTC, NULL);
  crtc->gpu = gpu;
  crtc->crtc_id = meta_kms_crtc_get_id (kms_crtc);
  crtc->rect = crtc_state->rect;
  crtc->is_dirty = FALSE;
  crtc->transform = META_MONITOR_TRANSFORM_NORMAL;
  crtc->all_transforms = ALL_TRANSFORMS_MASK;

  if (crtc_state->is_drm_mode_valid)
    {
      GList *l;

      for (l = meta_gpu_get_modes (gpu); l; l = l->next)
        {
          MetaCrtcMode *mode = l->data;

          if (meta_drm_mode_equal (&crtc_state->drm_mode, mode->driver_private))
            {
              crtc->current_mode = mode;
              break;
            }
        }
    }

  crtc_kms = g_new0 (MetaCrtcKms, 1);
  crtc_kms->kms_crtc = kms_crtc;
  crtc_kms->primary_plane = primary_plane;

  crtc->driver_private = crtc_kms;
  crtc->driver_notify = (GDestroyNotify) meta_crtc_destroy_notify;

  init_crtc_rotations (crtc, gpu);

  return crtc;
}
