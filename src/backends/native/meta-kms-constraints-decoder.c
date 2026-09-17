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
#include "backends/native/meta-kms-constraints-decoder.h"

#include <drm_mode.h>
#include <gio/gio.h>
#include <stdint.h>
#include <string.h>

G_STATIC_ASSERT (sizeof (struct drm_mode_constraints_list) == 64);
G_STATIC_ASSERT (sizeof (struct drm_mode_constraints) == 40);
G_STATIC_ASSERT (sizeof (struct drm_mode_constraints_description) == 16);
G_STATIC_ASSERT (sizeof (struct drm_mode_constraints_record) == 16);
G_STATIC_ASSERT (sizeof (struct drm_mode_constraints_output_size) == 32);
G_STATIC_ASSERT (sizeof (struct drm_mode_constraints_plane_format) == 72);
G_STATIC_ASSERT (sizeof (struct drm_mode_constraints_property) == 56);
G_STATIC_ASSERT (sizeof (struct drm_mode_constraints_plane_limit) == 24);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_constraints_list,
                                  generation) == 8);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_constraints_list,
                                  reserved) == 48);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_constraints,
                                  reserved) == 24);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_constraints_plane_format,
                                  modifier) == 24);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_constraints_plane_format,
                                  layout_flags) == 48);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_constraints_plane_format,
                                  max_pitch) == 68);
G_STATIC_ASSERT (G_STRUCT_OFFSET (struct drm_mode_constraints_property,
                                  minimum) == 32);

typedef enum _DecodeResult
{
  DECODE_RESULT_OK,
  DECODE_RESULT_UNSUPPORTED,
  DECODE_RESULT_INVALID,
} DecodeResult;

static gboolean
range_is_valid (size_t size,
                size_t offset,
                size_t length)
{
  return offset <= size && length <= size - offset;
}

static gboolean
array_is_valid (size_t size,
                size_t offset,
                size_t count,
                size_t stride,
                size_t minimum_stride)
{
  if (stride < minimum_stride || count > G_MAXSIZE / stride)
    return FALSE;

  return range_is_valid (size, offset, count * stride);
}

static gboolean
ranges_overlap (size_t first_offset,
                size_t first_length,
                size_t second_offset,
                size_t second_length)
{
  return first_offset < second_offset + second_length &&
         second_offset < first_offset + first_length;
}

static gboolean
bytes_are_zero (const uint8_t *bytes,
                size_t         size)
{
  size_t i;

  for (i = 0; i < size; i++)
    {
      if (bytes[i] != 0)
        return FALSE;
    }

  return TRUE;
}

static void
set_invalid_error (GError     **error,
                   const char  *message)
{
  if (error && *error)
    return;

  g_set_error_literal (error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_DATA,
                       message);
}

