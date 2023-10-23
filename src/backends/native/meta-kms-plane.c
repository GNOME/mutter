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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "backends/native/meta-kms-plane-private.h"

#include <drm_fourcc.h>
#include <stdio.h>

#include "backends/meta-monitor-transform.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-impl-device.h"
#include "backends/native/meta-kms-impl-device-atomic.h"
#include "backends/native/meta-kms-device-private.h"
#include "backends/native/meta-kms-update-private.h"

typedef struct _MetaKmsPlanePropTable
{
  MetaKmsProp props[META_KMS_PLANE_N_PROPS];
  MetaKmsEnum rotation_bitmask[META_KMS_PLANE_ROTATION_BIT_N_PROPS];
} MetaKmsPlanePropTable;

struct _MetaKmsPlane
{
  GObject parent;

  MetaKmsPlaneType type;
  gboolean is_fake;

  uint32_t id;

  uint32_t possible_crtcs;

  MetaKmsPlaneRotation rotations;

  /*
   * primary plane's supported formats and maybe modifiers
   * key: GUINT_TO_POINTER (format)
   * value: owned GArray* (uint64_t modifier), or NULL
   */
  GHashTable *formats_modifiers;

  MetaKmsPlanePropTable prop_table;

  MetaKmsDevice *device;
};

G_DEFINE_TYPE (MetaKmsPlane, meta_kms_plane, G_TYPE_OBJECT)

MetaKmsDevice *
meta_kms_plane_get_device (MetaKmsPlane *plane)
{
  return plane->device;
}

uint32_t
meta_kms_plane_get_id (MetaKmsPlane *plane)
{
  g_return_val_if_fail (!plane->is_fake, 0);

  return plane->id;
}

MetaKmsPlaneType
meta_kms_plane_get_plane_type (MetaKmsPlane *plane)
{
  return plane->type;
}

uint32_t
meta_kms_plane_get_prop_id (MetaKmsPlane     *plane,
                            MetaKmsPlaneProp  prop)
{
  return plane->prop_table.props[prop].prop_id;
}

const char *
meta_kms_plane_get_prop_name (MetaKmsPlane     *plane,
                              MetaKmsPlaneProp  prop)
{
  return plane->prop_table.props[prop].name;
}

MetaKmsPropType
meta_kms_plane_get_prop_internal_type (MetaKmsPlane     *plane,
                                       MetaKmsPlaneProp  prop)
{
  return plane->prop_table.props[prop].internal_type;
}

uint64_t
meta_kms_plane_get_prop_drm_value (MetaKmsPlane     *plane,
                                   MetaKmsPlaneProp  property,
                                   uint64_t          value)
{
  MetaKmsProp *prop = &plane->prop_table.props[property];
  return meta_kms_prop_convert_value (prop, value);
}

void
meta_kms_plane_update_set_rotation (MetaKmsPlane           *plane,
                                    MetaKmsPlaneAssignment *plane_assignment,
                                    MetaMonitorTransform    transform)
{
  MetaKmsPlaneRotation kms_rotation = 0;

  g_return_if_fail (meta_kms_plane_is_transform_handled (plane, transform));

  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_0;
      break;
    case META_MONITOR_TRANSFORM_90:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_90;
      break;
    case META_MONITOR_TRANSFORM_180:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_180;
      break;
    case META_MONITOR_TRANSFORM_270:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_270;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_0 |
                     META_KMS_PLANE_ROTATION_REFLECT_X;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_90:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_90 |
                     META_KMS_PLANE_ROTATION_REFLECT_X;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_0 |
                     META_KMS_PLANE_ROTATION_REFLECT_Y;
      break;
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      kms_rotation = META_KMS_PLANE_ROTATION_ROTATE_270 |
                     META_KMS_PLANE_ROTATION_REFLECT_X;
      break;
    default:
      g_assert_not_reached ();
    }

  meta_kms_plane_assignment_set_rotation (plane_assignment, kms_rotation);
}

