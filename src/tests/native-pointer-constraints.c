/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "meta/meta-cursor-tracker.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-virtual-monitor.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-backend.h"
#include "tests/meta-test-utils.h"

static MetaContext *test_context;

static void
run_test_client_command (MetaTestClient *client, ...)
{
  va_list vap;
  GError *error = NULL;
  gboolean retval;

  va_start (vap, client);
  retval = meta_test_client_dov (client, &error, vap);
  va_end (vap);

  if (!retval)
    g_error ("Failed to execute client command: %s", error->message);
}

static void
meta_test_warp_on_hotplug (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  g_autoptr (MetaVirtualMonitorInfo) monitor_info1 = NULL;
  g_autoptr (MetaVirtualMonitorInfo) monitor_info2 = NULL;
  MetaVirtualMonitor *virtual_monitor1;
  MetaVirtualMonitor *virtual_monitor2;
  ClutterSeat *seat;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  MetaTestClient *test_client;
  GError *error = NULL;
  graphene_point_t coords;

  seat = meta_backend_get_default_seat (backend);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  meta_set_custom_monitor_config_full (backend, "pointer-constraint.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);

  monitor_info1 = meta_virtual_monitor_info_new (100, 100, 60.0,
                                                 "MetaTestVendor",
                                                 "MetaVirtualMonitor",
                                                 "0x1234");
  virtual_monitor1 = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                  monitor_info1,
                                                                  &error);
  if (!virtual_monitor1)
    g_error ("Failed to create virtual monitor: %s", error->message);
  meta_monitor_manager_reload (monitor_manager);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       50, 50);
  meta_wait_for_paint (test_context);

  meta_cursor_tracker_get_pointer (meta_backend_get_cursor_tracker (backend),
                                   &coords, NULL);
  g_assert_nonnull (meta_backend_get_current_logical_monitor (backend));

  monitor_info2 = meta_virtual_monitor_info_new (200, 200, 60.0,
                                                 "MetaTestVendor",
                                                 "MetaVirtualMonitor",
                                                 "0x1235");
  virtual_monitor2 = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                  monitor_info2,
                                                                  &error);
  if (!virtual_monitor2)
    g_error ("Failed to create virtual monitor: %s", error->message);
  meta_monitor_manager_reload (monitor_manager);

  test_client = meta_test_client_new (test_context,
                                      "test-client",
                                      META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  run_test_client_command (test_client, "create", "1", NULL);
  run_test_client_command (test_client, "show", "1", NULL);
  run_test_client_command (test_client, "sync", NULL);

  meta_wait_for_paint (test_context);

  meta_cursor_tracker_get_pointer (meta_backend_get_cursor_tracker (backend),
                                   &coords, NULL);

  g_assert_nonnull (meta_backend_get_current_logical_monitor (backend));

  meta_test_client_destroy (test_client);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/pointer-constraints/warp-on-hotplug",
                   meta_test_warp_on_hotplug);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
