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

#include <libevdev/libevdev.h>

#include "backends/meta-backend-private.h"
#include "core/stack.h"
#include "core/window-private.h"
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
toplevel_activation_no_serial (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-activation-no-serial");
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
toplevel_activation_before_mapped (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  g_autoptr (GSettings) wm_prefs = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;

  wm_prefs = g_settings_new ("org.gnome.desktop.wm.preferences");
  virtual_keyboard =
    clutter_seat_create_virtual_device (seat, CLUTTER_KEYBOARD_DEVICE);
  wayland_test_client =
    meta_wayland_test_client_new (test_context, "xdg-activation-before-mapped");

  wait_for_sync_point (0);
  g_settings_set_enum (wm_prefs, "focus-new-windows",
                       G_DESKTOP_FOCUS_NEW_WINDOWS_STRICT);
  emit_sync_event (0);

  wait_for_sync_point (1);
  window = meta_find_client_window (test_context,
                                    "activated-window");
  g_assert_true (meta_window_has_focus (window));
  g_assert_true (window == meta_stack_get_top (window->display->stack));
  g_assert_true (window->stack_position == 1);

  meta_wayland_test_client_finish (wayland_test_client);
  g_settings_reset (wm_prefs, "focus-new-windows");
}

static void
toplevel_activation_serial (const char             *client_arg,
                            ClutterInputDeviceType  device_type)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaDisplay *display = meta_context_get_display (test_context);
  g_autoptr (ClutterVirtualInputDevice) device = NULL;
  g_autoptr (GSettings) wm_prefs = NULL;
  MetaWaylandTestClient *wayland_test_client;
  ClutterSeat *seat;
  MetaWindow *parent_window;
  MtkRectangle parent_rect;
  MetaWindow *child_window;

  wm_prefs = g_settings_new ("org.gnome.desktop.wm.preferences");

  seat = meta_backend_get_default_seat (backend);
  device = clutter_seat_create_virtual_device (seat, device_type);

  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "xdg-activation-serial",
                                            client_arg,
                                            NULL);

  while (!(parent_window = meta_find_client_window (test_context,
                                                    "xdg-activation-parent")))
    g_main_context_iteration (NULL, TRUE);
  g_object_add_weak_pointer (G_OBJECT (parent_window),
                             (gpointer *) &parent_window);

  while (meta_window_is_hidden (parent_window))
    g_main_context_iteration (NULL, TRUE);
  meta_wait_for_effects (parent_window);

  g_settings_set_enum (wm_prefs, "focus-new-windows",
                       G_DESKTOP_FOCUS_NEW_WINDOWS_STRICT);

  g_assert_true (meta_display_get_focus_window (display) == parent_window);

  meta_window_get_frame_rect (parent_window, &parent_rect);

  if (device_type == CLUTTER_POINTER_DEVICE)
    {
      clutter_virtual_input_device_notify_absolute_motion (device,
                                                           CLUTTER_CURRENT_TIME,
                                                           parent_rect.x + 10,
                                                           parent_rect.y + 10);
      clutter_virtual_input_device_notify_button (device,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_PRESSED);
      clutter_virtual_input_device_notify_button (device,
                                                  CLUTTER_CURRENT_TIME,
                                                  CLUTTER_BUTTON_PRIMARY,
                                                  CLUTTER_BUTTON_STATE_RELEASED);
    }
  else if (device_type == CLUTTER_KEYBOARD_DEVICE)
    {
      meta_window_activate (parent_window, META_CURRENT_TIME);
      clutter_virtual_input_device_notify_key (device,
                                               CLUTTER_CURRENT_TIME,
                                               KEY_A,
                                               CLUTTER_KEY_STATE_PRESSED);
      clutter_virtual_input_device_notify_key (device,
                                               CLUTTER_CURRENT_TIME,
                                               KEY_A,
                                               CLUTTER_KEY_STATE_RELEASED);
    }

  while (!(child_window = meta_find_client_window (test_context,
                                                   "xdg-activation-child")))
    g_main_context_iteration (NULL, TRUE);
  g_object_add_weak_pointer (G_OBJECT (child_window),
                             (gpointer *) &child_window);

  while (meta_window_is_hidden (child_window))
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (meta_display_get_focus_window (display) == child_window);

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);

  g_settings_reset (wm_prefs, "focus-new-windows");

  while (child_window || parent_window)
    g_main_context_iteration (NULL, TRUE);
}

static void
toplevel_activation_button_press (void)
{
  toplevel_activation_serial ("button-press", CLUTTER_POINTER_DEVICE);
}

static void
toplevel_activation_button_release (void)
{
  toplevel_activation_serial ("button-release", CLUTTER_POINTER_DEVICE);
}

static void
toplevel_activation_key_press (void)
{
  toplevel_activation_serial ("key-press", CLUTTER_KEYBOARD_DEVICE);
}

static void
toplevel_activation_key_release (void)
{
  toplevel_activation_serial ("key-release", CLUTTER_KEYBOARD_DEVICE);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/toplevel/activation/no-serial",
                   toplevel_activation_no_serial);
  g_test_add_func ("/wayland/toplevel/activation/before-mapped",
                   toplevel_activation_before_mapped);
  g_test_add_func ("/wayland/toplevel/activation/button-press",
                   toplevel_activation_button_press);
  g_test_add_func ("/wayland/toplevel/activation/button-release",
                   toplevel_activation_button_release);
  g_test_add_func ("/wayland/toplevel/activation/key-press",
                   toplevel_activation_key_press);
  g_test_add_func ("/wayland/toplevel/activation/key-release",
                   toplevel_activation_key_release);
}

int
main (int   argc,
      char *argv[])
{
  meta_run_wayland_tests (argc, argv, init_tests);
}