gboolean
meta_kms_plane_is_transform_handled (MetaKmsPlane         *plane,
                                     MetaMonitorTransform  transform)
{
  switch (transform)
    {
    case META_MONITOR_TRANSFORM_NORMAL:
      return plane->rotations & META_KMS_PLANE_ROTATION_ROTATE_0;
    case META_MONITOR_TRANSFORM_180:
      return plane->rotations & META_KMS_PLANE_ROTATION_ROTATE_180;
    case META_MONITOR_TRANSFORM_FLIPPED:
      return (plane->rotations & META_KMS_PLANE_ROTATION_ROTATE_0) &&
             (plane->rotations & META_KMS_PLANE_ROTATION_REFLECT_X);
    case META_MONITOR_TRANSFORM_FLIPPED_180:
      return (plane->rotations & META_KMS_PLANE_ROTATION_ROTATE_0) &&
             (plane->rotations & META_KMS_PLANE_ROTATION_REFLECT_Y);
    /*
     * Deny these transforms as testing shows that they don't work
     * anyway, e.g. due to the wrong buffer modifiers. They might as well be
     * less optimal due to the complexity dealing with rotation at scan-out,
     * potentially resulting in higher power consumption.
     */
    case META_MONITOR_TRANSFORM_90:
    case META_MONITOR_TRANSFORM_270:
    case META_MONITOR_TRANSFORM_FLIPPED_90:
    case META_MONITOR_TRANSFORM_FLIPPED_270:
      return FALSE;
    }

  return FALSE;
}

gboolean
meta_kms_plane_supports_cursor_hotspot (MetaKmsPlane *plane)
{
  MetaKmsImplDevice *impl_device =
    meta_kms_device_get_impl_device (plane->device);

  if (META_IS_KMS_IMPL_DEVICE_ATOMIC (impl_device))
    {
      return (meta_kms_plane_get_prop_id (plane, META_KMS_PLANE_PROP_HOTSPOT_X) &&
              meta_kms_plane_get_prop_id (plane, META_KMS_PLANE_PROP_HOTSPOT_Y));
    }
  else
    {
      return TRUE;
    }
}

GArray *
meta_kms_plane_get_modifiers_for_format (MetaKmsPlane *plane,
                                         uint32_t      format)
{
  return g_hash_table_lookup (plane->formats_modifiers,
                              GUINT_TO_POINTER (format));
}

GArray *
meta_kms_plane_copy_drm_format_list (MetaKmsPlane *plane)
{
  GArray *formats;
  GHashTableIter it;
  gpointer key;
  unsigned int n_formats_modifiers;

  n_formats_modifiers = g_hash_table_size (plane->formats_modifiers);
  formats = g_array_sized_new (FALSE, FALSE,
                               sizeof (uint32_t),
                               n_formats_modifiers);
  g_hash_table_iter_init (&it, plane->formats_modifiers);
  while (g_hash_table_iter_next (&it, &key, NULL))
    {
      uint32_t drm_format = GPOINTER_TO_UINT (key);

      g_array_append_val (formats, drm_format);
    }

  return formats;
}

gboolean
meta_kms_plane_is_format_supported (MetaKmsPlane *plane,
                                    uint32_t      drm_format)
{
  return g_hash_table_lookup_extended (plane->formats_modifiers,
                                       GUINT_TO_POINTER (drm_format),
                                       NULL, NULL);
}

gboolean
meta_kms_plane_is_usable_with (MetaKmsPlane *plane,
                               MetaKmsCrtc  *crtc)
{
  return !!(plane->possible_crtcs & (1 << meta_kms_crtc_get_idx (crtc)));
}

static inline uint32_t *
drm_formats_ptr (struct drm_format_modifier_blob *blob)
{
  return (uint32_t *) (((char *) blob) + blob->formats_offset);
}

