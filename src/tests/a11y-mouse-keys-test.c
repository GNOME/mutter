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

#include <libevdev/libevdev-uinput.h>
#include <linux/input-event-codes.h>

#include "backends/meta-backend-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-input-test-utils.h"
#include "tests/meta-test-utils.h"

static MetaContext *test_context;

static void
meta_test_a11y_mouse_keys (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  graphene_point_t initial_coords;
  graphene_point_t moved_coords;

  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  g_assert_true (clutter_seat_query_state (seat, NULL, &initial_coords, NULL));
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_KP6,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_KP6,
                                           CLUTTER_KEY_STATE_RELEASED);
  meta_flush_input (test_context);
  meta_wait_for_update (test_context);

  g_assert_true (clutter_seat_query_state (seat, NULL, &moved_coords, NULL));

  g_assert_cmpfloat (initial_coords.x, !=, moved_coords.x);
  g_assert_cmpfloat (initial_coords.y, ==, moved_coords.y);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;
  struct libevdev_uinput *keyboard_device;
  g_autoptr (GSettings) a11y_keyboard_settings = NULL;
  int ret;

  a11y_keyboard_settings = g_settings_new ("org.gnome.desktop.a11y.keyboard");
  g_settings_set_boolean (a11y_keyboard_settings, "mousekeys-enable", TRUE);

  keyboard_device = meta_create_test_keyboard ();
  meta_wait_for_uinput_device (keyboard_device);

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, &error));

  test_context = context;

  g_test_add_func ("/a11y/mouse-keys", meta_test_a11y_mouse_keys);

  ret = meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                     META_TEST_RUN_FLAG_CAN_SKIP);

  libevdev_uinput_destroy (keyboard_device);

  return ret;
}
