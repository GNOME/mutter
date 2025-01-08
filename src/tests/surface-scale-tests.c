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

#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-test/meta-context-test.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"

static MetaContext *test_context;

static MonitorTestCaseSetup test_case_base_setup = {
  .modes = {
    {
      .width = 1920,
      .height = 1080,
      .refresh_rate = 60.0
    },
  },
  .n_modes = 1,
  .outputs = {
    {
      .crtc = 0,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 0 },
      .n_possible_crtcs = 1,
      .width_mm = 150,
      .height_mm = 85,
    },
  },
  .n_outputs = 1,
  .crtcs = {
    {
      .current_mode = -1
    },
  },
  .n_crtcs = 1
};

static void
bump_output_serial (const char **serial)
{
  static int output_serial_counter = 0x1230000;

  g_clear_pointer ((gpointer *) serial, g_free);
  *serial = g_strdup_printf ("0x%x", output_serial_counter++);
}

static void
meta_test_wayland_surface_scales (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  g_autoptr (MetaWaylandTestDriver) test_driver = NULL;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MonitorTestCaseSetup test_case_setup = test_case_base_setup;
  MetaMonitorTestSetup *test_setup;
  float scale;

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  test_driver = meta_wayland_test_driver_new (compositor);
  meta_wayland_test_driver_set_property_int (test_driver,
                                             "cursor-theme-size",
                                             meta_prefs_get_cursor_size ());


  g_debug ("Testing with scale 2.0, then launching client");
  scale = 2.0f;
  test_case_setup.outputs[0].scale = scale;
  bump_output_serial (&test_case_setup.outputs[0].serial);
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  wayland_test_client = meta_wayland_test_client_new (test_context,
                                                      "surface-scale-client");
  meta_wait_for_window_cursor (test_context);
  meta_wayland_test_driver_emit_sync_event (test_driver,
                                            (uint32_t) (scale * 120.0f));
  meta_wayland_test_driver_wait_for_sync_point (test_driver, 0);

  g_debug ("Testing with scale 2.5 with existing client");
  scale = 2.5f;
  test_case_setup.outputs[0].scale = scale;
  bump_output_serial (&test_case_setup.outputs[0].serial);
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  meta_wayland_test_driver_emit_sync_event (test_driver,
                                            (uint32_t) (scale * 120.0f));
  meta_wayland_test_driver_wait_for_sync_point (test_driver, 0);

  g_debug ("Terminating client");
  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  g_clear_pointer ((gpointer *) &test_case_setup.outputs[0].serial, g_free);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/surface/surface-scales",
                   meta_test_wayland_surface_scales);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
