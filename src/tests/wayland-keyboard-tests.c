/*
 * Copyright (C) 2025 Red Hat, Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <gio/gio.h>
#include <wayland-client.h>
#include <gdesktop-enums.h>
#include <linux/input-event-codes.h>

#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-ref-test.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"

#include "dummy-client-protocol.h"
#include "dummy-server-protocol.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;

static void
wait_for_sync_point (unsigned int sync_point)
{
  meta_wayland_test_driver_wait_for_sync_point (test_driver, sync_point);
}

static void
keyboard_event_order (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  MetaWaylandTestClient *wayland_test_client;

  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  /* Test correct event order of key and modifier events */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "keyboard",
                                            "event-order",
                                            NULL);
  meta_wait_for_client_window (test_context, "event-order");
  wait_for_sync_point (0);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_PRESSED);

  wait_for_sync_point (1);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_RELEASED);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
keyboard_event_order2 (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  MetaWaylandTestClient *wayland_test_client;

  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  /* Test that pressed non-modifier already has modifier set */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "keyboard",
                                            "event-order2",
                                            NULL);
  meta_wait_for_client_window (test_context, "event-order2");
  wait_for_sync_point (0);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTSHIFT,
                                           CLUTTER_KEY_STATE_PRESSED);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_F,
                                           CLUTTER_KEY_STATE_PRESSED);

  wait_for_sync_point (1);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
keyboard_client_shortcut (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  MetaWaylandTestClient *wayland_test_client;

  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  /* Test shortcut behavior with super key */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "keyboard",
                                            "client-shortcut",
                                            NULL);
  meta_wait_for_client_window (test_context, "client-shortcut");
  wait_for_sync_point (0);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_PRESSED);
  wait_for_sync_point (1);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_F,
                                           CLUTTER_KEY_STATE_PRESSED);
  wait_for_sync_point (2);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_F,
                                           CLUTTER_KEY_STATE_RELEASED);
  wait_for_sync_point (3);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_RELEASED);
  wait_for_sync_point (4);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
keyboard_focus_switch (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  MetaWaylandTestClient *wayland_test_client, *wayland_test_client2;

  virtual_keyboard = clutter_seat_create_virtual_device (seat,
                                                         CLUTTER_KEYBOARD_DEVICE);

  /* Test super-tab app switching */
  wayland_test_client =
    meta_wayland_test_client_new_with_args (test_context,
                                            "keyboard",
                                            "focus-switch-dest",
                                            NULL);
  meta_wait_for_client_window (test_context, "focus-switch-dest");

  wait_for_sync_point (0);

  wayland_test_client2 =
    meta_wayland_test_client_new_with_args (test_context,
                                            "keyboard",
                                            "focus-switch-source",
                                            NULL);
  meta_wait_for_client_window (test_context, "focus-switch-source");
  wait_for_sync_point (100);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_PRESSED);
  wait_for_sync_point (101);

  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_TAB,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_TAB,
                                           CLUTTER_KEY_STATE_RELEASED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           g_get_monotonic_time (),
                                           KEY_LEFTMETA,
                                           CLUTTER_KEY_STATE_RELEASED);
  wait_for_sync_point (1);

  meta_wayland_test_client_finish (wayland_test_client);
  meta_wayland_test_client_finish (wayland_test_client2);
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
}

static void
on_after_tests (void)
{
  g_clear_object (&test_driver);
  g_clear_object (&virtual_monitor);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/keyboard/event-order",
                   keyboard_event_order);
  g_test_add_func ("/wayland/keyboard/event-order-2",
                   keyboard_event_order2);
  g_test_add_func ("/wayland/keyboard/client-shortcut",
                   keyboard_client_shortcut);
  g_test_add_func ("/wayland/keyboard/focus-switch",
                   keyboard_focus_switch);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

#ifdef MUTTER_PRIVILEGED_TEST
  return 0;
#endif

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));
  meta_context_test_set_background_color (META_CONTEXT_TEST (context),
                                          COGL_COLOR_INIT (255, 255, 255, 255));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
