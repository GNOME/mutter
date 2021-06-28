/*
 * Copyright (C) 2021 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-mode.h"
#include "backends/native/meta-kms-update-private.h"
#include "backends/native/meta-kms.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-kms-test-utils.h"

static MetaContext *test_context;

static void
meta_test_kms_update_sanity (void)
{
  MetaKmsDevice *device;
  MetaKmsCrtc *crtc;
  MetaKmsUpdate *update;

  device = meta_get_test_kms_device (test_context);
  crtc = meta_get_test_kms_crtc (device);

  update = meta_kms_update_new (device);
  g_assert (meta_kms_update_get_device (update) == device);
  g_assert_false (meta_kms_update_is_locked (update));
  g_assert_null (meta_kms_update_get_primary_plane_assignment (update, crtc));
  g_assert_null (meta_kms_update_get_plane_assignments (update));
  g_assert_null (meta_kms_update_get_mode_sets (update));
  g_assert_null (meta_kms_update_get_page_flip_listeners (update));
  g_assert_null (meta_kms_update_get_connector_updates (update));
  g_assert_null (meta_kms_update_get_crtc_gammas (update));
  meta_kms_update_free (update);
}

static void
meta_test_kms_update_plane_assignments (void)
{
  MetaKmsDevice *device;
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsConnector *connector;
  MetaKmsPlane *primary_plane;
  MetaKmsPlane *cursor_plane;
  MetaKmsMode *mode;
  int mode_width, mode_height;
  g_autoptr (MetaDrmBuffer) primary_buffer = NULL;
  g_autoptr (MetaDrmBuffer) cursor_buffer = NULL;
  MetaKmsPlaneAssignment *primary_plane_assignment;
  MetaKmsPlaneAssignment *cursor_plane_assignment;
  GList *plane_assignments;

  device = meta_get_test_kms_device (test_context);
  update = meta_kms_update_new (device);
  crtc = meta_get_test_kms_crtc (device);
  connector = meta_get_test_kms_connector (device);

  primary_plane = meta_kms_device_get_primary_plane_for (device, crtc);
  g_assert_nonnull (primary_plane);

  cursor_plane = meta_kms_device_get_cursor_plane_for (device, crtc);
  g_assert_nonnull (cursor_plane);

  mode = meta_kms_connector_get_preferred_mode (connector);

  mode_width = meta_kms_mode_get_width (mode);
  mode_height = meta_kms_mode_get_height (mode);
  primary_buffer = meta_create_test_mode_dumb_buffer (device, mode);

  primary_plane_assignment =
    meta_kms_update_assign_plane (update,
                                  crtc,
                                  primary_plane,
                                  primary_buffer,
                                  meta_get_mode_fixed_rect_16 (mode),
                                  meta_get_mode_rect (mode),
                                  META_KMS_ASSIGN_PLANE_FLAG_NONE);
  g_assert_nonnull (primary_plane_assignment);
  g_assert_cmpint (primary_plane_assignment->src_rect.x, ==, 0);
  g_assert_cmpint (primary_plane_assignment->src_rect.y, ==, 0);
  g_assert_cmpint (primary_plane_assignment->src_rect.width,
                   ==,
                   meta_fixed_16_from_int (mode_width));
  g_assert_cmpint (primary_plane_assignment->src_rect.height,
                   ==,
                   meta_fixed_16_from_int (mode_height));
  g_assert_cmpint (primary_plane_assignment->dst_rect.x, ==, 0);
  g_assert_cmpint (primary_plane_assignment->dst_rect.y, ==, 0);
  g_assert_cmpint (primary_plane_assignment->dst_rect.width, ==, mode_width);
  g_assert_cmpint (primary_plane_assignment->dst_rect.height, ==, mode_height);

  cursor_buffer = meta_create_test_dumb_buffer (device, 64, 64);

  cursor_plane_assignment =
    meta_kms_update_assign_plane (update,
                                  crtc,
                                  cursor_plane,
                                  cursor_buffer,
                                  META_FIXED_16_RECTANGLE_INIT_INT (0, 0, 64, 64),
                                  META_RECTANGLE_INIT (24, 48, 64, 64),
                                  META_KMS_ASSIGN_PLANE_FLAG_NONE);
  g_assert_nonnull (cursor_plane_assignment);
  g_assert_cmpint (cursor_plane_assignment->src_rect.x, ==, 0);
  g_assert_cmpint (cursor_plane_assignment->src_rect.y, ==, 0);
  g_assert_cmpint (cursor_plane_assignment->src_rect.width,
                   ==,
                   meta_fixed_16_from_int (64));
  g_assert_cmpint (cursor_plane_assignment->src_rect.height,
                   ==,
                   meta_fixed_16_from_int (64));
  g_assert_cmpint (cursor_plane_assignment->dst_rect.x, ==, 24);
  g_assert_cmpint (cursor_plane_assignment->dst_rect.y, ==, 48);
  g_assert_cmpint (cursor_plane_assignment->dst_rect.width, ==, 64);
  g_assert_cmpint (cursor_plane_assignment->dst_rect.height, ==, 64);

  meta_kms_plane_assignment_set_cursor_hotspot (cursor_plane_assignment,
                                                10, 11);

  g_assert (meta_kms_update_get_primary_plane_assignment (update, crtc) ==
            primary_plane_assignment);

  g_assert (primary_plane_assignment->crtc == crtc);
  g_assert (primary_plane_assignment->update == update);
  g_assert (primary_plane_assignment->plane == primary_plane);
  g_assert (primary_plane_assignment->buffer == primary_buffer);
  g_assert_cmpuint (primary_plane_assignment->rotation, ==, 0);
  g_assert_false (primary_plane_assignment->cursor_hotspot.is_valid);

  g_assert (meta_kms_update_get_cursor_plane_assignment (update, crtc) ==
            cursor_plane_assignment);

  g_assert (cursor_plane_assignment->crtc == crtc);
  g_assert (cursor_plane_assignment->update == update);
  g_assert (cursor_plane_assignment->plane == cursor_plane);
  g_assert (cursor_plane_assignment->buffer == cursor_buffer);
  g_assert_cmpuint (cursor_plane_assignment->rotation, ==, 0);
  g_assert_true (cursor_plane_assignment->cursor_hotspot.is_valid);
  g_assert_cmpint (cursor_plane_assignment->cursor_hotspot.x, ==, 10);
  g_assert_cmpint (cursor_plane_assignment->cursor_hotspot.y, ==, 11);

  plane_assignments = meta_kms_update_get_plane_assignments (update);
  g_assert_cmpuint (g_list_length (plane_assignments), ==, 2);

  g_assert_nonnull (g_list_find (plane_assignments, primary_plane_assignment));
  g_assert_nonnull (g_list_find (plane_assignments, cursor_plane_assignment));

  meta_kms_update_free (update);
}

static void
meta_test_kms_update_fixed16 (void)
{
  MetaFixed16Rectangle rect16;

  g_assert_cmpint (meta_fixed_16_from_int (12345), ==, 809041920);
  g_assert_cmpint (meta_fixed_16_to_int (809041920), ==, 12345);
  g_assert_cmpint (meta_fixed_16_from_int (-12345), ==, -809041920);
  g_assert_cmpint (meta_fixed_16_to_int (-809041920), ==, -12345);

  rect16 = META_FIXED_16_RECTANGLE_INIT_INT (100, 200, 300, 400);
  g_assert_cmpint (rect16.x, ==, 6553600);
  g_assert_cmpint (rect16.y, ==, 13107200);
  g_assert_cmpint (rect16.width, ==, 19660800);
  g_assert_cmpint (rect16.height, ==, 26214400);
}

static void
meta_test_kms_update_mode_sets (void)
{
  MetaKmsDevice *device;
  MetaKmsUpdate *update;
  MetaKmsCrtc *crtc;
  MetaKmsConnector *connector;
  MetaKmsMode *mode;
  GList *mode_sets;
  MetaKmsModeSet *mode_set;

  device = meta_get_test_kms_device (test_context);
  update = meta_kms_update_new (device);
  crtc = meta_get_test_kms_crtc (device);
  connector = meta_get_test_kms_connector (device);
  mode = meta_kms_connector_get_preferred_mode (connector);

  meta_kms_update_mode_set (update, crtc,
                            g_list_append (NULL, connector),
                            mode);

  mode_sets = meta_kms_update_get_mode_sets (update);
  g_assert_cmpuint (g_list_length (mode_sets), ==, 1);
  mode_set = mode_sets->data;

  g_assert (mode_set->crtc == crtc);
  g_assert_cmpuint (g_list_length (mode_set->connectors), ==, 1);
  g_assert (mode_set->connectors->data == connector);
  g_assert (mode_set->mode == mode);

  meta_kms_update_free (update);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/update/sanity",
                   meta_test_kms_update_sanity);
  g_test_add_func ("/backends/native/kms/update/fixed16",
                   meta_test_kms_update_fixed16);
  g_test_add_func ("/backends/native/kms/update/plane-assignments",
                   meta_test_kms_update_plane_assignments);
  g_test_add_func ("/backends/native/kms/update/mode-sets",
                   meta_test_kms_update_mode_sets);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                              META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
