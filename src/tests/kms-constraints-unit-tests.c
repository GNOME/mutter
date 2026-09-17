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

#include <errno.h>
#include <drm_fourcc.h>
#include <gio/gio.h>
#include <string.h>
#include <xf86drmMode.h>

#include "backends/native/meta-drm-constraints.h"
#include "backends/native/meta-kms-constraints-decoder.h"
#include "backends/native/meta-kms-constraints-event.h"
#include "backends/native/meta-kms-constraints-ioctl.h"
#include "backends/native/meta-kms-constraints-list.h"
#include "backends/native/meta-kms-constraints-query.h"
#include "backends/native/meta-kms-constraints-target.h"
#include "backends/native/meta-kms-constraints.h"

#define TEST_FORMAT_MODIFIER UINT64_C (1)

typedef struct _TestConstraintsPlaneLimit
{
  struct drm_mode_constraints_record header;
  uint32_t max_active;
  uint32_t count_planes;
  uint32_t plane_ids[2];
} TestConstraintsPlaneLimit;

G_STATIC_ASSERT (sizeof (TestConstraintsPlaneLimit) == 32);

typedef struct _TestConstraintsBlob
{
  struct drm_mode_constraints_list list;
  struct drm_mode_constraints entry;
  struct drm_mode_constraints_description description;
  struct drm_mode_constraints_output_size output;
  struct drm_mode_constraints_plane_format format;
  struct drm_mode_constraints_plane_geometry geometry;
  struct drm_mode_constraints_property property;
  TestConstraintsPlaneLimit plane_limit;
  struct drm_mode_constraints_record extension;
} TestConstraintsBlob;

typedef struct _TestConstraintsPayload
{
  struct drm_mode_constraints_description description;
  struct drm_mode_constraints_output_size output;
  struct drm_mode_constraints_plane_format format;
  struct drm_mode_constraints_plane_geometry geometry;
  struct drm_mode_constraints_property property;
  TestConstraintsPlaneLimit plane_limit;
  struct drm_mode_constraints_record extension;
} TestConstraintsPayload;

typedef struct _TestTwoEntryConstraintsBlob
{
  struct drm_mode_constraints_list list;
  struct drm_mode_constraints entries[2];
  TestConstraintsPayload payloads[2];
} TestTwoEntryConstraintsBlob;

static TestConstraintsBlob
create_constraints_blob (void)
{
  TestConstraintsBlob blob = {
    .list = {
      .version = DRM_MODE_CONSTRAINTS_VERSION,
      .length = sizeof (blob),
      .generation = 17,
      .selected_id = 5,
      .suggested_id = 5,
      .count_entries = 1,
      .entries_offset = G_STRUCT_OFFSET (TestConstraintsBlob, entry),
      .entry_size = sizeof (blob.entry),
    },
    .entry = {
      .id = 5,
      .flags = DRM_MODE_CONSTRAINTS_SELECTABLE,
      .description_offset = G_STRUCT_OFFSET (TestConstraintsBlob, description),
      .description_length = sizeof (blob.description) +
                            sizeof (blob.output) +
                            sizeof (blob.format) +
                            sizeof (blob.geometry) +
                            sizeof (blob.property) +
                            sizeof (blob.plane_limit) +
                            sizeof (blob.extension),
    },
    .description = {
      .version = DRM_MODE_CONSTRAINTS_VERSION,
      .length = sizeof (blob.description) +
                sizeof (blob.output) +
                sizeof (blob.format) +
                sizeof (blob.geometry) +
                sizeof (blob.property) +
                sizeof (blob.plane_limit) +
                sizeof (blob.extension),
      .record_count = 6,
      .records_offset = G_STRUCT_OFFSET (TestConstraintsBlob, output),
    },
    .output = {
      .header = {
        .type = DRM_MODE_CONSTRAINTS_RECORD_OUTPUT_SIZE,
        .flags = DRM_MODE_CONSTRAINTS_RECORD_REQUIRED,
        .length = sizeof (blob.output),
      },
      .min_width = 1920,
      .min_height = 1080,
      .max_width = 1920,
      .max_height = 1080,
    },
    .format = {
      .header = {
        .type = DRM_MODE_CONSTRAINTS_RECORD_PLANE_FORMAT,
        .flags = DRM_MODE_CONSTRAINTS_RECORD_REQUIRED,
        .length = sizeof (blob.format),
      },
      .plane_id = 7,
      .format = DRM_FORMAT_XRGB8888,
      .modifier = TEST_FORMAT_MODIFIER,
      .min_width = 64,
      .min_height = 32,
      .max_width = 4096,
      .max_height = 4096,
      .storage_flags = DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_NATIVE |
                       DRM_MODE_CONSTRAINTS_FORMAT_STORAGE_IMPORTED,
      .plane_count = 1,
      .pitch_alignment = 4,
      .offset_alignment = 4,
      .max_pitch = 65536,
    },
    .geometry = {
      .header = {
        .type = DRM_MODE_CONSTRAINTS_RECORD_PLANE_GEOMETRY,
        .flags = DRM_MODE_CONSTRAINTS_RECORD_REQUIRED,
        .length = sizeof (blob.geometry),
      },
      .plane_id = 7,
      .flags = DRM_MODE_CONSTRAINTS_GEOMETRY_CROP |
               DRM_MODE_CONSTRAINTS_GEOMETRY_FRACTIONAL_SOURCE |
               DRM_MODE_CONSTRAINTS_GEOMETRY_POSITION,
      .min_scale = 1U << 15,
      .max_scale = 2U << 16,
    },
    .property = {
      .header = {
        .type = DRM_MODE_CONSTRAINTS_RECORD_PROPERTY,
        .flags = DRM_MODE_CONSTRAINTS_RECORD_REQUIRED,
        .length = sizeof (blob.property),
      },
      .object_id = 7,
      .property_id = 8,
      .type = DRM_MODE_PROP_RANGE,
      .minimum = 1,
      .maximum = 4,
    },
    .plane_limit = {
      .header = {
        .type = DRM_MODE_CONSTRAINTS_RECORD_PLANE_LIMIT,
        .flags = DRM_MODE_CONSTRAINTS_RECORD_REQUIRED,
        .length = sizeof (blob.plane_limit),
      },
      .max_active = 1,
      .count_planes = 2,
      .plane_ids = { 7, 9 },
    },
    .extension = {
      .type = 99,
      .length = sizeof (blob.extension),
    },
  };

  return blob;
}

