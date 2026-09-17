/*
 * Copyright (C) 2026 Red Hat
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

#include "backends/native/meta-drm-constraints.h"
#include "backends/native/meta-kms-constraints.h"

#include <drm_fourcc.h>
#include <gio/gio.h>
#include <xf86drmMode.h>

struct _MetaKmsConstraintsDescription
{
  gatomicrefcount ref_count;
  MetaKmsConstraintsSize output;
  MetaKmsConstraintsFormat *formats;
  size_t n_formats;
  MetaKmsConstraintsPlaneGeometry *plane_geometries;
  size_t n_plane_geometries;
  MetaKmsConstraintsProperty *properties;
  size_t n_properties;
  MetaKmsConstraintsPlaneLimit *plane_limits;
  size_t n_plane_limits;
};

static gboolean
size_is_valid (const MetaKmsConstraintsSize *size)
{
  return size->min_width != 0 &&
         size->min_height != 0 &&
         size->min_width <= size->max_width &&
         size->min_height <= size->max_height;
}

static gboolean
format_is_valid (const MetaKmsConstraintsFormat *format)
{
  return format->plane_id != 0 &&
         format->format != 0 &&
         ((format->implicit && format->modifier == 0) ||
          (!format->implicit && format->modifier != DRM_FORMAT_MOD_INVALID)) &&
         (format->permits_native || format->permits_imported) &&
         format->plane_count >= 1 &&
         format->plane_count <= 4 &&
         format->pitch_alignment != 0 &&
         (format->pitch_alignment & (format->pitch_alignment - 1)) == 0 &&
         format->offset_alignment != 0 &&
         (format->offset_alignment & (format->offset_alignment - 1)) == 0 &&
         format->max_pitch >= format->pitch_alignment &&
         size_is_valid (&format->size);
}

static gboolean
plane_geometry_is_valid (const MetaKmsConstraintsPlaneGeometry *geometry)
{
  return geometry->plane_id != 0 &&
         geometry->min_scale != 0 &&
         geometry->min_scale <= geometry->max_scale;
}

static gboolean
property_is_valid (const MetaKmsConstraintsProperty *property)
{
  if (property->object_id == 0 || property->property_id == 0)
    return FALSE;

  switch (property->type)
    {
    case DRM_MODE_PROP_RANGE:
      return property->mask == 0 &&
             property->minimum <= property->maximum;
    case DRM_MODE_PROP_SIGNED_RANGE:
      return property->mask == 0 &&
             (int64_t) property->minimum <= (int64_t) property->maximum;
    case DRM_MODE_PROP_ENUM:
      return property->minimum == 0 &&
             property->maximum == 0 &&
             property->mask != 0;
    case DRM_MODE_PROP_BITMASK:
      return property->minimum == 0 && property->maximum == 0;
    default:
      return FALSE;
    }
}

static gboolean
format_equals (const MetaKmsConstraintsFormat *a,
               const MetaKmsConstraintsFormat *b)
{
  return a->plane_id == b->plane_id &&
         a->format == b->format &&
         a->modifier == b->modifier &&
         !!a->implicit == !!b->implicit;
}

static gboolean
property_equals (const MetaKmsConstraintsProperty *a,
                 const MetaKmsConstraintsProperty *b)
{
  return a->object_id == b->object_id &&
         a->property_id == b->property_id;
}

static gboolean
plane_limit_is_valid (const MetaKmsConstraintsPlaneLimit *limit)
{
  size_t i;
  size_t j;

  if (limit->max_active == 0 ||
      limit->n_plane_ids == 0 ||
      limit->max_active > limit->n_plane_ids ||
      limit->n_plane_ids > DRM_MODE_CONSTRAINTS_MAX_PLANES_PER_LIMIT ||
      !limit->plane_ids)
    return FALSE;

  for (i = 0; i < limit->n_plane_ids; i++)
    {
      if (limit->plane_ids[i] == 0)
        return FALSE;

      for (j = 0; j < i; j++)
        {
          if (limit->plane_ids[i] == limit->plane_ids[j])
            return FALSE;
        }
    }

  return TRUE;
}

static void
free_plane_limits (MetaKmsConstraintsPlaneLimit *plane_limits,
                   size_t                        n_plane_limits)
{
  size_t i;

  for (i = 0; i < n_plane_limits; i++)
    g_free ((gpointer) plane_limits[i].plane_ids);
  g_free (plane_limits);
}

MetaKmsConstraintsDescription *
meta_kms_constraints_description_new (
  const MetaKmsConstraintsSize     *output,
  const MetaKmsConstraintsFormat   *formats,
  size_t                            n_formats,
  const MetaKmsConstraintsPlaneGeometry *plane_geometries,
  size_t                            n_plane_geometries,
  const MetaKmsConstraintsProperty *properties,
  size_t                            n_properties,
  const MetaKmsConstraintsPlaneLimit *plane_limits,
  size_t                            n_plane_limits,
  GError                          **error)
{
  g_autofree MetaKmsConstraintsFormat *formats_copy = NULL;
  g_autofree MetaKmsConstraintsPlaneGeometry *plane_geometries_copy = NULL;
  g_autofree MetaKmsConstraintsProperty *properties_copy = NULL;
  MetaKmsConstraintsPlaneLimit *plane_limits_copy = NULL;
  MetaKmsConstraintsDescription *description;
  size_t i;
  size_t j;

  if (!output ||
      !formats ||
      n_formats == 0 ||
      !size_is_valid (output) ||
      (n_plane_geometries != 0 && !plane_geometries) ||
      n_plane_geometries > DRM_MODE_CONSTRAINTS_MAX_PLANE_GEOMETRIES ||
      (n_properties != 0 && !properties) ||
      (n_plane_limits != 0 && !plane_limits) ||
      n_plane_limits > DRM_MODE_CONSTRAINTS_MAX_PLANE_LIMITS)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Invalid KMS constraints description");
      return NULL;
    }

  for (i = 0; i < n_plane_geometries; i++)
    {
      if (!plane_geometry_is_valid (&plane_geometries[i]))
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "Invalid KMS constraints plane geometry");
          return NULL;
        }

      for (j = 0; j < i; j++)
        {
          if (plane_geometries[i].plane_id == plane_geometries[j].plane_id)
            {
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_INVALID_DATA,
                                   "Duplicate KMS constraints plane geometry");
              return NULL;
            }
        }
    }

  for (i = 0; i < n_plane_limits; i++)
    {
      if (!plane_limit_is_valid (&plane_limits[i]))
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "Invalid KMS constraints plane limit");
          return NULL;
        }
    }

  for (i = 0; i < n_formats; i++)
    {
      if (!format_is_valid (&formats[i]))
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "Invalid KMS constraints format");
          return NULL;
        }

      for (j = 0; j < i; j++)
        {
          if (format_equals (&formats[i], &formats[j]))
            {
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_INVALID_DATA,
                                   "Duplicate KMS constraints format");
              return NULL;
            }
        }
    }

  for (i = 0; i < n_properties; i++)
    {
      if (!property_is_valid (&properties[i]))
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_DATA,
                               "Invalid KMS constraints property");
          return NULL;
        }

      for (j = 0; j < i; j++)
        {
          if (property_equals (&properties[i], &properties[j]))
            {
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_INVALID_DATA,
                                   "Duplicate KMS constraints property");
              return NULL;
            }
        }
    }

  formats_copy = g_try_new (MetaKmsConstraintsFormat, n_formats);
  if (!formats_copy)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Allocate KMS constraints formats");
      return NULL;
    }
  memcpy (formats_copy, formats, sizeof (*formats) * n_formats);

  if (n_plane_geometries != 0)
    {
      plane_geometries_copy = g_try_new (MetaKmsConstraintsPlaneGeometry,
                                         n_plane_geometries);
      if (!plane_geometries_copy)
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_NO_SPACE,
                               "Allocate KMS constraints plane geometries");
          return NULL;
        }
      memcpy (plane_geometries_copy,
              plane_geometries,
              sizeof (*plane_geometries) * n_plane_geometries);
    }

  if (n_properties != 0)
    {
      properties_copy = g_try_new (MetaKmsConstraintsProperty, n_properties);
      if (!properties_copy)
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_NO_SPACE,
                               "Allocate KMS constraints properties");
          return NULL;
        }
      memcpy (properties_copy,
              properties,
              sizeof (*properties) * n_properties);
    }

  if (n_plane_limits != 0)
    {
      plane_limits_copy = g_try_new0 (MetaKmsConstraintsPlaneLimit,
                                      n_plane_limits);
      if (!plane_limits_copy)
        {
          g_set_error_literal (error,
                               G_IO_ERROR,
                               G_IO_ERROR_NO_SPACE,
                               "Allocate KMS constraints plane limits");
          return NULL;
        }

      for (i = 0; i < n_plane_limits; i++)
        {
          uint32_t *plane_ids;

          plane_limits_copy[i].max_active = plane_limits[i].max_active;
          plane_limits_copy[i].n_plane_ids = plane_limits[i].n_plane_ids;
          plane_ids = g_try_malloc_n (plane_limits[i].n_plane_ids,
                                      sizeof (*plane_limits[i].plane_ids));

          plane_limits_copy[i].plane_ids = plane_ids;
          if (!plane_ids)
            {
              free_plane_limits (plane_limits_copy, n_plane_limits);
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_NO_SPACE,
                                   "Allocate KMS constraints plane IDs");
              return NULL;
            }
          memcpy (plane_ids,
                  plane_limits[i].plane_ids,
                  sizeof (*plane_limits[i].plane_ids) *
                  plane_limits[i].n_plane_ids);
        }
    }

  description = g_try_new0 (MetaKmsConstraintsDescription, 1);
  if (!description)
    {
      free_plane_limits (plane_limits_copy, n_plane_limits);
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Allocate KMS constraints description");
      return NULL;
    }
  g_atomic_ref_count_init (&description->ref_count);
  description->output = *output;
  description->formats = g_steal_pointer (&formats_copy);
  description->n_formats = n_formats;
  description->plane_geometries = g_steal_pointer (&plane_geometries_copy);
  description->n_plane_geometries = n_plane_geometries;
  description->properties = g_steal_pointer (&properties_copy);
  description->n_properties = n_properties;
  description->plane_limits = plane_limits_copy;
  description->n_plane_limits = n_plane_limits;

  return description;
}

MetaKmsConstraintsDescription *
meta_kms_constraints_description_ref (
  MetaKmsConstraintsDescription *description)
{
  g_atomic_ref_count_inc (&description->ref_count);
  return description;
}

void
meta_kms_constraints_description_unref (
  MetaKmsConstraintsDescription *description)
{
  if (!description)
    return;
  if (!g_atomic_ref_count_dec (&description->ref_count))
    return;

  g_free (description->formats);
  g_free (description->plane_geometries);
  g_free (description->properties);
  free_plane_limits (description->plane_limits,
                     description->n_plane_limits);
  g_free (description);
}

const MetaKmsConstraintsSize *
meta_kms_constraints_description_get_output (
  const MetaKmsConstraintsDescription *description)
{
  return &description->output;
}

const MetaKmsConstraintsFormat *
meta_kms_constraints_description_get_formats (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_formats)
{
  *n_formats = description->n_formats;
  return description->formats;
}

const MetaKmsConstraintsProperty *
meta_kms_constraints_description_get_properties (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_properties)
{
  *n_properties = description->n_properties;
  return description->properties;
}

const MetaKmsConstraintsPlaneGeometry *
meta_kms_constraints_description_get_plane_geometries (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_plane_geometries)
{
  *n_plane_geometries = description->n_plane_geometries;
  return description->plane_geometries;
}

const MetaKmsConstraintsPlaneLimit *
meta_kms_constraints_description_get_plane_limits (
  const MetaKmsConstraintsDescription *description,
  size_t                              *n_plane_limits)
{
  *n_plane_limits = description->n_plane_limits;
  return description->plane_limits;
}

gboolean
meta_kms_constraints_size_contains (const MetaKmsConstraintsSize *size,
                                    uint32_t                      width,
                                    uint32_t                      height)
{
  return width >= size->min_width &&
         width <= size->max_width &&
         height >= size->min_height &&
         height <= size->max_height;
}

gboolean
meta_kms_constraints_description_allows_plane_geometry (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             framebuffer_width,
  uint32_t                             framebuffer_height,
  MetaFixed16Rectangle                 source,
  MtkRectangle                         destination)
{
  const MetaKmsConstraintsPlaneGeometry *geometry = NULL;
  size_t i;

  for (i = 0; i < description->n_plane_geometries; i++)
    {
      if (description->plane_geometries[i].plane_id == plane_id)
        {
          geometry = &description->plane_geometries[i];
          break;
        }
    }

  if (!geometry)
    return TRUE;
  if (source.x < 0 || source.y < 0 ||
      source.width <= 0 || source.height <= 0 ||
      destination.width <= 0 || destination.height <= 0)
    return FALSE;
  if ((uint64_t) source.x + source.width >
        (uint64_t) framebuffer_width << 16 ||
      (uint64_t) source.y + source.height >
        (uint64_t) framebuffer_height << 16)
    return FALSE;
  if (!geometry->permits_crop &&
      (source.x != 0 || source.y != 0 ||
       (int64_t) source.width != (int64_t) framebuffer_width << 16 ||
       (int64_t) source.height != (int64_t) framebuffer_height << 16))
    return FALSE;
  if (!geometry->permits_fractional_source &&
      (((uint32_t) source.x | (uint32_t) source.y |
        (uint32_t) source.width | (uint32_t) source.height) & 0xffff) != 0)
    return FALSE;
  if (!geometry->permits_position &&
      (destination.x != 0 || destination.y != 0))
    return FALSE;

  return (uint64_t) source.width >=
           (uint64_t) destination.width * geometry->min_scale &&
         (uint64_t) source.width <=
           (uint64_t) destination.width * geometry->max_scale &&
         (uint64_t) source.height >=
           (uint64_t) destination.height * geometry->min_scale &&
         (uint64_t) source.height <=
           (uint64_t) destination.height * geometry->max_scale;
}

static gboolean
format_permits_storage (const MetaKmsConstraintsFormat *format,
                        MetaKmsConstraintsStorage       storage)
{
  switch (storage)
    {
    case META_KMS_CONSTRAINTS_STORAGE_NATIVE:
      return format->permits_native;
    case META_KMS_CONSTRAINTS_STORAGE_IMPORTED:
      return format->permits_imported;
    }

  g_assert_not_reached ();
}

gboolean
meta_kms_constraints_description_allows_explicit_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  uint64_t                             modifier,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height)
{
  size_t i;

  for (i = 0; i < description->n_formats; i++)
    {
      const MetaKmsConstraintsFormat *candidate = &description->formats[i];

      if (candidate->plane_id == plane_id &&
          candidate->format == format &&
          !candidate->implicit &&
          candidate->modifier == modifier &&
          format_permits_storage (candidate, storage) &&
          meta_kms_constraints_size_contains (&candidate->size,
                                               width,
                                               height))
        return TRUE;
    }

  return FALSE;
}

gboolean
meta_kms_constraints_description_allows_implicit_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height)
{
  size_t i;

  for (i = 0; i < description->n_formats; i++)
    {
      const MetaKmsConstraintsFormat *candidate = &description->formats[i];

      if (candidate->plane_id == plane_id &&
          candidate->format == format &&
          candidate->implicit &&
          format_permits_storage (candidate, storage) &&
          meta_kms_constraints_size_contains (&candidate->size,
                                               width,
                                               height))
        return TRUE;
    }

  return FALSE;
}

gboolean
meta_kms_constraints_description_allows_buffer_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  uint64_t                             modifier,
  gboolean                             implicit,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height,
  size_t                               n_planes,
  const uint32_t                      *pitches,
  const uint32_t                      *offsets)
{
  size_t i;
  size_t j;

  if (!pitches || !offsets)
    return FALSE;

  for (i = 0; i < description->n_formats; i++)
    {
      const MetaKmsConstraintsFormat *candidate = &description->formats[i];

      if (candidate->plane_id != plane_id ||
          candidate->format != format ||
          !!candidate->implicit != !!implicit ||
          (!implicit && candidate->modifier != modifier) ||
          !format_permits_storage (candidate, storage) ||
          !meta_kms_constraints_size_contains (&candidate->size,
                                                width,
                                                height) ||
          candidate->plane_count != n_planes)
        continue;

      for (j = 0; j < n_planes; j++)
        {
          if (pitches[j] % candidate->pitch_alignment != 0 ||
              pitches[j] > candidate->max_pitch ||
              offsets[j] % candidate->offset_alignment != 0)
            break;
        }

      if (j == n_planes)
        return TRUE;
    }

  return FALSE;
}

static gboolean
contains_uint32 (GArray   *values,
                 uint32_t  value)
{
  size_t i;

  for (i = 0; i < values->len; i++)
    {
      if (g_array_index (values, uint32_t, i) == value)
        return TRUE;
    }

  return FALSE;
}

GArray *
meta_kms_constraints_description_copy_drm_formats_for_plane (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height)
{
  GArray *formats;
  size_t i;

  formats = g_array_new (FALSE, FALSE, sizeof (uint32_t));
  for (i = 0; i < description->n_formats; i++)
    {
      const MetaKmsConstraintsFormat *candidate = &description->formats[i];

      if (candidate->plane_id != plane_id ||
          !format_permits_storage (candidate, storage) ||
          !meta_kms_constraints_size_contains (&candidate->size,
                                                width,
                                                height) ||
          contains_uint32 (formats, candidate->format))
        continue;

      g_array_append_val (formats, candidate->format);
    }

  return formats;
}

GArray *
meta_kms_constraints_description_copy_explicit_modifiers_for_format (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  MetaKmsConstraintsStorage            storage,
  uint32_t                             width,
  uint32_t                             height)
{
  GArray *modifiers;
  size_t i;

  modifiers = g_array_new (FALSE, FALSE, sizeof (uint64_t));
  for (i = 0; i < description->n_formats; i++)
    {
      const MetaKmsConstraintsFormat *candidate = &description->formats[i];

      if (candidate->plane_id != plane_id ||
          candidate->format != format ||
          candidate->implicit ||
          !format_permits_storage (candidate, storage) ||
          !meta_kms_constraints_size_contains (&candidate->size, width, height))
        continue;

      g_array_append_val (modifiers, candidate->modifier);
    }

  return modifiers;
}

const MetaKmsConstraintsProperty *
meta_kms_constraints_description_find_property (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             object_id,
  uint32_t                             property_id)
{
  size_t i;

  for (i = 0; i < description->n_properties; i++)
    {
      const MetaKmsConstraintsProperty *property = &description->properties[i];

      if (property->object_id == object_id &&
          property->property_id == property_id)
        return property;
    }

  return NULL;
}

gboolean
meta_kms_constraints_property_matches (
  const MetaKmsConstraintsProperty *property,
  uint64_t                          value)
{
  switch (property->type)
    {
    case DRM_MODE_PROP_RANGE:
      return value >= property->minimum && value <= property->maximum;
    case DRM_MODE_PROP_SIGNED_RANGE:
      return (int64_t) value >= (int64_t) property->minimum &&
             (int64_t) value <= (int64_t) property->maximum;
    case DRM_MODE_PROP_ENUM:
      return value < 64 && (property->mask & (UINT64_C (1) << value));
    case DRM_MODE_PROP_BITMASK:
      return (value & ~property->mask) == 0;
    default:
      return FALSE;
    }
}
