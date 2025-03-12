/*
 * Copyright (C) 2025 Red Hat Inc.
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
 *
 */

#include "config.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-renderer.h"
#include "tests/meta-test/meta-context-test.h"
#include "tests/meta-test/meta-test-monitor.h"
#include "tests/meta-ref-test.h"
#include "tests/meta-test-utils.h"

static MetaContext *test_context;

static ClutterStageView *
get_view (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  GList *views;

  views = meta_renderer_get_views (renderer);
  g_assert_cmpuint (g_list_length (views), ==, 1);

  return CLUTTER_STAGE_VIEW (views->data);
}

static void
meta_test_cursor_overlay_damage (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (MetaVirtualMonitor) test_monitor = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  g_autoptr (GError) error = NULL;

  test_monitor = meta_create_test_monitor (test_context, 100, 100, 60.0);
  g_assert_nonnull (test_monitor);
  g_assert_no_error (error);

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       50.0f, 50.0f);

  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       50.0f, 50.0f);

  meta_flush_input (test_context);
  meta_wait_for_paint (test_context);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       75.0f, 75.0f);

  meta_flush_input (test_context);

  meta_ref_test_verify_view_undamaged (get_view (),
                                       g_test_get_path (), 0,
                                       meta_ref_test_determine_ref_test_flag ());
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/cursor-overlay/damage",
                   meta_test_cursor_overlay_damage);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));
  meta_context_test_set_background_color (META_CONTEXT_TEST (context),
                                          COGL_COLOR_INIT (255, 255, 255, 255));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
