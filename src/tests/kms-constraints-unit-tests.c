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

#include <drm_fourcc.h>
#include <gio/gio.h>
#include <xf86drmMode.h>

#include "backends/native/meta-kms-constraints-list.h"
#include "backends/native/meta-kms-constraints.h"

#define TEST_FORMAT_MODIFIER UINT64_C (1)

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
    .size = {
      .min_width = 1280,
      .min_height = 720,
      .max_width = 1280,
      .max_height = 720,
    },
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
                                                       properties,
                                                       n_properties,
                                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (description);
  return description;
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

  stored_formats =
    meta_kms_constraints_description_get_formats (description, &n_formats);
  g_assert_cmpuint (n_formats, ==, G_N_ELEMENTS (formats));
  g_assert_cmpuint (stored_formats[0].plane_id, ==, 7);

  g_assert_true (meta_kms_constraints_description_allows_explicit_layout (
                   description,
                   7,
                   DRM_FORMAT_XRGB8888,
                   DRM_FORMAT_MOD_LINEAR,
                   1920,
                   1080));
  g_assert_true (meta_kms_constraints_description_allows_explicit_layout (
                   description,
                   7,
                   DRM_FORMAT_XRGB8888,
                   TEST_FORMAT_MODIFIER,
                   3840,
                   2160));
  g_assert_false (meta_kms_constraints_description_allows_explicit_layout (
                    description,
                    8,
                    DRM_FORMAT_XRGB8888,
                    DRM_FORMAT_MOD_LINEAR,
                    1920,
                    1080));
  g_assert_false (meta_kms_constraints_description_allows_explicit_layout (
                    description,
                    7,
                    DRM_FORMAT_XRGB8888,
                    DRM_FORMAT_MOD_LINEAR,
                    1921,
                    1080));
  g_assert_true (meta_kms_constraints_description_allows_implicit_layout (
                   description,
                   7,
                   DRM_FORMAT_XRGB8888,
                   1280,
                   720));
  g_assert_false (meta_kms_constraints_description_allows_implicit_layout (
                    description,
                    7,
                    DRM_FORMAT_XRGB8888,
                    1920,
                    1080));
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
                                                                 3840,
                                                                 2160);
  g_assert_cmpuint (drm_formats->len, ==, 1);

  g_clear_pointer (&modifiers, g_array_unref);
  modifiers =
    meta_kms_constraints_description_copy_explicit_modifiers_for_format (
      description,
      7,
      DRM_FORMAT_XRGB8888,
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
  MetaKmsConstraintsProperty source_property = {
    .object_id = 7,
    .property_id = 11,
    .type = DRM_MODE_PROP_RANGE,
    .maximum = 1,
  };
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaKmsConstraintsDescription) description = NULL;
  const MetaKmsConstraintsSize *stored_output;
  const MetaKmsConstraintsFormat *stored_format;
  const MetaKmsConstraintsProperty *stored_property;
  size_t count;

  description = meta_kms_constraints_description_new (&source_output,
                                                       &source_format,
                                                       1,
                                                       &source_property,
                                                       1,
                                                       &error);
  g_assert_no_error (error);
  g_assert_nonnull (description);

  source_output.max_width = 1;
  source_format.plane_id = 1;
  source_property.object_id = 1;

  stored_output = meta_kms_constraints_description_get_output (description);
  stored_format =
    meta_kms_constraints_description_get_formats (description, &count);
  g_assert_cmpuint (count, ==, 1);
  stored_property =
    meta_kms_constraints_description_get_properties (description, &count);
  g_assert_cmpuint (count, ==, 1);

  g_assert_cmpuint (stored_output->max_width, ==, output_size.max_width);
  g_assert_cmpuint (stored_format->plane_id, ==, formats[0].plane_id);
  g_assert_cmpuint (stored_property->object_id, ==, 7);
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

  invalid_output.min_width = 0;
  description = meta_kms_constraints_description_new (
    &invalid_output,
    formats,
    G_N_ELEMENTS (formats),
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
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    formats,
    G_N_ELEMENTS (formats),
    &invalid_property,
    1,
    &error);
  g_assert_null (description);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);

  g_clear_error (&error);
  description = meta_kms_constraints_description_new (
    &output_size,
    formats,
    G_N_ELEMENTS (formats),
    duplicate_properties,
    G_N_ELEMENTS (duplicate_properties),
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
  const MetaKmsConstraintsListEntry *stored_entry;

  list = meta_kms_constraints_list_new (17,
                                        5,
                                        9,
                                        entries,
                                        G_N_ELEMENTS (entries),
                                        &error);
  g_assert_no_error (error);
  g_assert_nonnull (list);

  g_clear_pointer (&first, meta_kms_constraints_description_free);
  g_clear_pointer (&second, meta_kms_constraints_description_free);

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

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/backends/native/kms/constraints/sizes",
                   meta_test_kms_constraints_sizes);
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
  return g_test_run ();
}
