/*
 * Copyright (C) 2023 Red Hat
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

#include <linux/input-event-codes.h>

#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-udev.h"
#include "meta-test/meta-context-test.h"
#include "tests/drm-mock/drm-mock.h"
#include "tests/meta-test-utils.h"

typedef enum _State
{
  INIT,
  PAINTED,
  PRESENTED,
} State;

static MetaContext *test_context;

static void
on_after_paint (ClutterStage     *stage,
                ClutterStageView *view,
                ClutterFrame     *frame,
                State            *state)
{
  *state = PAINTED;
}

static void
on_presented (ClutterStage     *stage,
              ClutterStageView *view,
              ClutterFrameInfo *frame_info,
              State            *state)
{
  if (*state == PAINTED)
    *state = PRESENTED;
}

static void
set_true_cb (gboolean *done)
{
  *done = TRUE;
}

static void
meta_test_reload (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);
  GList *logical_monitors;
  gulong after_paint_handler_id;
  gulong presented_handler_id;
  g_autoptr (GError) error = NULL;
  State state;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  after_paint_handler_id = g_signal_connect (stage, "after-paint",
                                             G_CALLBACK (on_after_paint),
                                             &state);
  presented_handler_id = g_signal_connect (stage, "presented",
                                           G_CALLBACK (on_presented),
                                           &state);

  state = INIT;
  clutter_actor_queue_redraw (stage);
  while (state < PAINTED)
    g_main_context_iteration (NULL, TRUE);

  meta_monitor_manager_reload (monitor_manager);

  while (state < PRESENTED)
    g_main_context_iteration (NULL, TRUE);

  state = INIT;
  clutter_actor_queue_redraw (stage);
  while (state < PRESENTED)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (stage, after_paint_handler_id);
  g_signal_handler_disconnect (stage, presented_handler_id);
}

static void
disconnect_connector_filter (gpointer resource,
                             gpointer user_data)
{
  drmModeConnector *drm_connector = resource;

  drm_connector->connection = DRM_MODE_DISCONNECTED;
}

static void
frame_cb (CoglOnscreen  *onscreen,
          CoglFrameEvent frame_event,
          CoglFrameInfo *frame_info,
          void          *user_data)
{
  State *state = user_data;

  if (frame_event == COGL_FRAME_EVENT_SYNC)
    return;

  if (*state == PAINTED)
    *state = PRESENTED;
}

static void
meta_test_disconnect_connect (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaUdev *udev = meta_backend_native_get_udev (META_BACKEND_NATIVE (backend));
  g_autolist (GObject) udev_devices = NULL;
  GUdevDevice *udev_device;
  GList *logical_monitors;
  gulong after_paint_handler_id;
  gulong presented_handler_id;
  ClutterStageView *view;
  CoglFramebuffer *onscreen;
  g_autoptr (GError) error = NULL;
  State state;

  udev_devices = meta_udev_list_drm_devices (udev, &error);
  g_assert_cmpuint (g_list_length (udev_devices), ==, 1);
  udev_device = g_list_first (udev_devices)->data;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  after_paint_handler_id = g_signal_connect (stage, "after-paint",
                                             G_CALLBACK (on_after_paint),
                                             &state);
  presented_handler_id = g_signal_connect (stage, "presented",
                                           G_CALLBACK (on_presented),
                                           &state);

  g_debug ("Disconnect during page flip");
  view = clutter_stage_peek_stage_views (CLUTTER_STAGE (stage))->data;
  onscreen = clutter_stage_view_get_onscreen (view);
  g_assert_true (COGL_IS_ONSCREEN (onscreen));
  state = INIT;
  clutter_actor_queue_redraw (stage);
  while (state < PAINTED)
    g_main_context_iteration (NULL, TRUE);
  drm_mock_set_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR,
                                disconnect_connector_filter, NULL);
  g_signal_emit_by_name (udev, "hotplug", udev_device);
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 0);

  g_debug ("Wait until page flip completes");
  cogl_onscreen_add_frame_callback (COGL_ONSCREEN (onscreen),
                                    frame_cb, &state, NULL);
  while (state < PRESENTED)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Reconnect connector, wait for presented");
  drm_mock_unset_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR);
  g_signal_emit_by_name (udev, "hotplug", udev_device);
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  state = INIT;
  clutter_actor_queue_redraw (stage);
  while (state < PRESENTED)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Disconnect after page flip");
  drm_mock_set_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR,
                                disconnect_connector_filter, NULL);
  g_signal_emit_by_name (udev, "hotplug", udev_device);
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 0);
  while (TRUE)
    {
      if (!g_main_context_iteration (NULL, FALSE))
        break;
    }

  g_debug ("Restore");
  drm_mock_unset_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR);
  g_signal_emit_by_name (udev, "hotplug", udev_device);
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  g_signal_handler_disconnect (stage, after_paint_handler_id);
  g_signal_handler_disconnect (stage, presented_handler_id);
}

static gboolean
on_key_release (ClutterActor       *actor,
                const ClutterEvent *event,
                MetaMonitorManager *monitor_manager)
{
  if (clutter_event_get_key_symbol (event) == CLUTTER_KEY_a)
    {
      g_debug ("Switching config");
      meta_monitor_manager_switch_config (monitor_manager,
                                          META_MONITOR_SWITCH_CONFIG_ALL_MIRROR);
    }

  return TRUE;
}

static void
on_monitors_changed (MetaMonitorManager *monitor_manager,
                     gboolean           *monitors_changed)
{
  *monitors_changed = TRUE;
}

static void
meta_test_switch_config (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterSeat *seat = meta_backend_get_default_seat (backend);
  g_autoptr (ClutterVirtualInputDevice) virtual_keyboard = NULL;
  MetaVirtualMonitor *virtual_monitor;
  GList *logical_monitors;
  MtkRectangle logical_monitor_layout;
  gulong after_paint_handler_id;
  gulong presented_handler_id;
  gulong monitors_changed_handler_id;
  gboolean monitors_changed;
  g_autoptr (GError) error = NULL;
  ClutterActor *actor;
  State state;

  after_paint_handler_id = g_signal_connect (stage, "after-paint",
                                             G_CALLBACK (on_after_paint),
                                             &state);
  presented_handler_id = g_signal_connect (stage, "presented",
                                           G_CALLBACK (on_presented),
                                           &state);
  monitors_changed_handler_id =
    g_signal_connect (monitor_manager, "monitors-changed",
                      G_CALLBACK (on_monitors_changed),
                      &monitors_changed);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  logical_monitor_layout =
    meta_logical_monitor_get_layout (logical_monitors->data);

  virtual_monitor = meta_create_test_monitor (test_context,
                                              logical_monitor_layout.width,
                                              logical_monitor_layout.height,
                                              60.0);

  actor = clutter_actor_new ();
  g_object_add_weak_pointer (G_OBJECT (actor), (gpointer *) &actor);
  clutter_actor_insert_child_above (stage,
                                    actor,
                                    clutter_actor_get_first_child (stage));
  clutter_actor_set_size (actor,
                          logical_monitor_layout.width,
                          logical_monitor_layout.height);
  clutter_actor_set_position (actor, 0, 0);
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_show (actor);
  clutter_actor_grab_key_focus (actor);
  g_signal_connect (actor, "key-press-event",
                    G_CALLBACK (on_key_release),
                    monitor_manager);

  monitors_changed = FALSE;

  g_debug ("Sending virtual keyboard event");
  virtual_keyboard =
    clutter_seat_create_virtual_device (seat, CLUTTER_KEYBOARD_DEVICE);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           CLUTTER_CURRENT_TIME,
                                           KEY_A,
                                           CLUTTER_KEY_STATE_PRESSED);
  clutter_virtual_input_device_notify_key (virtual_keyboard,
                                           CLUTTER_CURRENT_TIME,
                                           KEY_A,
                                           CLUTTER_KEY_STATE_RELEASED);

  g_debug ("Waiting for monitors changed");
  while (!monitors_changed)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Waiting for being repainted");
  state = INIT;
  clutter_actor_queue_redraw (stage);
  while (state != PRESENTED)
    g_main_context_iteration (NULL, TRUE);

  clutter_actor_destroy (actor);
  g_assert_null (actor);

  g_signal_handler_disconnect (stage, after_paint_handler_id);
  g_signal_handler_disconnect (stage, presented_handler_id);

  monitors_changed = FALSE;
  g_clear_object (&virtual_monitor);
  while (!monitors_changed)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (monitor_manager, monitors_changed_handler_id);
}

static void
set_power_save_mode_via_dbus (MetaPowerSave power_save)
{
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GError) error = NULL;
  GVariant *parameters;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                         G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                         NULL,
                                         "org.gnome.Mutter.DisplayConfig",
                                         "/org/gnome/Mutter/DisplayConfig",
                                         "org.freedesktop.DBus.Properties",
                                         NULL,
                                         &error);
  g_assert_no_error (error);

  parameters = g_variant_new ("(ssv)",
                              "org.gnome.Mutter.DisplayConfig",
                              "PowerSaveMode",
                              g_variant_new_int32 (power_save));
  g_dbus_proxy_call (proxy,
                     "Set",
                     parameters,
                     G_DBUS_CALL_FLAGS_NO_AUTO_START,
                     G_MAXINT,
                     NULL, NULL, NULL);
}

static void
emulate_hotplug (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaUdev *udev = meta_backend_native_get_udev (META_BACKEND_NATIVE (backend));
  g_autoptr (GError) error = NULL;
  g_autolist (GObject) udev_devices = NULL;
  GUdevDevice *udev_device;

  udev_devices = meta_udev_list_drm_devices (udev, &error);
  g_assert_cmpuint (g_list_length (udev_devices), ==, 1);
  udev_device = g_list_first (udev_devices)->data;
  g_signal_emit_by_name (udev, "hotplug", udev_device);
}

static void
meta_test_power_save_implicit_on (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *logical_monitors;
  gulong power_save_handler_id;
  gulong monitors_changed_handler_id;
  gboolean power_save_mode_changed;
  gboolean monitors_changed;

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  meta_wait_for_paint (test_context);

  power_save_handler_id =
    g_signal_connect_swapped (monitor_manager, "power-save-mode-changed",
                              G_CALLBACK (set_true_cb), &power_save_mode_changed);

  set_power_save_mode_via_dbus (META_POWER_SAVE_OFF);

  power_save_mode_changed = FALSE;
  while (!power_save_mode_changed)
    g_main_context_iteration (NULL, TRUE);

  power_save_mode_changed = FALSE;
  monitors_changed = FALSE;

  monitors_changed_handler_id =
    g_signal_connect_swapped (monitor_manager, "monitors-changed",
                              G_CALLBACK (set_true_cb), &monitors_changed);

  drm_mock_set_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR,
                                disconnect_connector_filter, NULL);
  emulate_hotplug ();

  while (!power_save_mode_changed || !monitors_changed)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (monitor_manager, power_save_handler_id);
  g_signal_handler_disconnect (monitor_manager, monitors_changed_handler_id);

  drm_mock_unset_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR);
  emulate_hotplug ();
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
}

static void
init_tests (void)
{
  g_test_add_func ("/hotplug/reload",
                   meta_test_reload);
  g_test_add_func ("/hotplug/disconnect-connect",
                   meta_test_disconnect_connect);
  g_test_add_func ("/hotplug/switch-config",
                   meta_test_switch_config);
  g_test_add_func ("/hotplug/power-save-implicit-off",
                   meta_test_power_save_implicit_on);
}

int
main (int argc, char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  test_context = context;

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
