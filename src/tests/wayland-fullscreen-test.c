/*
 * Copyright (C) 2022 Red Hat, Inc.
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

#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "backends/native/meta-renderer-native.h"
#include "tests/meta-ref-test.h"
#include "wayland/meta-window-wayland.h"
#include "wayland/meta-wayland-surface-private.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;
static MetaWaylandTestClient *wayland_test_client;
static MetaWindow *test_window = NULL;

#define assert_wayland_surface_size(window, width, height) \
{ \
  g_assert_cmpint (meta_wayland_surface_get_width (meta_window_get_wayland_surface (window)), \
                   ==, \
                   width); \
  g_assert_cmpint (meta_wayland_surface_get_height (meta_window_get_wayland_surface (window)), \
                   ==, \
                   height); \
}

static ClutterStageView *
get_view (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return CLUTTER_STAGE_VIEW (meta_renderer_get_views (renderer)->data);
}

static void
on_first_frame (MetaWindowActor *window_actor,
                gboolean        *done)
{
  *done = TRUE;
}

static void
wait_for_first_frame (MetaWindow *window)
{
  MetaWindowActor *window_actor;
  gboolean done = FALSE;
  glong handler_id;

  window_actor = meta_window_actor_from_window (window);
  handler_id = g_signal_connect (window_actor, "first-frame",
                                 G_CALLBACK (on_first_frame), &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (window_actor, handler_id);
}

static void
on_effects_completed (MetaWindowActor *window_actor,
                      gboolean        *done)
{
  *done = TRUE;
}

static void
wait_for_window_added (MetaWindow *window)
{
  MetaWindowActor *window_actor;
  gboolean done = FALSE;
  gulong handler_id;

  window_actor = meta_window_actor_from_window (window);
  handler_id = g_signal_connect (window_actor, "effects-completed",
                                 G_CALLBACK (on_effects_completed), &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (window_actor, handler_id);
}

static void
toplevel_fullscreen (void)
{
  MtkRectangle rect;

  wait_for_first_frame (test_window);

  meta_window_get_frame_rect (test_window, &rect);
  g_assert_cmpint (rect.width, ==, 640);
  g_assert_cmpint (rect.height, ==, 480);
  g_assert_cmpint (rect.x, ==, 0);
  g_assert_cmpint (rect.y, ==, 0);
  assert_wayland_surface_size (test_window, 10, 10);
}

static void
toplevel_fullscreen_ref_test (void)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (test_window);
  MtkRectangle rect;

  wait_for_window_added (test_window);
  assert_wayland_surface_size (test_window, 10, 10);
  g_assert_true (meta_window_wayland_is_acked_fullscreen (wl_window));

  meta_ref_test_verify_view (get_view (),
                             g_test_get_path (), 1,
                             meta_ref_test_determine_ref_test_flag ());

  meta_window_unmake_fullscreen (test_window);

  while (meta_window_wayland_is_acked_fullscreen (wl_window))
    g_main_context_iteration (NULL, FALSE);

  meta_window_move_frame (test_window, FALSE, 12, 13);

  meta_window_get_frame_rect (test_window, &rect);
  g_assert_cmpint (rect.width, ==, 10);
  g_assert_cmpint (rect.height, ==, 10);
  g_assert_cmpint (rect.x, ==, 12);
  g_assert_cmpint (rect.y, ==, 13);
  assert_wayland_surface_size (test_window, 10, 10);

  meta_ref_test_verify_view (get_view (),
                             g_test_get_path (), 2,
                             meta_ref_test_determine_ref_test_flag ());

  meta_window_make_fullscreen (test_window);
  while (!meta_window_wayland_is_acked_fullscreen (wl_window))
    g_main_context_iteration (NULL, FALSE);

  meta_window_get_frame_rect (test_window, &rect);
  g_assert_cmpint (rect.width, ==, 640);
  g_assert_cmpint (rect.height, ==, 480);
  g_assert_cmpint (rect.x, ==, 0);
  g_assert_cmpint (rect.y, ==, 0);
  assert_wayland_surface_size (test_window, 10, 10);

  meta_ref_test_verify_view (get_view (),
                             g_test_get_path (), 3,
                             meta_ref_test_determine_ref_test_flag ());
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
#ifdef MUTTER_PRIVILEGED_TEST
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsDevice *kms_device = meta_kms_get_devices (kms)->data;
#endif

  test_driver = meta_wayland_test_driver_new (compositor);

#ifdef MUTTER_PRIVILEGED_TEST
  meta_wayland_test_driver_set_property (test_driver,
                                         "gpu-path",
                                         meta_kms_device_get_path (kms_device));

  meta_set_custom_monitor_config_full (backend,
                                       "vkms-640x480.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
#else
  virtual_monitor = meta_create_test_monitor (test_context,
                                              640, 480, 60.0);
#endif
  meta_monitor_manager_reload (monitor_manager);

  wayland_test_client = meta_wayland_test_client_new (test_context,
                                                      "fullscreen");

  while (!(test_window =
           meta_find_window_from_title (test_context, "fullscreen")))
    g_main_context_iteration (NULL, TRUE);
}

static void
on_after_tests (void)
{
  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  meta_wayland_test_client_finish (wayland_test_client);

  g_clear_object (&virtual_monitor);

  g_clear_object (&test_driver);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/toplevel/fullscreen",
                   toplevel_fullscreen);
  g_test_add_func ("/wayland/toplevel/fullscreen-ref-test",
                   toplevel_fullscreen_ref_test);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

#ifdef MUTTER_PRIVILEGED_TEST
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
#else
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
#endif
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
