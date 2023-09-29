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

#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-crtc-kms.h"
#include "backends/native/meta-crtc-virtual.h"
#include "backends/native/meta-udev.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "meta/meta-backend.h"
#include "meta/meta-cursor-tracker.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "tests/native-screen-cast.h"
#include "tests/native-virtual-monitor.h"
#include "wayland/meta-cursor-sprite-wayland.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-seat.h"

static MetaContext *test_context;

static void
set_true_cb (gboolean *done)
{
  *done = TRUE;
}

static void
meta_test_cursor_hotplug (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaCursorRenderer *cursor_renderer = meta_backend_get_cursor_renderer (backend);
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaWaylandSeat *wayland_seat = wayland_compositor->seat;
  g_autoptr (MetaWaylandTestDriver) test_driver = NULL;
  MetaCursorSprite *cursor_sprite;
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  MetaVirtualMonitor *virtual_monitor;
  ClutterSeat *seat;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  MetaWaylandTestClient *test_client;
  MetaWindow *window;
  GError *error = NULL;

  test_driver = meta_wayland_test_driver_new (wayland_compositor);

  seat = meta_backend_get_default_seat (backend);
  virtual_pointer = clutter_seat_create_virtual_device (seat,
                                                        CLUTTER_POINTER_DEVICE);

  meta_set_custom_monitor_config_full (backend, "kms-cursor-hotplug-off.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);

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

  test_client = meta_wayland_test_client_new (test_context,
                                              "kms-cursor-hotplug-helper");
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  while (TRUE)
    {
      window = meta_find_window_from_title (test_context,
                                            "kms-cursor-hotplug-helper");
      if (window && window->visible_to_compositor)
        break;
      g_main_context_iteration (NULL, TRUE);
    }

  meta_window_move_frame (window, FALSE, 0, 0);
  meta_wait_for_paint (test_context);

  cursor_renderer = meta_backend_get_cursor_renderer (backend);

  while (TRUE)
    {
      cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
      if (cursor_sprite)
        break;
      g_main_context_iteration (NULL, TRUE);
    }
  g_assert_true (META_IS_CURSOR_SPRITE_WAYLAND (cursor_sprite));

  /*
   * This tests a particular series of events:
   *
   *  1) Unplug the mouse
   *  2) Client attaches a new cursor buffer
   *  3) Client destroys cursor surface
   *  4) Monitor hotplug
   *
   * This would cause a NULL pointer deference when getting the buffer from the
   * cursor surface when trying to realize the hardware cursor buffer on the
   * hotplugged monitor.
   */

  g_clear_object (&virtual_pointer);
  while (!(wayland_seat->capabilities & WL_SEAT_CAPABILITY_POINTER))
    g_main_context_iteration (NULL, TRUE);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);
  meta_wayland_test_driver_wait_for_sync_point (test_driver, 0);

  meta_set_custom_monitor_config_full (backend, "kms-cursor-hotplug-on.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
  meta_monitor_manager_reload (monitor_manager);
  meta_wait_for_paint (test_context);

  meta_wayland_test_driver_emit_sync_event (test_driver, 1);
  meta_wayland_test_client_finish (test_client);

  g_clear_object (&virtual_monitor);
  meta_wait_for_monitors_changed (test_context);
  meta_wait_for_paint (test_context);
}

static void
meta_test_hotplug_multi_view_invalidation (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaCursorRenderer *cursor_renderer = meta_backend_get_cursor_renderer (backend);
  ClutterSeat *seat;
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  MetaVirtualMonitor *virtual_monitor;
  g_autoptr (ClutterVirtualInputDevice) virtual_pointer = NULL;
  MetaCursorSprite *cursor_sprite;
  gboolean texture_changed;
  gulong texture_changed_handler_id;
  g_autoptr (GError) error = NULL;
  GList *views;

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
  g_assert_no_error (error);

  meta_monitor_manager_reload (monitor_manager);
  views = meta_renderer_get_views (renderer);
  g_assert_true (META_IS_CRTC_KMS (meta_renderer_view_get_crtc (views->data)));
  g_assert_true (META_IS_CRTC_VIRTUAL (meta_renderer_view_get_crtc (views->next->data)));

  meta_wait_for_paint (test_context);

  cursor_sprite = meta_cursor_renderer_get_cursor (cursor_renderer);
  g_assert_nonnull (cursor_sprite);
  texture_changed_handler_id =
    g_signal_connect_swapped (cursor_sprite, "texture-changed",
                              G_CALLBACK (set_true_cb), &texture_changed);

  /* Trigger a cursor scale change, that causes invalidation on a non-first
   * KMS CRTC based cursor renderer view auxiliary object.
   */
  texture_changed = FALSE;
  meta_set_custom_monitor_config_full (backend, "kms-cursor-scale.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
  meta_monitor_manager_reload (monitor_manager);
  views = meta_renderer_get_views (renderer);
  g_assert_true (META_IS_CRTC_KMS (meta_renderer_view_get_crtc (views->data)));
  g_assert_true (META_IS_CRTC_VIRTUAL (meta_renderer_view_get_crtc (views->next->data)));
  g_assert_true (texture_changed);

  g_signal_handler_disconnect (cursor_sprite, texture_changed_handler_id);
  g_clear_object (&virtual_monitor);
  meta_wait_for_monitors_changed (test_context);
  meta_wait_for_paint (test_context);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/cursor-hotplug",
                   meta_test_cursor_hotplug);
  g_test_add_func ("/hotplug/multi-view-invalidation",
                   meta_test_hotplug_multi_view_invalidation);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
