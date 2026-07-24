/*
 * Copyright (C) 2016-2025 Red Hat, Inc.
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

#include "backends/meta-monitor-config-manager.h"
#include "tests/meta-backend-test.h"
#include "tests/meta-sensors-proxy-mock.h"
#include "tests/monitor-tests-common.h"

typedef ClutterVirtualInputDevice ClutterAutoRemoveInputDevice;
static void
input_device_test_remove (ClutterAutoRemoveInputDevice *virtual_device)
{
  MetaBackend *backend = meta_context_get_backend (test_context);

  meta_backend_test_remove_test_device (META_BACKEND_TEST (backend),
                                        virtual_device);
  g_object_unref (virtual_device);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (ClutterAutoRemoveInputDevice,
                               input_device_test_remove)

typedef MetaSensorsProxyMock MetaSensorsProxyAutoResetMock;
static void
meta_sensors_proxy_confirm_released (MetaSensorsProxyMock *proxy)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);

  g_object_unref (proxy);

  g_test_message ("Confirming accelerometer released");
  while (meta_orientation_manager_get_orientation (orientation_manager) != META_ORIENTATION_UNDEFINED)
    g_main_context_iteration (NULL, TRUE);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MetaSensorsProxyAutoResetMock,
                               meta_sensors_proxy_confirm_released)

static void
on_signal (gboolean *signal_received)
{
  *signal_received = TRUE;
}

static void
check_monitor_configuration_per_orientation (MonitorTestCase *test_case,
                                             unsigned int     monitor_index,
                                             MetaOrientation  orientation,
                                             int              width,
                                             int              height)
{
  MtkMonitorTransform transform;
  MtkMonitorTransform output_transform;
  MonitorTestCaseExpect expect = test_case->expect;
  MonitorTestCaseSetup *setup = &test_case->setup;
  int i = 0;

  transform = meta_orientation_to_transform (orientation);
  output_transform = setup->outputs[monitor_index].panel_orientation_transform;
  expect.logical_monitors[monitor_index].transform =
    mtk_monitor_transform_transform (transform,
                                     mtk_monitor_transform_invert (output_transform));
  expect.crtcs[monitor_index].transform = transform;

  if (mtk_monitor_transform_is_rotated (transform))
    {
      expect.logical_monitors[monitor_index].layout.width = height;
      expect.logical_monitors[monitor_index].layout.height = width;
    }
  else
    {
      expect.logical_monitors[monitor_index].layout.width = width;
      expect.logical_monitors[monitor_index].layout.height = height;
    }

  expect.screen_width = 0;
  expect.screen_height = 0;

  for (i = 0; i < expect.n_logical_monitors; ++i)
    {
      MonitorTestCaseLogicalMonitor *monitor =
        &expect.logical_monitors[i];
      int right_edge;
      int bottom_edge;

      g_debug ("Got monitor %dx%d : %dx%d", monitor->layout.x,
               monitor->layout.y, monitor->layout.width,
               monitor->layout.height);

      right_edge = (monitor->layout.width + monitor->layout.x);
      if (right_edge > expect.screen_width)
        expect.screen_width = right_edge;

      bottom_edge = (monitor->layout.height + monitor->layout.y);
      if (bottom_edge > expect.screen_height)
        expect.screen_height = bottom_edge;
    }

  meta_check_monitor_configuration (test_context,
                                    &expect);
  meta_check_monitor_test_clients_state ();
}

static void
meta_test_monitor_orientation_phone (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1080,
          .height = 1920,
          .refresh_rate = 60.000495910644531
        }
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
          .width_mm = 125,
          .height_mm = 222,
          .connector_type = META_CONNECTOR_TYPE_eDP,
          .serial = "0x123456",
        },
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        },
      },
      .n_crtcs = 1
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1080,
              .height = 1920,
              .refresh_rate = 60.000495910644531,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 125,
          .height_mm = 222,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1080, .height = 1920 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        }
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1080,
      .screen_height = 1920
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  unsigned int n_orientation_changed = 0;

  g_test_message ("%s", G_STRFUNC);

  /* A phone: a portrait builtin panel and a touchscreen, but no pointer
   * device and no tablet mode switch, so touch mode is enabled.
   */
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_true (clutter_seat_get_touch_mode (seat));

  /* Make the sensor proxy report an accelerometer already when mutter's
   * proxy fetches its initial property cache, like iio-sensor-proxy does
   * when it starts before mutter: the property is set before the main
   * context is iterated, so panel orientation management gets enabled
   * before the first accelerometer claim attempt.
   */
  orientation_mock = meta_sensors_proxy_mock_get ();
  meta_sensors_proxy_mock_set_property (orientation_mock, "HasAccelerometer",
                                        g_variant_new_boolean (TRUE));

  while (!meta_orientation_manager_has_accelerometer (orientation_manager))
    g_main_context_iteration (NULL, TRUE);

  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, TRUE);

  g_signal_connect_swapped (orientation_manager, "orientation-changed",
                            G_CALLBACK (on_signal),
                            &n_orientation_changed);

  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_RIGHT_UP);
  while (n_orientation_changed != 1)
    g_main_context_iteration (NULL, TRUE);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_RIGHT_UP,
                        1080, 1920));

  /* Unlike on unmanaged panels, later orientation changes must keep
   * rotating the panel.
   */
  n_orientation_changed = 0;
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);
  while (n_orientation_changed != 1)
    g_main_context_iteration (NULL, TRUE);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        1080, 1920));

  g_signal_handlers_disconnect_by_data (orientation_manager,
                                        &n_orientation_changed);
}

static void
init_orientation_phone_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/orientation/phone",
                         meta_test_monitor_orientation_phone);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_orientation_phone_tests);
}
