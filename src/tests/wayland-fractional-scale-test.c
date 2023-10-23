/*
 * Copyright (C) 2022 Red Hat, Inc.
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
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland-surface-private.h"

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

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static void
fractional_scale (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle layout;

  wait_for_sync_point (0);
  assert_wayland_surface_size (test_window, 1920, 1080);
  assert_wayland_buffer_size (test_window, 1920, 1080);

  meta_set_custom_monitor_config_full (backend,
                                       "full-hd-fractional-scale-1.25.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
  meta_monitor_manager_reload (monitor_manager);
  logical_monitor =
    meta_monitor_manager_get_logical_monitors (monitor_manager)->data;
  layout = meta_logical_monitor_get_layout (logical_monitor);
  g_assert_cmpint (layout.x, ==, 0);
  g_assert_cmpint (layout.y, ==, 0);
  g_assert_cmpint (layout.width, ==, 1536);
  g_assert_cmpint (layout.height, ==, 864);

  wait_for_sync_point (1);
  assert_wayland_surface_size (test_window, 1536, 864);
  assert_wayland_buffer_size (test_window, 1920, 1080);

  meta_set_custom_monitor_config_full (backend,
                                       "full-hd-fractional-scale-1.5.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
  meta_monitor_manager_reload (monitor_manager);
  logical_monitor =
    meta_monitor_manager_get_logical_monitors (monitor_manager)->data;
  layout = meta_logical_monitor_get_layout (logical_monitor);
  g_assert_cmpint (layout.x, ==, 0);
  g_assert_cmpint (layout.y, ==, 0);
  g_assert_cmpint (layout.width, ==, 1280);
  g_assert_cmpint (layout.height, ==, 720);

  wait_for_sync_point (2);
  assert_wayland_surface_size (test_window, 1280, 720);
  assert_wayland_buffer_size (test_window, 1920, 1080);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);

  test_driver = meta_wayland_test_driver_new (compositor);

  virtual_monitor = meta_create_test_monitor (test_context,
                                              1920, 1080, 60.0);

  wayland_test_client = meta_wayland_test_client_new (test_context,
                                                      "fractional-scale");

  while (!test_window)
    {
      g_main_context_iteration (NULL, TRUE);
      test_window =
        meta_find_window_from_title (test_context, "fractional-scale");
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
  g_test_add_func ("/wayland/fractional-scale",
                   fractional_scale);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
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