static inline struct drm_format_modifier *
drm_modifiers_ptr (struct drm_format_modifier_blob *blob)
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

static void
update_formats (MetaKmsPlane      *plane,
                MetaKmsImplDevice *impl_device)
{
  uint64_t blob_id;
  int fd;
  drmModePropertyBlobPtr blob;
  struct drm_format_modifier_blob *blob_fmt;
  uint32_t *formats;
  struct drm_format_modifier *drm_modifiers;
  unsigned int fmt_i, mod_i;
  MetaKmsProp *in_formats;

  g_return_if_fail (g_hash_table_size (plane->formats_modifiers) == 0);

  in_formats = &plane->prop_table.props[META_KMS_PLANE_PROP_IN_FORMATS];
  blob_id = in_formats->value;
  if (blob_id == 0)
    return;

  fd = meta_kms_impl_device_get_fd (impl_device);
  blob = drmModeGetPropertyBlob (fd, blob_id);
  if (!blob)
    return;

  if (blob->length < sizeof (struct drm_format_modifier_blob))
    {
      drmModeFreePropertyBlob (blob);
      return;
    }

  blob_fmt = blob->data;

  formats = drm_formats_ptr (blob_fmt);
  drm_modifiers = drm_modifiers_ptr (blob_fmt);

  for (fmt_i = 0; fmt_i < blob_fmt->count_formats; fmt_i++)
    {
      GArray *modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));

      for (mod_i = 0; mod_i < blob_fmt->count_modifiers; mod_i++)
        {
          struct drm_format_modifier *drm_modifier = &drm_modifiers[mod_i];

          /*
           * The modifier advertisement blob is partitioned into groups of
           * 64 formats.
           */
          if (fmt_i < drm_modifier->offset || fmt_i > drm_modifier->offset + 63)
            continue;

          if (!(drm_modifier->formats & (1 << (fmt_i - drm_modifier->offset))))
            continue;

          g_array_append_val (modifiers, drm_modifier->modifier);
        }

      if (modifiers->len == 0)
        {
          free_modifier_array (modifiers);
          modifiers = NULL;
        }

      g_hash_table_insert (plane->formats_modifiers,
                           GUINT_TO_POINTER (formats[fmt_i]),
                           modifiers);
    }

  drmModeFreePropertyBlob (blob);
}

static void
set_formats_from_array (MetaKmsPlane   *plane,
                        const uint32_t *formats,
                        size_t          n_formats)
{
  size_t i;

  for (i = 0; i < n_formats; i++)
    {
      g_hash_table_insert (plane->formats_modifiers,
                           GUINT_TO_POINTER (formats[i]), NULL);
    }
}

/*
 * In case the DRM driver does not expose a format list for the
 * primary plane (does not support universal planes nor
 * IN_FORMATS property), hardcode something that is probably supported.
 */
static const uint32_t drm_default_formats[] =
  {
    /* The format everything should always support by convention */
    DRM_FORMAT_XRGB8888,
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    /* OpenGL GL_RGBA, GL_UNSIGNED_BYTE format, hopefully supported */
    DRM_FORMAT_XBGR8888
#endif
  };

static void
update_legacy_formats (MetaKmsPlane *plane,
                       drmModePlane *drm_plane)
{
  if (g_hash_table_size (plane->formats_modifiers) == 0)
    {
      set_formats_from_array (plane,
                              drm_plane->formats,
                              drm_plane->count_formats);
    }

  /* final formats fallback to something hardcoded */
  if (g_hash_table_size (plane->formats_modifiers) == 0)
    {
      set_formats_from_array (plane,
                              drm_default_formats,
                              G_N_ELEMENTS (drm_default_formats));
    }
}

static void
update_rotations (MetaKmsPlane *plane)
{
  unsigned int i;
  MetaKmsProp *rotation = &plane->prop_table.props[META_KMS_PLANE_PROP_ROTATION];

  for (i = 0; i < rotation->num_enum_values; i++)
    {
      if (rotation->enum_values[i].valid)
        plane->rotations |= rotation->enum_values[i].bitmask;
    }
}