static DecodeResult
decode_description (const uint8_t                *data,
                    size_t                        size,
                    const struct drm_mode_constraints *entry,
                    size_t                        entries_offset,
                    size_t                        entries_length,
                    MetaKmsConstraintsDescription **out_description,
                    GError                      **error)
{
  struct drm_mode_constraints_description description;
  MetaKmsConstraintsSize output = {0};
  g_autofree MetaKmsConstraintsFormat *formats = NULL;
  g_autofree MetaKmsConstraintsProperty *properties = NULL;
  g_autofree MetaKmsConstraintsPlaneLimit *plane_limits = NULL;
  g_autoptr (GPtrArray) plane_id_arrays = NULL;
  size_t description_end;
  size_t offset;
  size_t n_formats = 0;
  size_t n_properties = 0;
  size_t n_plane_limits = 0;
  size_t format_capacity;
  size_t property_capacity;
  size_t plane_limit_capacity;
  gboolean has_output = FALSE;
  uint32_t i;

  if (entry->description_offset % 8 != 0 ||
      entry->description_length % 8 != 0 ||
      !range_is_valid (size,
                       entry->description_offset,
                       entry->description_length) ||
      entry->description_length < sizeof (description) ||
      ranges_overlap (entry->description_offset,
                      entry->description_length,
                      0,
                      sizeof (struct drm_mode_constraints_list)) ||
      ranges_overlap (entry->description_offset,
                      entry->description_length,
                      entries_offset,
                      entries_length))
    goto invalid;

  memcpy (&description,
          data + entry->description_offset,
          sizeof (description));
  if (description.version != DRM_MODE_CONSTRAINTS_VERSION)
    return DECODE_RESULT_UNSUPPORTED;
  if (description.length != entry->description_length ||
      description.record_count > entry->description_length /
                                 sizeof (struct drm_mode_constraints_record))
    goto invalid;

  description_end = (size_t) entry->description_offset +
                    entry->description_length;
  if (description.records_offset % 8 != 0 ||
      description.records_offset < entry->description_offset +
                                   sizeof (description) ||
      description.records_offset > description_end)
    goto invalid;

  format_capacity = MIN (description.record_count,
                         DRM_MODE_CONSTRAINTS_MAX_FORMATS);
  property_capacity = MIN (description.record_count,
                           DRM_MODE_CONSTRAINTS_MAX_PROPERTIES);
  plane_limit_capacity = MIN (description.record_count,
                              DRM_MODE_CONSTRAINTS_MAX_PLANE_LIMITS);
  formats = g_try_new0 (MetaKmsConstraintsFormat, format_capacity);
  properties = g_try_new0 (MetaKmsConstraintsProperty, property_capacity);
  plane_limits = g_try_new0 (MetaKmsConstraintsPlaneLimit,
                             plane_limit_capacity);
  if (description.record_count != 0 &&
      (!formats || !properties || !plane_limits))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Allocate decoded KMS constraints records");
      return DECODE_RESULT_INVALID;
    }
  plane_id_arrays = g_ptr_array_new_with_free_func (g_free);

  offset = description.records_offset;
  for (i = 0; i < description.record_count; i++)
    {
      struct drm_mode_constraints_record record;

      if (!range_is_valid (description_end,
                           offset,
                           sizeof (record)))
        goto invalid;
      memcpy (&record, data + offset, sizeof (record));
      if (record.pad != 0)
        goto invalid;
      if (record.length < sizeof (record) ||
          record.length % 8 != 0 ||
          !range_is_valid (description_end, offset, record.length) ||
          (record.flags & ~DRM_MODE_CONSTRAINTS_RECORD_REQUIRED) != 0)
        return DECODE_RESULT_UNSUPPORTED;

      switch (record.type)
        {
        case DRM_MODE_CONSTRAINTS_RECORD_OUTPUT_SIZE:
          {
            struct drm_mode_constraints_output_size wire;

            if (has_output)
              goto invalid;
            if (record.length != sizeof (wire))
              return DECODE_RESULT_UNSUPPORTED;
            memcpy (&wire, data + offset, sizeof (wire));
            output = (MetaKmsConstraintsSize) {
              .min_width = wire.min_width,
              .min_height = wire.min_height,
              .max_width = wire.max_width,
              .max_height = wire.max_height,
            };
            has_output = TRUE;
            break;
          }
        case DRM_MODE_CONSTRAINTS_RECORD_PLANE_FORMAT:
          {
            struct drm_mode_constraints_plane_format wire;

            if (record.length != sizeof (wire))
              return DECODE_RESULT_UNSUPPORTED;
            if (n_formats == format_capacity)
              goto invalid;
            memcpy (&wire, data + offset, sizeof (wire));
            if ((wire.layout_flags &
                 ~DRM_MODE_CONSTRAINTS_LAYOUT_IMPLICIT) != 0)
              return DECODE_RESULT_UNSUPPORTED;
            if ((wire.storage_flags &
                 ~(DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_NATIVE |
                   DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_IMPORTED)) != 0)
              return DECODE_RESULT_UNSUPPORTED;
            formats[n_formats++] = (MetaKmsConstraintsFormat) {
              .plane_id = wire.plane_id,
              .format = wire.format,
              .modifier = wire.modifier,
              .implicit = !!(wire.layout_flags &
                             DRM_MODE_CONSTRAINTS_LAYOUT_IMPLICIT),
              .permits_native = !!(wire.storage_flags &
                                   DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_NATIVE),
              .permits_imported = !!(wire.storage_flags &
                                     DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_IMPORTED),
              .plane_count = wire.plane_count,
              .pitch_alignment = wire.pitch_alignment,
              .offset_alignment = wire.offset_alignment,
              .max_pitch = wire.max_pitch,
              .size = {
                .min_width = wire.min_width,
                .min_height = wire.min_height,
                .max_width = wire.max_width,
                .max_height = wire.max_height,
              },
            };
            break;
          }
        case DRM_MODE_CONSTRAINTS_RECORD_PROPERTY:
          {
            struct drm_mode_constraints_property wire;

            if (record.length != sizeof (wire))
              return DECODE_RESULT_UNSUPPORTED;
            if (n_properties == property_capacity)
              goto invalid;
            memcpy (&wire, data + offset, sizeof (wire));
            if (wire.pad != 0)
              goto invalid;
            if (wire.type != DRM_MODE_PROP_RANGE &&
                wire.type != DRM_MODE_PROP_SIGNED_RANGE &&
                wire.type != DRM_MODE_PROP_ENUM &&
                wire.type != DRM_MODE_PROP_BITMASK)
              return DECODE_RESULT_UNSUPPORTED;
            properties[n_properties++] = (MetaKmsConstraintsProperty) {
              .object_id = wire.object_id,
              .property_id = wire.property_id,
              .type = wire.type,
              .minimum = wire.minimum,
              .maximum = wire.maximum,
              .mask = wire.mask,
            };
            break;
          }
        case DRM_MODE_CONSTRAINTS_RECORD_PLANE_LIMIT:
          {
            struct drm_mode_constraints_plane_limit wire;
            g_autofree uint32_t *plane_ids = NULL;
            size_t ids_size;
            size_t unpadded_size;
            size_t expected_size;

            if (record.length < sizeof (wire))
              return DECODE_RESULT_UNSUPPORTED;
            if (n_plane_limits == plane_limit_capacity)
              goto invalid;
            memcpy (&wire, data + offset, sizeof (wire));
            if (wire.count_planes == 0 ||
                wire.count_planes > DRM_MODE_CONSTRAINTS_MAX_PLANES_PER_LIMIT ||
                wire.max_active == 0 ||
                wire.max_active > wire.count_planes)
              goto invalid;

            ids_size = sizeof (*plane_ids) * wire.count_planes;
            unpadded_size = sizeof (wire) + ids_size;
            expected_size = (unpadded_size + 7) & ~(size_t) 7;
            if (record.length != expected_size ||
                !bytes_are_zero (data + offset + unpadded_size,
                                 expected_size - unpadded_size))
              goto invalid;

            plane_ids = g_try_new (uint32_t, wire.count_planes);
            if (!plane_ids)
              {
                g_set_error_literal (error,
                                     G_IO_ERROR,
                                     G_IO_ERROR_NO_SPACE,
                                     "Allocate decoded KMS plane IDs");
                return DECODE_RESULT_INVALID;
              }
            memcpy (plane_ids,
                    data + offset + sizeof (wire),
                    ids_size);
            plane_limits[n_plane_limits++] = (MetaKmsConstraintsPlaneLimit) {
              .max_active = wire.max_active,
              .plane_ids = plane_ids,
              .n_plane_ids = wire.count_planes,
            };
            g_ptr_array_add (plane_id_arrays, g_steal_pointer (&plane_ids));
            break;
          }
        default:
          if (record.flags & DRM_MODE_CONSTRAINTS_RECORD_REQUIRED)
            return DECODE_RESULT_UNSUPPORTED;
          break;
        }

      offset += record.length;
    }

  if (offset != description_end || !has_output || n_formats == 0)
    goto invalid;

  *out_description = meta_kms_constraints_description_new (&output,
                                                            formats,
                                                            n_formats,
                                                            properties,
                                                            n_properties,
                                                            plane_limits,
                                                            n_plane_limits,
                                                            error);
  return *out_description ? DECODE_RESULT_OK : DECODE_RESULT_INVALID;