static void
meta_test_kms_constraints_event (void)
{
  struct drm_event_kms_constraints_list_changed event = {
    .base = {
      .type = DRM_EVENT_KMS_CONSTRAINTS_LIST_CHANGED,
      .length = sizeof (event),
    },
    .crtc_id = 7,
    .generation = 11,
  };
  MetaKmsConstraintsListChange change;
  uint8_t unaligned[sizeof (event) + 1];

  g_assert_true (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                 &change));
  g_assert_cmpuint (change.crtc_id, ==, 7);
  g_assert_cmpuint (change.generation, ==, 11);
  g_assert_false (change.is_closed);

  memcpy (unaligned + 1, &event, sizeof (event));
  g_assert_true (meta_kms_constraints_event_decode_list_change (
                   (const struct drm_event *) (unaligned + 1),
                   &change));

  event.flags = DRM_KMS_CONSTRAINTS_LIST_CLOSED;
  event.generation = 0;
  g_assert_true (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                 &change));
  g_assert_true (change.is_closed);

  event.base.type++;
  g_assert_false (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                  &change));
  event.base.type--;
  event.base.length--;
  g_assert_false (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                  &change));
  event.base.length++;
  event.crtc_id = 0;
  g_assert_false (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                  &change));
  event.crtc_id = 7;
  event.reserved = 1;
  g_assert_false (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                  &change));
  event.reserved = 0;
  event.flags = 2;
  g_assert_false (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                  &change));
  event.flags = DRM_KMS_CONSTRAINTS_LIST_CLOSED;
  event.generation = 11;
  g_assert_false (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                  &change));
  event.flags = 0;
  event.generation = 0;
  g_assert_false (meta_kms_constraints_event_decode_list_change (&event.base,
                                                                  &change));
}

static TestTwoEntryConstraintsBlob
create_two_entry_constraints_blob (void)
{
  TestConstraintsBlob single = create_constraints_blob ();
  TestTwoEntryConstraintsBlob blob = {
    .list = single.list,
    .entries = { single.entry, single.entry },
    .payloads = {
      {
        .description = single.description,
        .output = single.output,
        .format = single.format,
        .geometry = single.geometry,
        .property = single.property,
        .plane_limit = single.plane_limit,
        .extension = single.extension,
      },
      {
        .description = single.description,
        .output = single.output,
        .format = single.format,
        .geometry = single.geometry,
        .property = single.property,
        .plane_limit = single.plane_limit,
        .extension = single.extension,
      },
    },
  };
  size_t i;

  blob.list.length = sizeof (blob);
  blob.list.count_entries = G_N_ELEMENTS (blob.entries);
  blob.list.entries_offset = G_STRUCT_OFFSET (TestTwoEntryConstraintsBlob,
                                              entries);
  blob.list.entry_size = sizeof (blob.entries[0]);
  blob.list.suggested_id = 9;
  blob.entries[1].id = 9;
  blob.payloads[1].extension.flags = DRM_MODE_CONSTRAINTS_RECORD_REQUIRED;

  for (i = 0; i < G_N_ELEMENTS (blob.entries); i++)
    {
      blob.entries[i].description_offset =
        G_STRUCT_OFFSET (TestTwoEntryConstraintsBlob, payloads) +
        i * sizeof (blob.payloads[0]);
      blob.entries[i].description_length = sizeof (blob.payloads[i]);
      blob.payloads[i].description.length = sizeof (blob.payloads[i]);
      blob.payloads[i].description.records_offset =
        blob.entries[i].description_offset +
        G_STRUCT_OFFSET (TestConstraintsPayload, output);
    }

  return blob;
}

typedef struct _TestConstraintsQuery
{
  const void *snapshot;
  uint32_t snapshot_size;
  uint64_t generation;
  const void *replacement_snapshot;
  uint32_t replacement_size;
  uint64_t replacement_generation;
  gboolean replace_on_fetch;
  gboolean always_stale;
  int result;
  unsigned int calls;
} TestConstraintsQuery;

typedef struct _TestConstraintsIoctl
{
  const void *snapshot;
  uint32_t snapshot_size;
  uint64_t generation;
  int error_number;
  gboolean malformed_result;
  unsigned int calls;
} TestConstraintsIoctl;

static TestConstraintsIoctl constraints_ioctl;

int __wrap_drmIoctl (int fd, unsigned long command, void *data);

int
__wrap_drmIoctl (int            fd,
                 unsigned long  command,
                 void          *data)
{
  struct drm_mode_list_constraints *request = data;

  g_assert_cmpint (fd, ==, 41);
  g_assert_cmpuint (command, ==, DRM_IOCTL_MODE_LIST_CONSTRAINTS);
  g_assert_cmpuint (request->crtc_id, ==, 19);
  g_assert_cmpuint (request->flags, ==, 0);
  g_assert_cmpuint (request->pad, ==, 0);
  g_assert_cmpuint (request->reserved[0], ==, 0);
  g_assert_cmpuint (request->reserved[1], ==, 0);
  constraints_ioctl.calls++;

  if (constraints_ioctl.malformed_result)
    return -2;

  if (constraints_ioctl.error_number != 0)
    {
      errno = constraints_ioctl.error_number;
      return -1;
    }

  if (request->data == 0)
    {
      g_assert_cmpuint (request->generation, ==, 0);
      g_assert_cmpuint (request->size, ==, 0);
    }
  else
    {
      g_assert_cmpuint (request->generation, ==,
                        constraints_ioctl.generation);
      g_assert_cmpuint (request->size, >=,
                        constraints_ioctl.snapshot_size);
      memcpy ((void *) (uintptr_t) request->data,
              constraints_ioctl.snapshot,
              constraints_ioctl.snapshot_size);
    }

  request->generation = constraints_ioctl.generation;
  request->size = constraints_ioctl.snapshot_size;
  return 0;
}

static int
query_constraints (gpointer  user_data,
                   uint32_t  crtc_id,
                   uint64_t *generation,
                   void     *data,
                   uint32_t *size)
{
  TestConstraintsQuery *query = user_data;
  uint32_t capacity = *size;

  g_assert_cmpuint (crtc_id, ==, 19);
  query->calls++;

  if (query->result != 0)
    return query->result;
  if (query->always_stale)
    return -ESTALE;
  if (query->replace_on_fetch && data)
    {
      query->snapshot = query->replacement_snapshot;
      query->snapshot_size = query->replacement_size;
      query->generation = query->replacement_generation;
      query->replace_on_fetch = FALSE;
      return -ESTALE;
    }
  if (*generation != 0 && *generation != query->generation)
    return -ESTALE;

  *generation = query->generation;
  *size = query->snapshot_size;
  if (!data)
    return 0;
  if (capacity < query->snapshot_size)
    return -ENOSPC;

  memcpy (data, query->snapshot, query->snapshot_size);
  return 0;
}

static const MetaKmsConstraintsSize output_size = {
  .min_width = 1280,
  .min_height = 720,
  .max_width = 3840,
  .max_height = 2160,
};