static MetaKmsResourceChanges
meta_kms_plane_read_state (MetaKmsPlane            *plane,
                           MetaKmsImplDevice       *impl_device,
                           drmModePlane            *drm_plane,
                           drmModeObjectProperties *drm_plane_props)
{
  MetaKmsResourceChanges changes = META_KMS_RESOURCE_CHANGE_NONE;

  meta_kms_impl_device_update_prop_table (impl_device,
                                          drm_plane_props->props,
                                          drm_plane_props->prop_values,
                                          drm_plane_props->count_props,
                                          plane->prop_table.props,
                                          META_KMS_PLANE_N_PROPS);

  update_formats (plane, impl_device);
  update_rotations (plane);
  update_legacy_formats (plane, drm_plane);

  return changes;
}

static void
init_properties (MetaKmsPlane            *plane,
                 MetaKmsImplDevice       *impl_device,
                 drmModePlane            *drm_plane,
                 drmModeObjectProperties *drm_plane_props)
{
  MetaKmsPlanePropTable *prop_table = &plane->prop_table;

  *prop_table = (MetaKmsPlanePropTable) {
    .props = {
      [META_KMS_PLANE_PROP_TYPE] =
        {
          .name = "type",
          .type = DRM_MODE_PROP_ENUM,
        },
      [META_KMS_PLANE_PROP_ROTATION] =
        {
          .name = "rotation",
          .type = DRM_MODE_PROP_BITMASK,
          .enum_values = prop_table->rotation_bitmask,
          .num_enum_values = META_KMS_PLANE_ROTATION_BIT_N_PROPS,
          .default_value = META_KMS_PLANE_ROTATION_UNKNOWN,
        },
      [META_KMS_PLANE_PROP_IN_FORMATS] =
        {
          .name = "IN_FORMATS",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_PLANE_PROP_SRC_X] =
        {
          .name = "SRC_X",
          .type = DRM_MODE_PROP_RANGE,
          .internal_type = META_KMS_PROP_TYPE_FIXED_16,
        },
      [META_KMS_PLANE_PROP_SRC_Y] =
        {
          .name = "SRC_Y",
          .type = DRM_MODE_PROP_RANGE,
          .internal_type = META_KMS_PROP_TYPE_FIXED_16,
        },
      [META_KMS_PLANE_PROP_SRC_W] =
        {
          .name = "SRC_W",
          .type = DRM_MODE_PROP_RANGE,
          .internal_type = META_KMS_PROP_TYPE_FIXED_16,
        },
      [META_KMS_PLANE_PROP_SRC_H] =
        {
          .name = "SRC_H",
          .type = DRM_MODE_PROP_RANGE,
          .internal_type = META_KMS_PROP_TYPE_FIXED_16,
        },
      [META_KMS_PLANE_PROP_CRTC_X] =
        {
          .name = "CRTC_X",
          .type = DRM_MODE_PROP_SIGNED_RANGE,
        },
      [META_KMS_PLANE_PROP_CRTC_Y] =
        {
          .name = "CRTC_Y",
          .type = DRM_MODE_PROP_SIGNED_RANGE,
        },
      [META_KMS_PLANE_PROP_CRTC_W] =
        {
          .name = "CRTC_W",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_PLANE_PROP_CRTC_H] =
        {
          .name = "CRTC_H",
          .type = DRM_MODE_PROP_RANGE,
        },
      [META_KMS_PLANE_PROP_FB_ID] =
        {
          .name = "FB_ID",
          .type = DRM_MODE_PROP_OBJECT,
        },
      [META_KMS_PLANE_PROP_CRTC_ID] =
        {
          .name = "CRTC_ID",
          .type = DRM_MODE_PROP_OBJECT,
        },
      [META_KMS_PLANE_PROP_FB_DAMAGE_CLIPS_ID] =
        {
          .name = "FB_DAMAGE_CLIPS",
          .type = DRM_MODE_PROP_BLOB,
        },
      [META_KMS_PLANE_PROP_IN_FENCE_FD] =
        {
          .name = "IN_FENCE_FD",
          .type = DRM_MODE_PROP_SIGNED_RANGE,
        },
      [META_KMS_PLANE_PROP_HOTSPOT_X] =
        {
          .name = "HOTSPOT_X",
          .type = DRM_MODE_PROP_SIGNED_RANGE,
        },
      [META_KMS_PLANE_PROP_HOTSPOT_Y] =
        {
          .name = "HOTSPOT_Y",
          .type = DRM_MODE_PROP_SIGNED_RANGE,
        },
    },
    .rotation_bitmask = {
      [META_KMS_PLANE_ROTATION_BIT_ROTATE_0] =
        {
          .name = "rotate-0",
          .bitmask = META_KMS_PLANE_ROTATION_ROTATE_0,
        },
      [META_KMS_PLANE_ROTATION_BIT_ROTATE_90] =
        {
          .name = "rotate-90",
          .bitmask = META_KMS_PLANE_ROTATION_ROTATE_90,
        },
      [META_KMS_PLANE_ROTATION_BIT_ROTATE_180] =
        {
          .name = "rotate-180",
          .bitmask = META_KMS_PLANE_ROTATION_ROTATE_180,
        },
      [META_KMS_PLANE_ROTATION_BIT_ROTATE_270] =
        {
          .name = "rotate-270",
          .bitmask = META_KMS_PLANE_ROTATION_ROTATE_270,
        },
      [META_KMS_PLANE_ROTATION_BIT_REFLECT_X] =
        {
          .name = "reflect-x",
          .bitmask = META_KMS_PLANE_ROTATION_REFLECT_X,
        },
      [META_KMS_PLANE_ROTATION_BIT_REFLECT_Y] =
        {
          .name = "reflect-y",
          .bitmask = META_KMS_PLANE_ROTATION_REFLECT_Y,
        },
    },
  };
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

  init_properties (plane, impl_device, drm_plane, drm_plane_props);

  meta_kms_plane_read_state (plane, impl_device, drm_plane, drm_plane_props);

  return plane;
}

