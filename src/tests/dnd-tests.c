/*
 * Copyright (C) 2026 Red Hat, Inc.
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

#include "backends/meta-backend-private.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-runner.h"
#include "tests/meta-wayland-test-utils.h"

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static void
emit_sync_event (unsigned int sync_point)
{
  meta_wayland_test_driver_emit_sync_event (test_driver, sync_point);
}

static void
click_on_client_window (const char *test_client,
                        const char *window_title)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window = NULL;
  MtkRectangle rect;

  meta_backend_inhibit_hw_cursor (backend);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  wayland_test_client = meta_wayland_test_client_new (test_context,
                                                      test_client);

  while (!window)
    {
      g_main_context_iteration (NULL, TRUE);
      window = meta_find_window_from_title (test_context, window_title);
    }
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (window);

  meta_window_get_frame_rect (window, &rect);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       CLUTTER_CURRENT_TIME,
                                                       rect.x + 10,
                                                       rect.y + 10);
  clutter_virtual_input_device_notify_button (virtual_pointer,
                                              CLUTTER_CURRENT_TIME,
                                              CLUTTER_BUTTON_PRIMARY,
                                              CLUTTER_BUTTON_STATE_PRESSED);

  meta_wayland_test_client_finish (wayland_test_client);
  meta_backend_uninhibit_hw_cursor (backend);
}

static void
data_device_dnd_feedback_request_order (void)
{
  click_on_client_window ("dnd-order", "dnd");
}

static void
data_device_dnd_feedback_reuse_icon (void)
{
  click_on_client_window ("dnd-reuse-icon", "dnd");
}

static void
data_device_dnd_touch_same_surface (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_touch = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle rect;
  int64_t time_us;

  virtual_touch = clutter_seat_create_virtual_device (seat,
                                                      CLUTTER_TOUCHSCREEN_DEVICE);

  wayland_test_client = meta_wayland_test_client_new (test_context,
                                                      "touch-dnd-same-surface");
  wait_for_sync_point (0);

  window = meta_wait_for_client_window (test_context, "touch-dnd");
  g_assert_nonnull (window);
  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (window);

  meta_window_get_frame_rect (window, &rect);

  time_us = g_get_monotonic_time ();
  clutter_virtual_input_device_notify_touch_down (virtual_touch,
                                                  time_us,
                                                  0,
                                                  rect.x + 10,
                                                  rect.y + 10);
  meta_flush_input (test_context);

  wait_for_sync_point (1);

  time_us = g_get_monotonic_time ();
  clutter_virtual_input_device_notify_touch_motion (virtual_touch,
                                                    time_us,
                                                    0,
                                                    rect.x + 20,
                                                    rect.y + 20);
  meta_flush_input (test_context);

  emit_sync_event (0);

  time_us = g_get_monotonic_time ();
  clutter_virtual_input_device_notify_touch_up (virtual_touch, time_us, 0);
  meta_flush_input (test_context);

  emit_sync_event (1);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/data-device/dnd/feedback/request-order",
                   data_device_dnd_feedback_request_order);
  g_test_add_func ("/wayland/data-device/dnd/feedback/reuse-icon",
                   data_device_dnd_feedback_reuse_icon);
  g_test_add_func ("/wayland/data-device/dnd/touch/same-surface",
                   data_device_dnd_touch_same_surface);
}

int
main (int   argc,
      char *argv[])
{
  meta_run_wayland_tests (argc, argv, init_tests);
}