invalid:
  set_invalid_error (error, "Malformed KMS constraints description");
  return DECODE_RESULT_INVALID;
}

MetaKmsConstraintsList *
meta_kms_constraints_decode (const void  *data,
                             size_t       size,
                             GError     **error)
{
  const uint8_t *bytes = data;
  struct drm_mode_constraints_list header;
  g_autofree MetaKmsConstraintsListEntrySpec *entries = NULL;
  g_autofree MetaKmsConstraintsDescription **descriptions = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;
  uint64_t suggested_id;
  size_t entries_length;
  size_t n_entries = 0;
  uint32_t i;

  if (!data || size < sizeof (header) ||
      size > DRM_MODE_CONSTRAINTS_MAX_BYTES)
    goto invalid_header;

  memcpy (&header, bytes, sizeof (header));
  if (header.version != DRM_MODE_CONSTRAINTS_VERSION)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_SUPPORTED,
                           "Unsupported KMS constraints list encoding");
      return NULL;
    }
  if (header.length != size ||
      header.generation == 0 ||
      header.selected_id == 0 ||
      header.count_entries == 0 ||
      header.count_entries > DRM_MODE_CONSTRAINTS_MAX_ENTRIES ||
      header.pad != 0 ||
      header.reserved[0] != 0 ||
      header.reserved[1] != 0 ||
      header.entries_offset % 8 != 0 ||
      header.entry_size % 8 != 0 ||
      header.entry_size != sizeof (struct drm_mode_constraints) ||
      header.entries_offset < sizeof (header) ||
      !array_is_valid (size,
                       header.entries_offset,
                       header.count_entries,
                       header.entry_size,
                       sizeof (struct drm_mode_constraints)))
    goto invalid_header;

  entries_length = (size_t) header.count_entries * header.entry_size;

  entries = g_try_new0 (MetaKmsConstraintsListEntrySpec,
                        header.count_entries);
  descriptions = g_try_new0 (MetaKmsConstraintsDescription *,
                             header.count_entries);
  if (!entries || !descriptions)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_NO_SPACE,
                           "Allocate decoded KMS constraints entries");
      return NULL;
    }

  suggested_id = header.suggested_id;
  for (i = 0; i < header.count_entries; i++)
    {
      struct drm_mode_constraints entry;
      DecodeResult result;
      size_t entry_offset = header.entries_offset +
                            (size_t) i * header.entry_size;
      uint32_t j;

      memcpy (&entry, bytes + entry_offset, sizeof (entry));
      if (entry.id == 0 ||
          entry.pad != 0 ||
          entry.reserved[0] != 0 ||
          entry.reserved[1] != 0)
        goto invalid_entry;

      for (j = 0; j < i; j++)
        {
          struct drm_mode_constraints previous;
          size_t previous_offset = header.entries_offset +
                                   (size_t) j * header.entry_size;

          memcpy (&previous, bytes + previous_offset, sizeof (previous));
          if (entry.id == previous.id)
            goto invalid_entry;
        }

      if ((entry.flags & ~DRM_MODE_CONSTRAINTS_SELECTABLE) != 0)
        {
          result = DECODE_RESULT_UNSUPPORTED;
        }
      else
        {
          result = decode_description (bytes,
                                       size,
                                       &entry,
                                       header.entries_offset,
                                       entries_length,
                                       &descriptions[n_entries],
                                       error);
        }
      if (result == DECODE_RESULT_INVALID)
        goto fail;
      if (result == DECODE_RESULT_UNSUPPORTED)
        {
          if (entry.id == header.selected_id)
            {
              g_set_error_literal (error,
                                   G_IO_ERROR,
                                   G_IO_ERROR_NOT_SUPPORTED,
                                   "Selected KMS constraints are unsupported");
              goto fail;
            }
          if (entry.id == suggested_id)
            suggested_id = 0;
          continue;
        }

      entries[n_entries] = (MetaKmsConstraintsListEntrySpec) {
        .id = entry.id,
        .selectable = !!(entry.flags & DRM_MODE_CONSTRAINTS_SELECTABLE),
        .description = descriptions[n_entries],
      };
      n_entries++;
    }

  list = meta_kms_constraints_list_new (header.generation,
                                        header.selected_id,
                                        suggested_id,
                                        entries,
                                        n_entries,
                                        error);
  if (!list)
    goto fail;

  for (i = 0; i < n_entries; i++)
    meta_kms_constraints_description_unref (descriptions[i]);
  return g_steal_pointer (&list);

invalid_header:
  set_invalid_error (error, "Malformed KMS constraints list header");
  return NULL;

invalid_entry:
  set_invalid_error (error, "Malformed KMS constraints list entry");
fail:
  for (i = 0; i < n_entries; i++)
    meta_kms_constraints_description_unref (descriptions[i]);
  return NULL;
}
