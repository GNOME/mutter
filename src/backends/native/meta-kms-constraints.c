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
  MetaKmsConstraintsProperty *properties;
  size_t n_properties;
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

MetaKmsConstraintsDescription *
meta_kms_constraints_description_new (
  const MetaKmsConstraintsSize     *output,
  const MetaKmsConstraintsFormat   *formats,
  size_t                            n_formats,
  const MetaKmsConstraintsProperty *properties,
  size_t                            n_properties,
  GError                          **error)
{
  g_autofree MetaKmsConstraintsFormat *formats_copy = NULL;
  g_autofree MetaKmsConstraintsProperty *properties_copy = NULL;
  MetaKmsConstraintsDescription *description;
  size_t i;
  size_t j;

  if (!output ||
      !formats ||
      n_formats == 0 ||
      !size_is_valid (output) ||
      (n_properties != 0 && !properties))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Invalid KMS constraints description");
      return NULL;
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

  description = g_try_new0 (MetaKmsConstraintsDescription, 1);
  if (!description)
    {
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
  description->properties = g_steal_pointer (&properties_copy);
  description->n_properties = n_properties;

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
  g_free (description->properties);
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
meta_kms_constraints_description_allows_explicit_layout (
  const MetaKmsConstraintsDescription *description,
  uint32_t                             plane_id,
  uint32_t                             format,
  uint64_t                             modifier,
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
          meta_kms_constraints_size_contains (&candidate->size,
                                               width,
                                               height))
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
