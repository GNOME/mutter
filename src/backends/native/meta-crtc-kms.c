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

/* added in libdrm 2.4.95 */
#ifndef DRM_FORMAT_INVALID
#define DRM_FORMAT_INVALID 0
#endif

#define ALL_TRANSFORMS (META_MONITOR_TRANSFORM_FLIPPED_270 + 1)
#define ALL_TRANSFORMS_MASK ((1 << ALL_TRANSFORMS) - 1)

typedef struct _MetaCrtcKms
{
  unsigned int index;
  uint32_t primary_plane_id;
  uint32_t rotation_prop_id;
  uint32_t rotation_map[ALL_TRANSFORMS];
  uint32_t all_hw_transforms;

  /*
   * primary plane's supported formats and maybe modifiers
   * key: GUINT_TO_POINTER (format)
   * value: owned GArray* (uint64_t modifier), or NULL
   */
  GHashTable *formats_modifiers;
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

  if ((1 << transform) & crtc_kms->all_hw_transforms)
    return TRUE;
  else
    return FALSE;
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

  if (crtc_kms->all_hw_transforms & (1 << crtc->transform))
    hw_transform = crtc->transform;
  else
    hw_transform = META_MONITOR_TRANSFORM_NORMAL;

  if (!meta_crtc_kms_is_transform_handled (crtc, META_MONITOR_TRANSFORM_NORMAL))
    return;

  if (drmModeObjectSetProperty (kms_fd,
                                crtc_kms->primary_plane_id,
                                DRM_MODE_OBJECT_PLANE,
                                crtc_kms->rotation_prop_id,
                                crtc_kms->rotation_map[hw_transform]) != 0)
    {
      g_warning ("Failed to apply DRM plane transform %d: %m", hw_transform);

      /*
       * Blacklist this HW transform, we want to fallback to our
       * fallbacks in this case.
       */
      crtc_kms->all_hw_transforms &= ~(1 << hw_transform);
    }
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

  return g_hash_table_lookup (crtc_kms->formats_modifiers,
                              GUINT_TO_POINTER (format));
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
  GArray *formats;
  GHashTableIter it;
  gpointer key;
  unsigned int n_formats_modifiers;

  n_formats_modifiers = g_hash_table_size (crtc_kms->formats_modifiers);
  formats = g_array_sized_new (FALSE,
                               FALSE,
                               sizeof (uint32_t),
                               n_formats_modifiers);
  g_hash_table_iter_init (&it, crtc_kms->formats_modifiers);
  while (g_hash_table_iter_next (&it, &key, NULL))
    {
      uint32_t drm_format = GPOINTER_TO_UINT (key);
      g_array_append_val (formats, drm_format);
    }

  return formats;
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

  return g_hash_table_lookup_extended (crtc_kms->formats_modifiers,
                                       GUINT_TO_POINTER (drm_format),
                                       NULL,
                                       NULL);
}

static inline uint32_t *
formats_ptr (struct drm_format_modifier_blob *blob)
{
  return (uint32_t *) (((char *) blob) + blob->formats_offset);
}

static inline struct drm_format_modifier *
modifiers_ptr (struct drm_format_modifier_blob *blob)
{
  return (struct drm_format_modifier *) (((char *) blob) +
                                         blob->modifiers_offset);
}

static void
free_modifier_array (GArray *array)
{
  if (!array)
    return;

  g_array_free (array, TRUE);
}

/*
 * In case the DRM driver does not expose a format list for the
 * primary plane (does not support universal planes nor
 * IN_FORMATS property), hardcode something that is probably supported.
 */
static const uint32_t drm_default_formats[] =
  {
    DRM_FORMAT_XRGB8888 /* The format everything should always support by convention */,
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    DRM_FORMAT_XBGR8888 /* OpenGL GL_RGBA, GL_UNSIGNED_BYTE format, hopefully supported */
#endif
  };

static void
set_formats_from_array (MetaCrtc       *crtc,
                        const uint32_t *formats,
                        size_t          n_formats)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;
  size_t i;

  for (i = 0; i < n_formats; i++)
    {
      g_hash_table_insert (crtc_kms->formats_modifiers,
                           GUINT_TO_POINTER (formats[i]), NULL);
    }
}

static void
parse_formats (MetaCrtc *crtc,
               int       kms_fd,
               uint32_t  blob_id)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;
  drmModePropertyBlobPtr blob;
  struct drm_format_modifier_blob *blob_fmt;
  uint32_t *formats;
  struct drm_format_modifier *modifiers;
  unsigned int fmt_i, mod_i;

  g_return_if_fail (g_hash_table_size (crtc_kms->formats_modifiers) == 0);

  if (blob_id == 0)
    return;

  blob = drmModeGetPropertyBlob (kms_fd, blob_id);
  if (!blob)
    return;

  if (blob->length < sizeof (struct drm_format_modifier_blob))
    {
      drmModeFreePropertyBlob (blob);
      return;
    }

  blob_fmt = blob->data;

  formats = formats_ptr (blob_fmt);
  modifiers = modifiers_ptr (blob_fmt);

  for (fmt_i = 0; fmt_i < blob_fmt->count_formats; fmt_i++)
    {
      GArray *mod_tmp = g_array_new (FALSE, FALSE, sizeof (uint64_t));

      for (mod_i = 0; mod_i < blob_fmt->count_modifiers; mod_i++)
        {
          struct drm_format_modifier *modifier = &modifiers[mod_i];

          /* The modifier advertisement blob is partitioned into groups of
           * 64 formats. */
          if (fmt_i < modifier->offset || fmt_i > modifier->offset + 63)
            continue;

          if (!(modifier->formats & (1 << (fmt_i - modifier->offset))))
            continue;

          g_array_append_val (mod_tmp, modifier->modifier);
        }

      if (mod_tmp->len == 0)
        {
          free_modifier_array (mod_tmp);
          mod_tmp = NULL;
        }

      g_hash_table_insert (crtc_kms->formats_modifiers,
                           GUINT_TO_POINTER (formats[fmt_i]), mod_tmp);
    }

  drmModeFreePropertyBlob (blob);
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
        {
          crtc_kms->all_hw_transforms |= 1 << transform;
          crtc_kms->rotation_map[transform] = 1 << prop->enums[i].value;
        }
    }
}