MetaKmsPlane *
meta_kms_plane_new_fake (MetaKmsPlaneType  type,
                         MetaKmsCrtc      *crtc)
{
  MetaKmsPlane *plane;

  static const uint32_t fake_plane_drm_formats[] =
    {
      DRM_FORMAT_XRGB8888,
      DRM_FORMAT_ARGB8888,
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      /* OpenGL GL_RGBA, GL_UNSIGNED_BYTE format, hopefully supported */
      DRM_FORMAT_XBGR8888,
      DRM_FORMAT_ABGR8888
#endif
    };

  plane = g_object_new (META_TYPE_KMS_PLANE, NULL);
  plane->type = type;
  plane->is_fake = TRUE;
  plane->possible_crtcs = 1 << meta_kms_crtc_get_idx (crtc);
  plane->device = meta_kms_crtc_get_device (crtc);

  set_formats_from_array (plane,
                          fake_plane_drm_formats,
                          G_N_ELEMENTS (fake_plane_drm_formats));

  return plane;
}

static void
meta_kms_plane_finalize (GObject *object)
{
  MetaKmsPlane *plane = META_KMS_PLANE (object);

  g_hash_table_destroy (plane->formats_modifiers);

  G_OBJECT_CLASS (meta_kms_plane_parent_class)->finalize (object);
}

static void
meta_kms_plane_init (MetaKmsPlane *plane)
{
  plane->formats_modifiers =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) free_modifier_array);
}

static void
meta_kms_plane_class_init (MetaKmsPlaneClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = meta_kms_plane_finalize;
}
