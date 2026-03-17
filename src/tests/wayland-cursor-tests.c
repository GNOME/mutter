/*
 * Copyright (C) 2019-2026 Red Hat, Inc.
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
cursor_shape (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaWaylandTestClient *wayland_test_client;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       320.0f,
                                                       240.0f);
  meta_flush_input (test_context);

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "cursor-shape",
                                            "v2-shape-on-v1",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "cursor-shape");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "cursor-shape",
                                            "bad-shape",
                                            NULL);
  /* we wait for the window to flush out all the messages */
  meta_wait_for_client_window (test_context, "cursor-shape");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();

  /* FIXME workaround for a bug in native cursor renderer where just trying to
   * get the cursor on a plane results in no software cursor being rendered */
  meta_backend_inhibit_hw_cursor (backend);
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "cursor-shape",
                                            "ref-test",
                                            NULL);
  meta_wayland_test_client_finish (wayland_test_client);
  meta_backend_uninhibit_hw_cursor (backend);
}

static void
delayed_cursor (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  MetaWaylandTestClient *test_client1, *test_client2;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  MetaWindow *window;

  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       600.0f,
                                                       0.0f);
  meta_flush_input (test_context);

  /* FIXME workaround for a bug in native cursor renderer where just trying to
   * get the cursor on a plane results in no software cursor being rendered */
  meta_backend_inhibit_hw_cursor (backend);

  test_client1 =
    meta_wayland_test_client_new_with_args (test_context,
                                            "delayed-cursor",
                                            "src",
                                            NULL);
  meta_wait_for_client_window (test_context, "src");
  window = meta_find_client_window (test_context, "src");
  g_assert_nonnull (window);
  meta_wait_for_effects (window);
  wait_for_sync_point (0);
  meta_window_move_frame (window, FALSE, 100, 100);

  test_client2 =
    meta_wayland_test_client_new_with_args (test_context,
                                            "delayed-cursor",
                                            "dst",
                                            NULL);
  meta_wait_for_client_window (test_context, "dst");
  window = meta_find_client_window (test_context, "dst");
  g_assert_nonnull (window);
  meta_wait_for_effects (window);
  wait_for_sync_point (1);
  meta_window_move_frame (window, FALSE, 200, 200);

  /* Move into src */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       150.0f,
                                                       150.0f);
  meta_flush_input (test_context);

  wait_for_sync_point (2);

  /* Move into compositor chrome */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       250.0f,
                                                       150.0f);
  meta_flush_input (test_context);

  /* Move into dst */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       250.0f,
                                                       250.0f);
  meta_flush_input (test_context);

  wait_for_sync_point (3);

  /* Move into compositor chrome again */
  clutter_virtual_input_device_notify_absolute_motion (virtual_pointer,
                                                       g_get_monotonic_time (),
                                                       150.0f,
                                                       250.0f);
  meta_flush_input (test_context);

  emit_sync_event (0);

  meta_wayland_test_client_finish (test_client1);
  meta_wayland_test_client_finish (test_client2);
  meta_backend_uninhibit_hw_cursor (backend);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/cursor/shape",
                   cursor_shape);
  g_test_add_func ("/wayland/cursor/delayed",
                   delayed_cursor);
}

int
main (int   argc,
      char *argv[])
{
  meta_run_wayland_tests (argc, argv, init_tests);
}