static gboolean
is_primary_plane (MetaGpu                   *gpu,
                  drmModeObjectPropertiesPtr props)
{
  drmModePropertyPtr prop;
  int idx;

  idx = find_property_index (gpu, props, "type", &prop);
  if (idx < 0)
    return FALSE;

  drmModeFreeProperty (prop);
  return props->prop_values[idx] == DRM_PLANE_TYPE_PRIMARY;
}

static void
init_crtc_rotations (MetaCrtc *crtc,
                     MetaGpu  *gpu)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;
  MetaGpuKms *gpu_kms = META_GPU_KMS (gpu);
  int kms_fd;
  drmModeObjectPropertiesPtr props;
  drmModePlaneRes *planes;
  drmModePlane *drm_plane;
  unsigned int i;

  kms_fd = meta_gpu_kms_get_fd (gpu_kms);

  planes = drmModeGetPlaneResources (kms_fd);
  if (planes == NULL)
    return;

  for (i = 0; i < planes->count_planes; i++)
    {
      drmModePropertyPtr prop;

      drm_plane = drmModeGetPlane (kms_fd, planes->planes[i]);

      if (!drm_plane)
        continue;

      if ((drm_plane->possible_crtcs & (1 << crtc_kms->index)))
        {
          props = drmModeObjectGetProperties (kms_fd,
                                              drm_plane->plane_id,
                                              DRM_MODE_OBJECT_PLANE);

          if (props && is_primary_plane (gpu, props))
            {
              int rotation_idx, fmts_idx;

              crtc_kms->primary_plane_id = drm_plane->plane_id;
              rotation_idx = find_property_index (gpu, props,
                                                  "rotation", &prop);
              if (rotation_idx >= 0)
                {
                  crtc_kms->rotation_prop_id = props->props[rotation_idx];
                  parse_transforms (crtc, prop);
                  drmModeFreeProperty (prop);
                }

              fmts_idx = find_property_index (gpu, props,
                                              "IN_FORMATS", &prop);
              if (fmts_idx >= 0)
                {
                  parse_formats (crtc, kms_fd, props->prop_values[fmts_idx]);
                  drmModeFreeProperty (prop);
                }

              /* fall back to universal plane formats without modifiers */
              if (g_hash_table_size (crtc_kms->formats_modifiers) == 0)
                {
                  set_formats_from_array (crtc,
                                          drm_plane->formats,
                                          drm_plane->count_formats);
                }
            }

          if (props)
            drmModeFreeObjectProperties (props);
        }

      drmModeFreePlane (drm_plane);
    }

  crtc->all_transforms |= crtc_kms->all_hw_transforms;

  drmModeFreePlaneResources (planes);

  /* final formats fallback to something hardcoded */
  if (g_hash_table_size (crtc_kms->formats_modifiers) == 0)
    {
      set_formats_from_array (crtc,
                              drm_default_formats,
                              G_N_ELEMENTS (drm_default_formats));
    }
}

static void
meta_crtc_destroy_notify (MetaCrtc *crtc)
{
  MetaCrtcKms *crtc_kms = crtc->driver_private;

  g_hash_table_destroy (crtc_kms->formats_modifiers);
  g_free (crtc->driver_private);
}

MetaCrtc *
meta_create_kms_crtc (MetaGpuKms   *gpu_kms,
                      drmModeCrtc  *drm_crtc,
                      unsigned int  crtc_index)
{
  MetaGpu *gpu = META_GPU (gpu_kms);
  MetaCrtc *crtc;
  MetaCrtcKms *crtc_kms;

  crtc = g_object_new (META_TYPE_CRTC, NULL);

  crtc->gpu = gpu;
  crtc->crtc_id = drm_crtc->crtc_id;
  crtc->rect.x = drm_crtc->x;
  crtc->rect.y = drm_crtc->y;
  crtc->rect.width = drm_crtc->width;
  crtc->rect.height = drm_crtc->height;
  crtc->is_dirty = FALSE;
  crtc->transform = META_MONITOR_TRANSFORM_NORMAL;
  crtc->all_transforms = ALL_TRANSFORMS_MASK;

  if (drm_crtc->mode_valid)
    {
      GList *l;

      for (l = meta_gpu_get_modes (gpu); l; l = l->next)
        {
          MetaCrtcMode *mode = l->data;

          if (meta_drm_mode_equal (&drm_crtc->mode, mode->driver_private))
            {
              crtc->current_mode = mode;
              break;
            }
        }
    }

  crtc_kms = g_new0 (MetaCrtcKms, 1);
  crtc_kms->index = crtc_index;

  crtc_kms->formats_modifiers =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) free_modifier_array);

  crtc->driver_private = crtc_kms;
  crtc->driver_notify = (GDestroyNotify) meta_crtc_destroy_notify;

  init_crtc_rotations (crtc, gpu);

  return crtc;
}