static const MetaKmsConstraintsFormat formats[] = {
  {
    .plane_id = 7,
    .format = DRM_FORMAT_XRGB8888,
    .modifier = DRM_FORMAT_MOD_LINEAR,
    .permits_native = TRUE,
    .permits_imported = TRUE,
    .plane_count = 1,
    .pitch_alignment = 4,
    .offset_alignment = 4,
    .max_pitch = 65536,
    .size = {
      .min_width = 1280,
      .min_height = 720,
      .max_width = 1920,
      .max_height = 1080,
    },
  },
  {
    .plane_id = 7,
    .format = DRM_FORMAT_XRGB8888,
    .modifier = TEST_FORMAT_MODIFIER,
    .permits_imported = TRUE,
    .plane_count = 1,
    .pitch_alignment = 256,
    .offset_alignment = 4096,
    .max_pitch = 65536,
    .size = {
      .min_width = 1920,
      .min_height = 1080,
      .max_width = 3840,
      .max_height = 2160,
    },
  },
  {
    .plane_id = 7,
    .format = DRM_FORMAT_XRGB8888,
    .modifier = 0,
    .implicit = TRUE,
    .permits_native = TRUE,
    .plane_count = 1,
    .pitch_alignment = 4,
    .offset_alignment = 4,
    .max_pitch = 65536,
    .size = {
      .min_width = 1280,
      .min_height = 720,
      .max_width = 1280,
      .max_height = 720,
    },
  },
};

static const MetaKmsConstraintsPlaneGeometry plane_geometries[] = {
  {
    .plane_id = 7,
    .permits_crop = TRUE,
    .permits_fractional_source = TRUE,
    .permits_position = TRUE,
    .min_scale = 1U << 15,
    .max_scale = 2U << 16,
  },
};

