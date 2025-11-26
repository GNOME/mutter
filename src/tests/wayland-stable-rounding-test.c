/*
 * Copyright (C) 2025 Red Hat, Inc.
 * Copyright (C) 2023 Collabora, Ltd.
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
#include "compositor/meta-window-actor-private.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-ref-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-window-wayland.h"

static MetaContext *test_context;
static MetaVirtualMonitor *virtual_monitor;
static MetaWaylandTestClient *wayland_test_client;
static MetaWaylandTestDriver *test_driver;
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

#define assert_wayland_buffer_size(window, width, height) \
{ \
  g_assert_cmpint (meta_wayland_surface_get_buffer_width (meta_window_get_wayland_surface (window)), \
                   ==, \
                   width); \
  g_assert_cmpint (meta_wayland_surface_get_buffer_height (meta_window_get_wayland_surface (window)), \
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
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static void
stable_rounding (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle layout;

  wait_for_sync_point (1);
  assert_wayland_surface_size (test_window, 960, 720);
  assert_wayland_buffer_size (test_window, 1, 1);

  meta_set_custom_monitor_config_full (backend,
                                       "stable-rounding.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
  meta_monitor_manager_reload (monitor_manager);
  logical_monitor =
    meta_monitor_manager_get_logical_monitors (monitor_manager)->data;
  layout = meta_logical_monitor_get_layout (logical_monitor);
  g_assert_cmpint (layout.x, ==, 0);
  g_assert_cmpint (layout.y, ==, 0);
  g_assert_cmpint (layout.width, ==, 640);
  g_assert_cmpint (layout.height, ==, 480);

  wait_for_sync_point (2);
  assert_wayland_surface_size (test_window, 638, 480);
  assert_wayland_buffer_size (test_window, 1, 1);
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
stable_rounding_ref_test (void)
{
  MetaWindowWayland *wl_window = META_WINDOW_WAYLAND (test_window);
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  meta_set_custom_monitor_config_full (backend,
                                       "stable-rounding.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
  meta_monitor_manager_reload (monitor_manager);

  wait_for_window_added (test_window);
  g_assert_true (meta_window_wayland_is_acked_fullscreen (wl_window));
  meta_ref_test_verify_view (get_view (),
                             g_test_get_path (), 1,
                             meta_ref_test_determine_ref_test_flag ());
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);

  test_driver = meta_wayland_test_driver_new (compositor);

  virtual_monitor = meta_create_test_monitor (test_context,
                                              960, 720, 60.0);

  wayland_test_client = meta_wayland_test_client_new (test_context,
                                                      "stable-rounding");

  while (!test_window)
    {
      g_main_context_iteration (NULL, TRUE);
      test_window =
        meta_find_window_from_title (test_context, "stable-rounding");
    }
}

static void
on_after_tests (void)
{
  meta_window_delete (test_window, g_get_monotonic_time ());

  meta_wayland_test_client_finish (wayland_test_client);

  g_clear_object (&virtual_monitor);

  g_clear_object (&test_driver);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/stable-rounding",
                   stable_rounding);
  g_test_add_func ("/wayland/stable-rounding-ref-test",
                   stable_rounding_ref_test);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
