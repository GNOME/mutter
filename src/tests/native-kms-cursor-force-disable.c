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

#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-udev.h"
#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-crtc-virtual.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-backend.h"
#include "meta/meta-cursor-tracker.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-cursor-sprite-wayland.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"

static MetaContext *test_context;

static void
meta_test_cursor_force_disable (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorRenderer *cursor_renderer = meta_backend_get_cursor_renderer (backend);
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  g_autoptr (MetaWaylandTestDriver) test_driver = NULL;
  MetaCursorSprite *cursor_sprite;
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  MetaVirtualMonitor *virtual_monitor;
  ClutterSeat *seat;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  GError *error = NULL;

  test_driver = meta_wayland_test_driver_new (wayland_compositor);

  seat = meta_backend_get_default_seat (backend);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  monitor_info = meta_virtual_monitor_info_new (100, 100, 60.0,
                                                "MetaTestVendor",
                                                "MetaVirtualMonitor",
                                                "0x1234");
  virtual_monitor = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                 monitor_info,
                                                                 &error);
  if (!virtual_monitor)
    g_error ("Failed to create virtual monitor: %s", error->message);
  meta_monitor_manager_reload (monitor_manager);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       50, 50);

  cursor_renderer = meta_backend_get_cursor_renderer (backend);

  while (TRUE)
    {
      cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
      if (cursor_sprite)
        break;
      g_main_context_iteration (NULL, TRUE);
    }
  g_assert_nonnull (cursor_sprite);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/cursor-force-disable",
                   meta_test_cursor_force_disable);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  g_setenv ("MUTTER_DEBUG_DISABLE_HW_CURSORS", "1", TRUE);

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