static MetaKmsConstraintsDescription *
create_description (const MetaKmsConstraintsProperty *properties,
                    size_t                            n_properties)
{
  g_autoptr (GError) error = NULL;
  MetaKmsConstraintsDescription *description;

  description = meta_kms_constraints_description_new (&output_size,
                                                       formats,
                                                       G_N_ELEMENTS (formats),
                                                       plane_geometries,
                                                       G_N_ELEMENTS (plane_geometries),
                                                       properties,
                                                       n_properties,
                                                       NULL,
                                                       0,
                                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (description);
  return description;
}

static void
meta_test_kms_constraints_plane_geometry (void)
{
  const MetaKmsConstraintsPlaneGeometry fixed_geometry = {
    .plane_id = 7,
    .min_scale = 1U << 16,
    .max_scale = 1U << 16,
  };
  const MetaFixed16Rectangle full_source = {
    .width = 1920U << 16,
    .height = 1080U << 16,
  };
  const MtkRectangle full_destination = {
    .width = 1920,
    .height = 1080,
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsDescription) description = NULL;
  MetaFixed16Rectangle source;
  MtkRectangle destination;

  description = meta_kms_constraints_description_new (
    &output_size,
    formats,
    G_N_ELEMENTS (formats),
    &fixed_geometry,
    1,
    NULL,
    0,
    NULL,
    0,
    &error);
  g_assert_no_error (error);
  g_assert_true (meta_kms_constraints_description_allows_plane_geometry (
                   description, 7, 1920, 1080,
                   full_source, full_destination));

  source = full_source;
  source.x = 1U << 16;
  g_assert_false (meta_kms_constraints_description_allows_plane_geometry (
                    description, 7, 1920, 1080, source, full_destination));
  source = full_source;
  source.width--;
  g_assert_false (meta_kms_constraints_description_allows_plane_geometry (
                    description, 7, 1920, 1080, source, full_destination));
  destination = full_destination;
  destination.x = 1;
  g_assert_false (meta_kms_constraints_description_allows_plane_geometry (
                    description, 7, 1920, 1080, full_source, destination));
  destination = full_destination;
  destination.width /= 2;
  g_assert_false (meta_kms_constraints_description_allows_plane_geometry (
                    description, 7, 1920, 1080, full_source, destination));
  g_assert_true (meta_kms_constraints_description_allows_plane_geometry (
                   description, 9, 1920, 1080,
                   full_source, full_destination));
}

static void
meta_test_kms_constraints_sizes (void)
{
  g_autoptr (MetaKmsConstraintsDescription) description =
    create_description (NULL, 0);
  const MetaKmsConstraintsSize *output;

  output = meta_kms_constraints_description_get_output (description);
  g_assert_true (meta_kms_constraints_size_contains (output, 1280, 720));
  g_assert_true (meta_kms_constraints_size_contains (output, 3840, 2160));
  g_assert_false (meta_kms_constraints_size_contains (output, 1279, 720));
  g_assert_false (meta_kms_constraints_size_contains (output, 3840, 2161));
}

static void
meta_test_kms_constraints_formats (void)
{
  g_autoptr (MetaKmsConstraintsDescription) description =
    create_description (NULL, 0);
  const MetaKmsConstraintsFormat *stored_formats;
  size_t n_formats;
  const uint32_t pitches[] = { 7680 };
  const uint32_t offsets[] = { 4096 };
  const uint32_t unaligned_pitches[] = { 7684 };

  stored_formats =
    meta_kms_constraints_description_get_formats (description, &n_formats);
  g_assert_cmpuint (n_formats, ==, G_N_ELEMENTS (formats));
  g_assert_cmpuint (stored_formats[0].plane_id, ==, 7);

  g_assert_true (meta_kms_constraints_description_allows_explicit_layout (
                   description,
                   7,
                   DRM_FORMAT_XRGB8888,
                   DRM_FORMAT_MOD_LINEAR,
                   META_KMS_CONSTRAINTS_STORAGE_NATIVE,
                   1920,
                   1080));
  g_assert_true (meta_kms_constraints_description_allows_explicit_layout (
                   description,
                   7,
                   DRM_FORMAT_XRGB8888,
                   TEST_FORMAT_MODIFIER,
                   META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                   3840,
                   2160));
  g_assert_false (meta_kms_constraints_description_allows_explicit_layout (
                    description,
                    7,
                    DRM_FORMAT_XRGB8888,
                    TEST_FORMAT_MODIFIER,
                    META_KMS_CONSTRAINTS_STORAGE_NATIVE,
                    3840,
                    2160));
  g_assert_false (meta_kms_constraints_description_allows_explicit_layout (
                    description,
                    8,
                    DRM_FORMAT_XRGB8888,
                    DRM_FORMAT_MOD_LINEAR,
                    META_KMS_CONSTRAINTS_STORAGE_NATIVE,
                    1920,
                    1080));
  g_assert_false (meta_kms_constraints_description_allows_explicit_layout (
                    description,
                    7,
                    DRM_FORMAT_XRGB8888,
                    DRM_FORMAT_MOD_LINEAR,
                    META_KMS_CONSTRAINTS_STORAGE_NATIVE,
                    1921,
                    1080));
  g_assert_true (meta_kms_constraints_description_allows_implicit_layout (
                   description,
                   7,
                   DRM_FORMAT_XRGB8888,
                   META_KMS_CONSTRAINTS_STORAGE_NATIVE,
                   1280,
                   720));
  g_assert_false (meta_kms_constraints_description_allows_implicit_layout (
                    description,
                    7,
                    DRM_FORMAT_XRGB8888,
                    META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                    1280,
                    720));
  g_assert_false (meta_kms_constraints_description_allows_implicit_layout (
                    description,
                    7,
                    DRM_FORMAT_XRGB8888,
                    META_KMS_CONSTRAINTS_STORAGE_NATIVE,
                    1920,
                    1080));

  g_assert_true (meta_kms_constraints_description_allows_buffer_layout (
                   description,
                   7,
                   DRM_FORMAT_XRGB8888,
                   TEST_FORMAT_MODIFIER,
                   FALSE,
                   META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                   1920,
                   1080,
                   G_N_ELEMENTS (pitches),
                   pitches,
                   offsets));
  g_assert_false (meta_kms_constraints_description_allows_buffer_layout (
                    description,
                    7,
                    DRM_FORMAT_XRGB8888,
                    TEST_FORMAT_MODIFIER,
                    FALSE,
                    META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                    1920,
                    1080,
                    G_N_ELEMENTS (unaligned_pitches),
                    unaligned_pitches,
                    offsets));
}

static void
meta_test_kms_constraints_allocation_views (void)
{
  g_autoptr (MetaKmsConstraintsDescription) description =
    create_description (NULL, 0);
  g_autoptr (GArray) drm_formats = NULL;
  g_autoptr (GArray) modifiers = NULL;

  drm_formats =
    meta_kms_constraints_description_copy_drm_formats_for_plane (description,
                                                                 7,
                                                                 META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                                                                 1920,
                                                                 1080);
  g_assert_cmpuint (drm_formats->len, ==, 1);
  g_assert_cmphex (g_array_index (drm_formats, uint32_t, 0),
                   ==,
                   DRM_FORMAT_XRGB8888);

  modifiers =
    meta_kms_constraints_description_copy_explicit_modifiers_for_format (
      description,
      7,
      DRM_FORMAT_XRGB8888,
      META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
      1920,
      1080);
  g_assert_cmpuint (modifiers->len, ==, 2);
  g_assert_cmphex (g_array_index (modifiers, uint64_t, 0),
                   ==,
                   DRM_FORMAT_MOD_LINEAR);
  g_assert_cmphex (g_array_index (modifiers, uint64_t, 1),
                   ==,
                   TEST_FORMAT_MODIFIER);

  g_clear_pointer (&modifiers, g_array_unref);
  modifiers =
    meta_kms_constraints_description_copy_explicit_modifiers_for_format (
      description,
      7,
      DRM_FORMAT_XRGB8888,
      META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
      1280,
      720);
  g_assert_cmpuint (modifiers->len, ==, 1);
  g_assert_cmphex (g_array_index (modifiers, uint64_t, 0),
                   ==,
                   DRM_FORMAT_MOD_LINEAR);

  g_clear_pointer (&drm_formats, g_array_unref);
  drm_formats =
    meta_kms_constraints_description_copy_drm_formats_for_plane (description,
                                                                 7,
                                                                 META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                                                                 3840,
                                                                 2160);
  g_assert_cmpuint (drm_formats->len, ==, 1);

  g_clear_pointer (&modifiers, g_array_unref);
  modifiers =
    meta_kms_constraints_description_copy_explicit_modifiers_for_format (
      description,
      7,
      DRM_FORMAT_XRGB8888,
      META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
      3840,
      2160);
  g_assert_cmpuint (modifiers->len, ==, 1);
  g_assert_cmphex (g_array_index (modifiers, uint64_t, 0),
                   ==,
                   TEST_FORMAT_MODIFIER);

  g_clear_pointer (&drm_formats, g_array_unref);
  drm_formats =
    meta_kms_constraints_description_copy_drm_formats_for_plane (description,
                                                                 8,
                                                                 META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                                                                 1920,
                                                                 1080);
  g_assert_cmpuint (drm_formats->len, ==, 0);
}

static void
meta_test_kms_constraints_properties (void)
{
  const MetaKmsConstraintsProperty properties[] = {
    {
      .object_id = 7,
      .property_id = 11,
      .type = DRM_MODE_PROP_RANGE,
      .minimum = 2,
      .maximum = 4,
    },
    {
      .object_id = 7,
      .property_id = 12,
      .type = DRM_MODE_PROP_SIGNED_RANGE,
      .minimum = (uint64_t) -2,
      .maximum = 3,
    },
    {
      .object_id = 7,
      .property_id = 13,
      .type = DRM_MODE_PROP_ENUM,
      .mask = UINT64_C (1) << 5,
    },
    {
      .object_id = 7,
      .property_id = 14,
      .type = DRM_MODE_PROP_BITMASK,
      .mask = 0x5,
    },
  };
  g_autoptr (MetaKmsConstraintsDescription) description =
    create_description (properties, G_N_ELEMENTS (properties));
  const MetaKmsConstraintsProperty *property;
  size_t n_properties;

  meta_kms_constraints_description_get_properties (description, &n_properties);
  g_assert_cmpuint (n_properties, ==, G_N_ELEMENTS (properties));

  property = meta_kms_constraints_description_find_property (description,
                                                              7,
                                                              11);
  g_assert_nonnull (property);
  g_assert_true (meta_kms_constraints_property_matches (property, 2));
  g_assert_true (meta_kms_constraints_property_matches (property, 4));
  g_assert_false (meta_kms_constraints_property_matches (property, 5));

  property = meta_kms_constraints_description_find_property (description,
                                                              7,
                                                              12);
  g_assert_true (meta_kms_constraints_property_matches (property,
                                                        (uint64_t) -2));
  g_assert_false (meta_kms_constraints_property_matches (property,
                                                         (uint64_t) -3));

  property = meta_kms_constraints_description_find_property (description,
                                                              7,
                                                              13);
  g_assert_true (meta_kms_constraints_property_matches (property, 5));
  g_assert_false (meta_kms_constraints_property_matches (property, 4));

  property = meta_kms_constraints_description_find_property (description,
                                                              7,
                                                              14);
  g_assert_true (meta_kms_constraints_property_matches (property, 0x4));
  g_assert_false (meta_kms_constraints_property_matches (property, 0x6));

  g_assert_null (meta_kms_constraints_description_find_property (description,
                                                                 7,
                                                                 15));
}

static void
meta_test_kms_constraints_owns_description (void)
{
  MetaKmsConstraintsSize source_output = output_size;
  MetaKmsConstraintsFormat source_format = formats[0];
  MetaKmsConstraintsPlaneGeometry source_geometry = plane_geometries[0];
  MetaKmsConstraintsProperty source_property = {
    .object_id = 7,
    .property_id = 11,
    .type = DRM_MODE_PROP_RANGE,
    .maximum = 1,
  };
  uint32_t source_plane_ids[] = { 7, 9 };
  MetaKmsConstraintsPlaneLimit source_plane_limit = {
    .max_active = 1,
    .plane_ids = source_plane_ids,
    .n_plane_ids = G_N_ELEMENTS (source_plane_ids),
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsDescription) description = NULL;
  const MetaKmsConstraintsSize *stored_output;
  const MetaKmsConstraintsFormat *stored_format;
  const MetaKmsConstraintsPlaneGeometry *stored_geometry;
  const MetaKmsConstraintsProperty *stored_property;
  const MetaKmsConstraintsPlaneLimit *stored_plane_limit;
  size_t count;

  description = meta_kms_constraints_description_new (&source_output,
                                                       &source_format,
                                                       1,
                                                       &source_geometry,
                                                       1,
                                                       &source_property,
                                                       1,
                                                       &source_plane_limit,
                                                       1,
                                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (description);

  source_output.max_width = 1;
  source_format.plane_id = 1;
  source_geometry.plane_id = 1;
  source_property.object_id = 1;
  source_plane_ids[0] = 1;

  stored_output = meta_kms_constraints_description_get_output (description);
  stored_format =
    meta_kms_constraints_description_get_formats (description, &count);
  g_assert_cmpuint (count, ==, 1);
  stored_plane_limit =
    meta_kms_constraints_description_get_plane_limits (description, &count);
  g_assert_cmpuint (count, ==, 1);
  stored_geometry =
    meta_kms_constraints_description_get_plane_geometries (description,
                                                            &count);
  g_assert_cmpuint (count, ==, 1);
  stored_property =
    meta_kms_constraints_description_get_properties (description, &count);
  g_assert_cmpuint (count, ==, 1);

  g_assert_cmpuint (stored_output->max_width, ==, output_size.max_width);
  g_assert_cmpuint (stored_format->plane_id, ==, formats[0].plane_id);
  g_assert_cmpuint (stored_geometry->plane_id, ==, 7);
  g_assert_cmpuint (stored_property->object_id, ==, 7);
  g_assert_cmpuint (stored_plane_limit->plane_ids[0], ==, 7);
}

static void
meta_test_kms_constraints_reject_invalid (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsDescription) description = NULL;
  MetaKmsConstraintsFormat duplicate_formats[] = {
    formats[0],
    formats[0],
  };
  MetaKmsConstraintsFormat duplicate_implicit_formats[] = {
    formats[2],
    formats[2],
  };
  MetaKmsConstraintsProperty duplicate_properties[] = {
    {
      .object_id = 7,
      .property_id = 11,
      .type = DRM_MODE_PROP_RANGE,
      .maximum = 1,
    },
    {
      .object_id = 7,
      .property_id = 11,
      .type = DRM_MODE_PROP_BITMASK,
    },
  };
  MetaKmsConstraintsSize invalid_output = output_size;
  MetaKmsConstraintsProperty invalid_property = {
    .object_id = 7,
    .property_id = 11,
    .type = DRM_MODE_PROP_ENUM,
  };
  MetaKmsConstraintsFormat invalid_implicit = formats[0];
  uint32_t duplicate_plane_ids[] = { 7, 7 };
  MetaKmsConstraintsPlaneLimit invalid_plane_limit = {
    .max_active = 1,
    .plane_ids = duplicate_plane_ids,
    .n_plane_ids = G_N_ELEMENTS (duplicate_plane_ids),
  };

  invalid_output.min_width = 0;
  description = meta_kms_constraints_description_new (
    &invalid_output,
    formats,
    G_N_ELEMENTS (formats),
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  invalid_implicit.implicit = TRUE;
  invalid_implicit.modifier = TEST_FORMAT_MODIFIER;
  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    &invalid_implicit,
    1,
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  duplicate_implicit_formats[1].implicit = 2;
  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    duplicate_implicit_formats,
    G_N_ELEMENTS (duplicate_implicit_formats),
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    duplicate_formats,
    G_N_ELEMENTS (duplicate_formats),
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    formats,
    G_N_ELEMENTS (formats),
    NULL,
    0,
    &invalid_property,
    1,
    NULL,
    0,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    formats,
    G_N_ELEMENTS (formats),
    NULL,
    0,
    duplicate_properties,
    G_N_ELEMENTS (duplicate_properties),
    NULL,
    0,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    formats,
    G_N_ELEMENTS (formats),
    NULL,
    0,
    NULL,
    0,
    &invalid_plane_limit,
    1,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
meta_test_kms_constraints_list (void)
{
  g_autoptr (MetaKmsConstraintsDescription) first =
    create_description (NULL, 0);
  g_autoptr (MetaKmsConstraintsDescription) second =
    create_description (NULL, 0);
  MetaKmsConstraintsListEntrySpec entries[] = {
    {
      .id = 5,
      .selectable = FALSE,
      .description = first,
    },
    {
      .id = 9,
      .selectable = TRUE,
      .description = second,
    },
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;
  g_autoptr (MetaKmsConstraintsList) retained_list = NULL;
  const MetaKmsConstraintsListEntry *stored_entry;

  list = meta_kms_constraints_list_new (17,
                                        5,
                                        9,
                                        entries,
                                        G_N_ELEMENTS (entries),
                                        &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);

  g_clear_pointer (&first, meta_kms_constraints_description_unref);
  g_clear_pointer (&second, meta_kms_constraints_description_unref);

  g_assert_cmpuint (meta_kms_constraints_list_get_generation (list), ==, 17);
  g_assert_cmpuint (meta_kms_constraints_list_get_selected_id (list), ==, 5);
  g_assert_cmpuint (meta_kms_constraints_list_get_suggested_id (list), ==, 9);

  g_assert_cmpuint (meta_kms_constraints_list_get_n_entries (list), ==, 2);
  stored_entry = meta_kms_constraints_list_get_entry (list, 0);
  g_assert_cmpuint (meta_kms_constraints_list_entry_get_id (stored_entry),
                    ==,
                    5);
  g_assert_false (meta_kms_constraints_list_entry_is_selectable (stored_entry));
  g_assert_cmpuint (
    meta_kms_constraints_description_get_output (
      meta_kms_constraints_list_entry_get_description (
        stored_entry))->max_width,
    ==,
    output_size.max_width);

  stored_entry = meta_kms_constraints_list_find_entry (list, 9);
  g_assert_true (meta_kms_constraints_list_entry_is_selectable (stored_entry));
  g_assert_null (meta_kms_constraints_list_find_entry (list, 10));

  retained_list = meta_kms_constraints_list_ref (list);
  g_clear_pointer (&list, meta_kms_constraints_list_unref);
  stored_entry = meta_kms_constraints_list_find_entry (retained_list, 9);
  g_assert_nonnull (stored_entry);
  g_assert_cmpuint (meta_kms_constraints_list_entry_get_id (stored_entry),
                    ==,
                    9);
}

static void
meta_test_kms_constraints_list_reject_invalid (void)
{
  g_autoptr (MetaKmsConstraintsDescription) first =
    create_description (NULL, 0);
  g_autoptr (MetaKmsConstraintsDescription) second =
    create_description (NULL, 0);
  MetaKmsConstraintsListEntrySpec entries[] = {
    {
      .id = 5,
      .selectable = TRUE,
      .description = first,
    },
    {
      .id = 9,
      .selectable = FALSE,
      .description = second,
    },
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  list = meta_kms_constraints_list_new (0,
                                        5,
                                        0,
                                        entries,
                                        G_N_ELEMENTS (entries),
                                        &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  list = meta_kms_constraints_list_new (1,
                                        7,
                                        0,
                                        entries,
                                        G_N_ELEMENTS (entries),
                                        &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  list = meta_kms_constraints_list_new (1,
                                        5,
                                        9,
                                        entries,
                                        G_N_ELEMENTS (entries),
                                        &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  entries[1].id = 5;
  g_clear_error (&error);
  list = meta_kms_constraints_list_new (1,
                                        5,
                                        0,
                                        entries,
                                        G_N_ELEMENTS (entries),
                                        &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
meta_test_kms_constraints_decode (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;
  const MetaKmsConstraintsListEntry *entry;
  const MetaKmsConstraintsDescription *description;
  const MetaKmsConstraintsFormat *decoded_formats;
  const MetaKmsConstraintsPlaneGeometry *decoded_geometries;
  const MetaKmsConstraintsProperty *decoded_properties;
  const MetaKmsConstraintsPlaneLimit *decoded_plane_limits;
  const MetaKmsConstraintsSize *decoded_output;
  size_t n_formats;
  size_t n_geometries;
  size_t n_properties;
  size_t n_plane_limits;

  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);
  g_assert_cmpuint (meta_kms_constraints_list_get_generation (list), ==, 17);
  g_assert_cmpuint (meta_kms_constraints_list_get_selected_id (list), ==, 5);
  g_assert_cmpuint (meta_kms_constraints_list_get_suggested_id (list), ==, 5);
  g_assert_cmpuint (meta_kms_constraints_list_get_n_entries (list), ==, 1);

  entry = meta_kms_constraints_list_get_entry (list, 0);
  g_assert_cmpuint (meta_kms_constraints_list_entry_get_id (entry), ==, 5);
  g_assert_true (meta_kms_constraints_list_entry_is_selectable (entry));
  description = meta_kms_constraints_list_entry_get_description (entry);
  decoded_output = meta_kms_constraints_description_get_output (description);
  g_assert_cmpuint (decoded_output->min_width, ==, 1920);
  g_assert_cmpuint (decoded_output->max_height, ==, 1080);
  decoded_formats = meta_kms_constraints_description_get_formats (description,
                                                                   &n_formats);
  g_assert_cmpuint (n_formats, ==, 1);
  g_assert_cmpuint (decoded_formats[0].plane_id, ==, 7);
  g_assert_cmpuint (decoded_formats[0].format, ==, DRM_FORMAT_XRGB8888);
  g_assert_cmpuint (decoded_formats[0].modifier, ==, TEST_FORMAT_MODIFIER);
  g_assert_false (decoded_formats[0].implicit);
  g_assert_true (decoded_formats[0].permits_native);
  g_assert_true (decoded_formats[0].permits_imported);
  g_assert_cmpuint (decoded_formats[0].plane_count, ==, 1);
  g_assert_cmpuint (decoded_formats[0].pitch_alignment, ==, 4);
  g_assert_cmpuint (decoded_formats[0].offset_alignment, ==, 4);
  g_assert_cmpuint (decoded_formats[0].max_pitch, ==, 65536);
  decoded_geometries =
    meta_kms_constraints_description_get_plane_geometries (description,
                                                            &n_geometries);
  g_assert_cmpuint (n_geometries, ==, 1);
  g_assert_cmpuint (decoded_geometries[0].plane_id, ==, 7);
  g_assert_true (decoded_geometries[0].permits_crop);
  g_assert_true (decoded_geometries[0].permits_fractional_source);
  g_assert_true (decoded_geometries[0].permits_position);
  g_assert_cmpuint (decoded_geometries[0].min_scale, ==, 1U << 15);
  g_assert_cmpuint (decoded_geometries[0].max_scale, ==, 2U << 16);
  decoded_properties =
    meta_kms_constraints_description_get_properties (description,
                                                      &n_properties);
  g_assert_cmpuint (n_properties, ==, 1);
  g_assert_cmpuint (decoded_properties[0].object_id, ==, 7);
  g_assert_cmpuint (decoded_properties[0].property_id, ==, 8);
  g_assert_cmpuint (decoded_properties[0].type, ==, DRM_MODE_PROP_RANGE);
  g_assert_false (decoded_properties[0].applies_to_yuv_plane);
  g_assert_true (meta_kms_constraints_property_matches (&decoded_properties[0],
                                                        1));
  g_assert_true (meta_kms_constraints_property_matches (&decoded_properties[0],
                                                        4));
  g_assert_false (meta_kms_constraints_property_matches (&decoded_properties[0],
                                                         5));
  decoded_plane_limits =
    meta_kms_constraints_description_get_plane_limits (description,
                                                        &n_plane_limits);
  g_assert_cmpuint (n_plane_limits, ==, 1);
  g_assert_cmpuint (decoded_plane_limits[0].max_active, ==, 1);
  g_assert_cmpuint (decoded_plane_limits[0].n_plane_ids, ==, 2);
  g_assert_cmpuint (decoded_plane_limits[0].plane_ids[0], ==, 7);
  g_assert_cmpuint (decoded_plane_limits[0].plane_ids[1], ==, 9);
}

static void
meta_test_kms_constraints_decode_implicit (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;
  const MetaKmsConstraintsListEntry *entry;
  const MetaKmsConstraintsDescription *description;
  const MetaKmsConstraintsFormat *decoded_formats;
  size_t n_formats;

  blob.format.modifier = 0;
  blob.format.layout_flags = DRM_MODE_CONSTRAINTS_LAYOUT_IMPLICIT;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);

  entry = meta_kms_constraints_list_get_entry (list, 0);
  description = meta_kms_constraints_list_entry_get_description (entry);
  decoded_formats = meta_kms_constraints_description_get_formats (description,
                                                                   &n_formats);
  g_assert_cmpuint (n_formats, ==, 1);
  g_assert_true (decoded_formats[0].implicit);
  g_assert_cmpuint (decoded_formats[0].modifier, ==, 0);
}

static void
meta_test_kms_constraints_decode_plane_limit_padding (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  blob.plane_limit.count_planes = 1;
  blob.plane_limit.plane_ids[1] = 0;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);

  g_clear_pointer (&list, meta_kms_constraints_list_unref);
  blob.plane_limit.plane_ids[1] = 9;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
meta_test_kms_constraints_decode_reject_malformed (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  list = meta_kms_constraints_decode (&blob, sizeof (blob) - 1, &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.list.entries_offset = G_MAXUINT32;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.list.entry_size += 8;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.entry.reserved[1] = 1;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.format.header.length = sizeof (blob.format) - 1;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.format.layout_flags = 2;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.format.storage_flags = 4;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.geometry.flags = 8;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.geometry.min_scale = 0;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.format.pitch_alignment = 3;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.property.type = G_MAXUINT32;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.property.applicability_flags = 2;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.plane_limit.count_planes = 3;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.plane_limit.plane_ids[1] = 7;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.entry.description_offset++;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.entry.description_offset = blob.list.entries_offset;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
meta_test_kms_constraints_decode_reject_required_extension (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  blob.extension.flags = DRM_MODE_CONSTRAINTS_RECORD_REQUIRED;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.entry.flags |= 2;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.description.version++;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  blob = create_constraints_blob ();
  blob.list.version = 2;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
}

static void
meta_test_kms_constraints_decode_skip_unsupported (void)
{
  TestTwoEntryConstraintsBlob blob = create_two_entry_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);
  g_assert_cmpuint (meta_kms_constraints_list_get_n_entries (list), ==, 1);
  g_assert_cmpuint (meta_kms_constraints_list_get_selected_id (list), ==, 5);
  g_assert_cmpuint (meta_kms_constraints_list_get_suggested_id (list), ==, 0);
  g_assert_nonnull (meta_kms_constraints_list_find_entry (list, 5));
  g_assert_null (meta_kms_constraints_list_find_entry (list, 9));

  g_clear_pointer (&list, meta_kms_constraints_list_unref);
  blob = create_two_entry_constraints_blob ();
  blob.payloads[1].extension.flags = 0;
  blob.entries[1].flags |= 2;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);
  g_assert_cmpuint (meta_kms_constraints_list_get_n_entries (list), ==, 1);
  g_assert_cmpuint (meta_kms_constraints_list_get_suggested_id (list), ==, 0);

  g_clear_pointer (&list, meta_kms_constraints_list_unref);
  blob.entries[1].id = blob.entries[0].id;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
assert_constraints_decode_result_is_coherent (const void *data,
                                              size_t      size)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  list = meta_kms_constraints_decode (data, size, &error);
  if (list)
    g_assert_no_error (error);
  else
    g_assert_nonnull (error);
}

static void
meta_test_kms_constraints_decode_mutations (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  size_t offset;

  for (offset = 0; offset < sizeof (blob); offset++)
    {
      unsigned int bit;

      for (bit = 0; bit < 8; bit++)
        {
          TestConstraintsBlob mutated = blob;
          uint8_t *bytes = (uint8_t *) &mutated;

          bytes[offset] ^= 1U << bit;
          assert_constraints_decode_result_is_coherent (&mutated,
                                                        sizeof (mutated));
        }
    }

  for (offset = 0; offset < sizeof (blob); offset++)
    assert_constraints_decode_result_is_coherent (&blob, offset);
}

static void
meta_test_kms_constraints_query (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  TestConstraintsQuery query = {
    .snapshot = &blob,
    .snapshot_size = sizeof (blob),
    .generation = blob.list.generation,
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  list = meta_kms_constraints_query (19,
                                     query_constraints,
                                     &query,
                                     &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);
  g_assert_cmpuint (query.calls, ==, 2);
  g_assert_cmpuint (meta_kms_constraints_list_get_generation (list),
                    ==,
                    blob.list.generation);
}

static void
meta_test_kms_constraints_query_retries_changes (void)
{
  TestConstraintsBlob initial = create_constraints_blob ();
  TestTwoEntryConstraintsBlob replacement = create_two_entry_constraints_blob ();
  TestConstraintsQuery query = {
    .snapshot = &initial,
    .snapshot_size = sizeof (initial),
    .generation = initial.list.generation,
    .replacement_snapshot = &replacement,
    .replacement_size = sizeof (replacement),
    .replacement_generation = 18,
    .replace_on_fetch = TRUE,
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  replacement.list.generation = query.replacement_generation;
  list = meta_kms_constraints_query (19,
                                     query_constraints,
                                     &query,
                                     &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);
  g_assert_cmpuint (query.calls, ==, 4);
  g_assert_cmpuint (meta_kms_constraints_list_get_generation (list), ==, 18);
}

static void
meta_test_kms_constraints_query_rejects_bad_transport (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  TestConstraintsQuery query = {
    .snapshot = &blob,
    .snapshot_size = sizeof (blob),
    .generation = blob.list.generation,
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  query.snapshot_size = DRM_MODE_CONSTRAINTS_MAX_BYTES + 1U;
  list = meta_kms_constraints_query (19,
                                     query_constraints,
                                     &query,
                                     &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  query.snapshot_size = sizeof (blob);
  query.always_stale = TRUE;
  query.calls = 0;
  list = meta_kms_constraints_query (19,
                                     query_constraints,
                                     &query,
                                     &error);
  g_assert_null (list);
  g_assert_error (error,
                  G_IO_ERROR,
                  g_io_error_from_errno (ESTALE));
  g_assert_cmpuint (query.calls, ==, 1);

  g_clear_error (&error);
  query.always_stale = FALSE;
  query.result = -EACCES;
  list = meta_kms_constraints_query (19,
                                     query_constraints,
                                     &query,
                                     &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);

  g_clear_error (&error);
  query.result = 0;
  query.generation++;
  list = meta_kms_constraints_query (19,
                                     query_constraints,
                                     &query,
                                     &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void
meta_test_kms_constraints_query_ioctl (void)
{
  TestConstraintsBlob blob = create_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;

  constraints_ioctl = (TestConstraintsIoctl) {
    .snapshot = &blob,
    .snapshot_size = sizeof (blob),
    .generation = blob.list.generation,
  };
  list = meta_kms_constraints_query_fd (41, 19, &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);
  g_assert_cmpuint (constraints_ioctl.calls, ==, 2);
  g_assert_cmpuint (meta_kms_constraints_list_get_generation (list), ==,
                    blob.list.generation);

  g_clear_pointer (&list, meta_kms_constraints_list_unref);
  constraints_ioctl = (TestConstraintsIoctl) {
    .error_number = EACCES,
  };
  list = meta_kms_constraints_query_fd (41, 19, &error);
  g_assert_null (list);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);

  g_clear_error (&error);
  constraints_ioctl = (TestConstraintsIoctl) {
    .malformed_result = TRUE,
  };
  errno = EACCES;
  list = meta_kms_constraints_query_fd (41, 19, &error);
  g_assert_null (list);
  g_assert_error (error,
                  G_IO_ERROR,
                  g_io_error_from_errno (EPROTO));
}

static void
meta_test_kms_constraints_target (void)
{
  TestTwoEntryConstraintsBlob blob = create_two_entry_constraints_blob ();
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsList) list = NULL;
  g_autoptr (MetaKmsConstraintsTarget) target = NULL;
  g_autoptr (GArray) candidate_array = NULL;
  g_autoptr (GArray) modifiers = NULL;
  const MetaKmsConstraintsDescription *description;
  const MetaKmsConstraintsSize *output;
  const uint64_t candidate_modifiers[] = {
    DRM_FORMAT_MOD_LINEAR,
    TEST_FORMAT_MODIFIER,
    UINT64_C (2),
  };
  const uint32_t primary_plane[] = { 7 };
  const uint32_t primary_and_cursor_planes[] = { 7, 9 };
  const uint32_t unrelated_planes[] = { 11, 12 };
  const MetaFixed16Rectangle source = {
    .width = 1920U << 16,
    .height = 1080U << 16,
  };
  MtkRectangle destination = {
    .x = 20,
    .y = 30,
    .width = 1920,
    .height = 1080,
  };

  blob.payloads[1].extension.flags = 0;
  blob.payloads[1].property.applicability_flags =
    DRM_MODE_CONSTRAINTS_PROPERTY_PLANE_YUV;
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);

  target = meta_kms_constraints_target_new (
    list,
    meta_kms_constraints_list_get_suggested_id (list),
    &error);
  g_assert_no_error (error);
  g_assert_nonnull (target);
  g_assert_cmpuint (meta_kms_constraints_target_get_generation (target),
                    ==,
                    blob.list.generation);
  g_assert_cmpuint (meta_kms_constraints_target_get_id (target), ==, 9);

  g_clear_pointer (&list, meta_kms_constraints_list_unref);
  description = meta_kms_constraints_target_get_description (target);
  output = meta_kms_constraints_description_get_output (description);
  g_assert_cmpuint (output->max_width, ==,
                    blob.payloads[1].output.max_width);
  g_assert_true (meta_kms_constraints_target_allows_format (
                   target,
                   7,
                   DRM_FORMAT_XRGB8888,
                   META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                   1920,
                   1080));
  g_assert_false (meta_kms_constraints_target_allows_format (
                    target,
                    8,
                    DRM_FORMAT_XRGB8888,
                    META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                    1920,
                    1080));
  g_assert_true (meta_kms_constraints_target_allows_plane_geometry (
                   target, 7, 1920, 1080, source, destination));
  destination.width = 640;
  g_assert_false (meta_kms_constraints_target_allows_plane_geometry (
                    target, 7, 1920, 1080, source, destination));
  g_assert_false (meta_kms_constraints_target_allows_implicit_layout (
                    target,
                    7,
                    DRM_FORMAT_XRGB8888,
                    META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
                    1920,
                    1080));
  candidate_array = g_array_new (FALSE, FALSE, sizeof (uint64_t));
  g_array_append_vals (candidate_array,
                       candidate_modifiers,
                       G_N_ELEMENTS (candidate_modifiers));
  modifiers = meta_kms_constraints_target_filter_explicit_modifiers (
    target,
    7,
    DRM_FORMAT_XRGB8888,
    META_KMS_CONSTRAINTS_STORAGE_IMPORTED,
    1920,
    1080,
    candidate_array);
  g_assert_cmpuint (modifiers->len, ==, 1);
  g_assert_cmphex (g_array_index (modifiers, uint64_t, 0),
                   ==,
                   TEST_FORMAT_MODIFIER);
  g_assert_true (meta_kms_constraints_target_allows_property (target,
                                                              7,
                                                              8,
                                                              TRUE,
                                                              2));
  g_assert_false (meta_kms_constraints_target_allows_property (target,
                                                               7,
                                                               8,
                                                               TRUE,
                                                               5));
  g_assert_true (meta_kms_constraints_target_allows_property (target,
                                                              7,
                                                              8,
                                                              FALSE,
                                                              5));
  g_assert_true (meta_kms_constraints_target_allows_property (target,
                                                              7,
                                                              99,
                                                              FALSE,
                                                              G_MAXUINT64));
  g_assert_true (meta_kms_constraints_target_allows_active_planes (
                   target,
                   primary_plane,
                   G_N_ELEMENTS (primary_plane)));
  g_assert_false (meta_kms_constraints_target_allows_active_planes (
                    target,
                    primary_and_cursor_planes,
                    G_N_ELEMENTS (primary_and_cursor_planes)));
  g_assert_true (meta_kms_constraints_target_allows_active_planes (
                   target,
                   unrelated_planes,
                   G_N_ELEMENTS (unrelated_planes)));

  g_clear_pointer (&target, meta_kms_constraints_target_free);
  blob.entries[1].flags = 0;
  blob.list.suggested_id = 0;
  g_clear_pointer (&list, meta_kms_constraints_list_unref);
  list = meta_kms_constraints_decode (&blob, sizeof (blob), &error);
  g_assert_no_error (error);
  target = meta_kms_constraints_target_new (list, 9, &error);
  g_assert_null (target);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);

  g_clear_error (&error);
  target = meta_kms_constraints_target_new (
    list,
    meta_kms_constraints_list_get_selected_id (list),
    &error);
  g_assert_no_error (error);
  g_assert_nonnull (target);
  g_assert_cmpuint (meta_kms_constraints_target_get_id (target), ==, 5);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/backends/native/kms/constraints/sizes",
                   meta_test_kms_constraints_sizes);
  g_test_add_func ("/backends/native/kms/constraints/plane-geometry",
                   meta_test_kms_constraints_plane_geometry);
  g_test_add_func ("/backends/native/kms/constraints/event",
                   meta_test_kms_constraints_event);
  g_test_add_func ("/backends/native/kms/constraints/formats",
                   meta_test_kms_constraints_formats);
  g_test_add_func ("/backends/native/kms/constraints/allocation-views",
                   meta_test_kms_constraints_allocation_views);
  g_test_add_func ("/backends/native/kms/constraints/properties",
                   meta_test_kms_constraints_properties);
  g_test_add_func ("/backends/native/kms/constraints/owns-description",
                   meta_test_kms_constraints_owns_description);
  g_test_add_func ("/backends/native/kms/constraints/reject-invalid",
                   meta_test_kms_constraints_reject_invalid);
  g_test_add_func ("/backends/native/kms/constraints/list",
                   meta_test_kms_constraints_list);
  g_test_add_func ("/backends/native/kms/constraints/list-reject-invalid",
                   meta_test_kms_constraints_list_reject_invalid);
  g_test_add_func ("/backends/native/kms/constraints/decode",
                   meta_test_kms_constraints_decode);
  g_test_add_func ("/backends/native/kms/constraints/decode-implicit",
                   meta_test_kms_constraints_decode_implicit);
  g_test_add_func ("/backends/native/kms/constraints/decode-plane-limit-padding",
                   meta_test_kms_constraints_decode_plane_limit_padding);
  g_test_add_func ("/backends/native/kms/constraints/decode-reject-malformed",
                   meta_test_kms_constraints_decode_reject_malformed);
  g_test_add_func (
    "/backends/native/kms/constraints/decode-reject-required-extension",
    meta_test_kms_constraints_decode_reject_required_extension);
  g_test_add_func (
    "/backends/native/kms/constraints/decode-skip-unsupported",
    meta_test_kms_constraints_decode_skip_unsupported);
  g_test_add_func ("/backends/native/kms/constraints/decode-mutations",
                   meta_test_kms_constraints_decode_mutations);
  g_test_add_func ("/backends/native/kms/constraints/query",
                   meta_test_kms_constraints_query);
  g_test_add_func ("/backends/native/kms/constraints/query-retries-changes",
                   meta_test_kms_constraints_query_retries_changes);
  g_test_add_func ("/backends/native/kms/constraints/query-rejects-transport",
                   meta_test_kms_constraints_query_rejects_bad_transport);
  g_test_add_func ("/backends/native/kms/constraints/query-ioctl",
                   meta_test_kms_constraints_query_ioctl);
  g_test_add_func ("/backends/native/kms/constraints/target",
                   meta_test_kms_constraints_target);
  return g_test_run ();
}
