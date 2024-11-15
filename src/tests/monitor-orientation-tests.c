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
               monitor->layout.y, monitor->layout.width, monitor->layout.height);

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
meta_test_monitor_orientation_initial_portrait_mode_workaround (void)
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
  g_autoptr (ClutterAutoRemoveInputDevice) pointer_device = NULL;
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  unsigned int n_orientation_changed = 0;

  g_test_message ("%s", G_STRFUNC);

  orientation_mock = meta_sensors_proxy_mock_get ();

  /* Add a touch device *and* a pointer device. This means a touchscreen is
   * present, but touch mode is disabled. That should be enough to trigger the
   * initial-orientation workaround.
   */
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  pointer_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_POINTER_DEVICE, 1);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_false (clutter_seat_get_touch_mode (seat));
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

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, FALSE);

  /* Change the orientation to portrait and the orientation change should
   * now be ignored, because it's no longer the initial one.
   */
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_RIGHT_UP,
                        1080, 1920));

  g_signal_handlers_disconnect_by_data (orientation_manager, &n_orientation_changed);
}

static void
meta_test_monitor_orientation_is_managed (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
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
          .width_mm = 222,
          .height_mm = 125,
          .connector_type = META_CONNECTOR_TYPE_DisplayPort,
        }
      },
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0
        }
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
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
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
          .width_mm = 222,
          .height_mm = 125
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        }
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
      },
      .n_crtcs = 1,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  ClutterBackend *clutter_backend = meta_backend_get_clutter_backend (backend);
  ClutterSeat *seat = clutter_backend_get_default_seat (clutter_backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);

  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);
  meta_check_monitor_test_clients_state ();

  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  g_assert_null (meta_monitor_manager_get_builtin_monitor (monitor_manager));
  test_case.setup.outputs[0].connector_type = META_CONNECTOR_TYPE_eDP;
  test_case.setup.outputs[0].serial = "0x1000001";
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
  g_assert_nonnull (meta_monitor_manager_get_builtin_monitor (monitor_manager));

  g_assert_false (clutter_seat_get_touch_mode (seat));
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  g_assert_true (clutter_seat_get_touch_mode (seat));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  orientation_mock = meta_sensors_proxy_mock_get ();
  g_assert_false (
    meta_orientation_manager_has_accelerometer (orientation_manager));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_sensors_proxy_mock_set_property (orientation_mock, "HasAccelerometer",
                                        g_variant_new_boolean (TRUE));

  while (!meta_orientation_manager_has_accelerometer (orientation_manager))
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (
    meta_orientation_manager_has_accelerometer (orientation_manager));
  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  test_case.setup.outputs[0].connector_type = META_CONNECTOR_TYPE_DisplayPort;
  test_case.setup.outputs[0].serial = NULL;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
  g_assert_null (meta_monitor_manager_get_builtin_monitor (monitor_manager));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  test_case.setup.outputs[0].connector_type = META_CONNECTOR_TYPE_eDP;
  test_case.setup.outputs[0].serial = "0x1000001";
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
  g_assert_nonnull (meta_monitor_manager_get_builtin_monitor (monitor_manager));
  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_sensors_proxy_mock_set_property (orientation_mock, "HasAccelerometer",
                                        g_variant_new_boolean (FALSE));

  while (meta_orientation_manager_has_accelerometer (orientation_manager))
    g_main_context_iteration (NULL, FALSE);

  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_sensors_proxy_mock_set_property (orientation_mock, "HasAccelerometer",
                                        g_variant_new_boolean (TRUE));

  while (!meta_orientation_manager_has_accelerometer (orientation_manager))
    g_main_context_iteration (NULL, FALSE);

  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  meta_backend_test_remove_test_device (META_BACKEND_TEST (backend),
                                        touch_device);
  g_clear_object (&touch_device);

  g_assert_false (clutter_seat_get_touch_mode (seat));
  g_assert_false (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));

  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  g_assert_true (clutter_seat_get_touch_mode (seat));
  g_assert_true (
    meta_monitor_manager_get_panel_orientation_managed (monitor_manager));
}

static void
meta_test_monitor_orientation_initial_rotated (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
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
          .width_mm = 222,
          .height_mm = 125,
          .connector_type = META_CONNECTOR_TYPE_eDP,
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
              .width = 1024,
              .height = 768,
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
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
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
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  MetaOrientation orientation;
  unsigned int n_orientation_changed = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  g_signal_connect_swapped (orientation_manager, "orientation-changed",
                            G_CALLBACK (on_signal),
                            &n_orientation_changed);

  orientation = META_ORIENTATION_LEFT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  while (n_orientation_changed != 1)
    g_main_context_iteration (NULL, TRUE);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 1024, 768));

  g_signal_handlers_disconnect_by_data (orientation_manager, &n_orientation_changed);
}

static void
meta_test_monitor_orientation_initial_rotated_no_touch_mode (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
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
          .width_mm = 222,
          .height_mm = 125,
          .connector_type = META_CONNECTOR_TYPE_eDP,
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
              .width = 1024,
              .height = 768,
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
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
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
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  MetaOrientation orientation;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  orientation = META_ORIENTATION_LEFT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, FALSE);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL, 1024, 768));
}

static void
meta_test_monitor_orientation_initial_stored_rotated (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
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
          .width_mm = 222,
          .height_mm = 125,
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
              .width = 1920,
              .height = 1080,
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
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
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
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  MetaOrientation orientation;
  unsigned int n_orientation_changed = 0;
  unsigned int n_sensor_active = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  g_signal_connect_swapped (orientation_manager, "orientation-changed",
                            G_CALLBACK (on_signal),
                            &n_orientation_changed);

  orientation = META_ORIENTATION_RIGHT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  while (n_orientation_changed != 1)
    g_main_context_iteration (NULL, TRUE);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "lid-scale.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 960, 540));

  g_test_message ("Closing lid");
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);


  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 960, 540));

  g_test_message ("Rotating to left-up");
  orientation = META_ORIENTATION_LEFT_UP;
  n_orientation_changed = 0;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);
  while (n_orientation_changed != 1)
    g_main_context_iteration (NULL, TRUE);

  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);


  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, orientation, 960, 540));

  /* When no touch device is available, we reset back to normal orientation. */
  g_test_message ("Removing touch device");
  n_orientation_changed = 0;
  meta_backend_test_remove_test_device (META_BACKEND_TEST (backend),
                                        touch_device);
  g_clear_object (&touch_device);

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, FALSE);
  g_assert_cmpuint (n_orientation_changed, ==, 0);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        960, 540));

  g_signal_connect_swapped (orientation_manager, "sensor-active",
                            G_CALLBACK (on_signal),
                            &n_sensor_active);

  /* Adding back the touch device, we should now pick up the orientation again */
  n_orientation_changed = 0;
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, TRUE);
  while (n_sensor_active != 1)
    g_main_context_iteration (NULL, TRUE);

  g_assert_cmpuint (n_orientation_changed, ==, 0);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_LEFT_UP,
                        960, 540));

  /* Now remove it again, we should go to NORMAL and even when rotating we
   * should remain in NORMAL.
   */
  g_test_message ("Removing touch device again");
  meta_backend_test_remove_test_device (META_BACKEND_TEST (backend),
                                        touch_device);
  g_clear_object (&touch_device);

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, FALSE);

  g_test_message ("Rotating to right-up");
  orientation = META_ORIENTATION_RIGHT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        960, 540));

  g_signal_handlers_disconnect_by_data (orientation_manager, &n_orientation_changed);
  g_signal_handlers_disconnect_by_data (orientation_manager, &n_sensor_active);
}

static void
meta_test_monitor_orientation_initial_stored_rotated_no_touch (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
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
          .width_mm = 222,
          .height_mm = 125,
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
              .width = 1920,
              .height = 1080,
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
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 960, .height = 540 },
          .scale = 2
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
      .screen_width = 960,
      .screen_height = 540
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  MetaOrientation orientation;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  orientation = META_ORIENTATION_RIGHT_UP;
  meta_sensors_proxy_mock_set_orientation (orientation_mock, orientation);

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, FALSE);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_set_custom_monitor_config (test_context, "lid-scale.xml");
  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        960, 540));

  g_test_message ("Closing lid");
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_monitor_manager_lid_is_closed_changed (monitor_manager);


  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        960, 540));
}

static void
meta_test_monitor_orientation_changes (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
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
          .width_mm = 222,
          .height_mm = 125,
          .connector_type = META_CONNECTOR_TYPE_eDP,
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
              .width = 1024,
              .height = 768,
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
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
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
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (MetaMonitorsConfig) initial_config = NULL;
  g_autoptr (MetaMonitorsConfig) previous_config = NULL;
  unsigned int n_monitors_changed = 0;
  MetaOrientation i;
  unsigned int n_orientation_changed = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_set_object (&previous_config,
                meta_monitor_config_manager_get_previous (config_manager));
  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));
  g_signal_connect_swapped (monitor_manager, "monitors-changed",
                            G_CALLBACK (on_signal),
                            &n_monitors_changed);

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_UNDEFINED);

  g_signal_connect_swapped (orientation_manager, "orientation-changed",
                            G_CALLBACK (on_signal),
                            &n_orientation_changed);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      n_monitors_changed = 0;
      n_orientation_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      while (n_orientation_changed != 1)
        g_main_context_iteration (NULL, TRUE);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert_cmpuint (n_monitors_changed, ==, 1);
      g_assert_true (previous == previous_config);
      g_assert_true (current != initial_config);
      g_assert_true (meta_monitors_config_key_equal (current->key,
                                                     initial_config->key));
    }

  /* Ensure applying the current orientation doesn't change the config */
  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));

  n_monitors_changed = 0;
  n_orientation_changed = 0;
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        1024, 768));

  g_assert_cmpuint (n_orientation_changed, ==, 0);
  g_assert_cmpuint (n_monitors_changed, ==, 0);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 initial_config);

  /* When no touch device is available, the orientation changes are ignored */
  g_test_message ("Removing touch device");
  meta_backend_test_remove_test_device (META_BACKEND_TEST (backend),
                                        touch_device);
  g_clear_object (&touch_device);

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, FALSE);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      n_monitors_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, META_ORIENTATION_NORMAL,
                            1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert_true (previous == previous_config);
      g_assert_true (current == initial_config);
      g_assert_cmpuint (n_monitors_changed, ==, 0);
    }

  g_signal_handlers_disconnect_by_data (monitor_manager, &n_monitors_changed);
  g_signal_handlers_disconnect_by_data (orientation_manager, &n_orientation_changed);
}

static void
meta_test_monitor_orientation_changes_for_transformed_panel (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
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
          .width_mm = 222,
          .height_mm = 125,
          .connector_type = META_CONNECTOR_TYPE_eDP,
          .panel_orientation_transform = MTK_MONITOR_TRANSFORM_90,
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
              .width = 768,
              .height = 1024,
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
          .width_mm = 222,
          .height_mm = 125,
        }
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
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
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  g_autoptr (MetaMonitorsConfig) initial_config = NULL;
  g_autoptr (MetaMonitorsConfig) previous_config = NULL;
  unsigned int n_monitors_changed = 0;
  MetaOrientation i;
  unsigned int n_orientation_changed = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_set_object (&previous_config,
                meta_monitor_config_manager_get_previous (config_manager));
  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));
  g_signal_connect_swapped (monitor_manager, "monitors-changed",
                            G_CALLBACK (on_signal),
                            &n_monitors_changed);

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_UNDEFINED);

  g_signal_connect_swapped (orientation_manager, "orientation-changed",
                            G_CALLBACK (on_signal),
                            &n_orientation_changed);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      n_monitors_changed = 0;
      n_orientation_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      while (n_orientation_changed != 1)
        g_main_context_iteration (NULL, TRUE);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert_true (n_monitors_changed == 1);
      g_assert_true (previous == previous_config);
      g_assert_true (current != initial_config);
      g_assert_true (meta_monitors_config_key_equal (current->key,
                                                     initial_config->key));
    }

  /* Ensure applying the current orientation doesn't change the config */
  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  g_set_object (&initial_config,
                meta_monitor_config_manager_get_current (config_manager));

  n_monitors_changed = 0;
  n_orientation_changed = 0;
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_NORMAL);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_NORMAL,
                        1024, 768));

  g_assert_cmpuint (n_monitors_changed, ==, 0);
  g_assert_cmpuint (n_orientation_changed, ==, 0);
  g_assert_true (meta_monitor_config_manager_get_current (config_manager) ==
                 initial_config);

  /* When no touch device is available, the orientation changes are ignored */
  g_test_message ("Removing touch device");
  meta_backend_test_remove_test_device (META_BACKEND_TEST (backend),
                                        touch_device);
  g_clear_object (&touch_device);

  meta_sensors_proxy_mock_wait_accelerometer_claimed (orientation_mock, FALSE);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      MetaMonitorsConfig *current;
      MetaMonitorsConfig *previous;

      n_monitors_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, META_ORIENTATION_NORMAL,
                            1024, 768));

      current = meta_monitor_config_manager_get_current (config_manager);
      previous = meta_monitor_config_manager_get_previous (config_manager);

      g_assert_true (previous == previous_config);
      g_assert_true (current == initial_config);
      g_assert_cmpuint (n_monitors_changed, ==, 0);
    }

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);
  n_monitors_changed = 0;
  n_orientation_changed = 0;
  meta_sensors_proxy_mock_set_orientation (orientation_mock,
                                           META_ORIENTATION_RIGHT_UP);
  while (n_orientation_changed != 1)
    g_main_context_iteration (NULL, TRUE);

  META_TEST_LOG_CALL ("Checking configuration per orientation",
                      check_monitor_configuration_per_orientation (
                        &test_case, 0, META_ORIENTATION_RIGHT_UP,
                        1024, 768));
  g_assert_cmpuint (n_monitors_changed, ==, 1);

  g_signal_handlers_disconnect_by_data (monitor_manager, &n_monitors_changed);
  g_signal_handlers_disconnect_by_data (orientation_manager, &n_orientation_changed);
}

static void
meta_test_monitor_orientation_changes_with_hotplugging (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 1,
      .outputs = {
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .connector_type = META_CONNECTOR_TYPE_eDP,
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 1 },
          .n_possible_crtcs = 1,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_outputs = 1, /* Second is hotplugged later */
      .crtcs = {
        {
          .current_mode = -1
        },
        {
          .current_mode = -1
        }
      },
      .n_crtcs = 2
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
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
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 1 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1024,
              .height = 768,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 1,
                  .crtc_mode = 0
                }
              }
            }
          },
          .n_modes = 1,
          .current_mode = 0,
          .width_mm = 220,
          .height_mm = 124
        }
      },
      .n_monitors = 1, /* Second is hotplugged later */
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1024, .height = 768 },
          .scale = 1
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1024, .y = 0, .width = 1024, .height = 768 },
          .scale = 1,
          .transform = MTK_MONITOR_TRANSFORM_NORMAL,
        }
      },
      .n_logical_monitors = 1, /* Second is hotplugged later */
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 0,
        },
        {
          .current_mode = -1,
          .transform = MTK_MONITOR_TRANSFORM_NORMAL,
        }
      },
      .n_crtcs = 2,
      .n_tiled_monitors = 0,
      .screen_width = 1024,
      .screen_height = 768
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaOrientationManager *orientation_manager =
    meta_backend_get_orientation_manager (backend);
  g_autoptr (ClutterAutoRemoveInputDevice) touch_device = NULL;
  g_autoptr (MetaSensorsProxyAutoResetMock) orientation_mock = NULL;
  MetaOrientation i;
  unsigned int times_signalled = 0;
  unsigned int n_orientation_changed = 0;

  g_test_message ("%s", G_STRFUNC);
  orientation_mock = meta_sensors_proxy_mock_get ();
  touch_device =
    meta_backend_test_add_test_device (META_BACKEND_TEST (backend),
                                       CLUTTER_TOUCHSCREEN_DEVICE, 1);

  /*
   * The first part of this test emulate the following:
   *  1) Start with the lid open
   *  2) Rotate the device in all directions
   *  3) Connect external monitor
   *  4) Rotate the device in all directions
   *  5) Close lid
   */

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);

  meta_emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  g_signal_connect_swapped (orientation_manager, "orientation-changed",
                            G_CALLBACK (on_signal),
                            &n_orientation_changed);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      n_orientation_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      while (n_orientation_changed != 1)
        g_main_context_iteration (NULL, TRUE);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));
    }

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  g_test_message ("External monitor connected");
  test_case.setup.n_outputs = 2;
  test_case.expect.n_outputs = 2;
  test_case.expect.n_monitors = 2;
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[1].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  /* Rotate the monitor in all the directions */
  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      n_orientation_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      while (n_orientation_changed != 1)
        g_main_context_iteration (NULL, TRUE);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));
    }

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  g_test_message ("Lid closed");
  test_case.expect.monitors[0].current_mode = -1;
  test_case.expect.logical_monitors[0].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 1;
  test_case.expect.crtcs[0].current_mode = -1;
  test_case.expect.crtcs[1].x = 0;
  test_case.expect.screen_width = 1024;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
  meta_emulate_hotplug (test_setup);

  /* Rotate the monitor in all the directions */
  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      n_orientation_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      while (n_orientation_changed != 1)
        g_main_context_iteration (NULL, TRUE);

      meta_check_monitor_configuration (test_context,
                                        &test_case.expect);
    }

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  /*
   * The second part of this test emulate the following at each device rotation:
   *  1) Open lid
   *  2) Close lid
   *  3) Change orientation
   *  4) Reopen the lid
   *  2) Disconnect external monitor
   */

  g_test_message ("Lid opened");
  test_case.expect.monitors[0].current_mode = 0;
  test_case.expect.logical_monitors[0].monitors[0] = 0,
  test_case.expect.logical_monitors[1].monitors[0] = 1,
  test_case.expect.n_logical_monitors = 2;
  test_case.expect.crtcs[0].current_mode = 0;
  test_case.expect.crtcs[1].x = 1024;
  test_case.expect.screen_width = 1024 * 2;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
  meta_emulate_hotplug (test_setup);
  meta_check_monitor_configuration (test_context,
                                    &test_case.expect);

  for (i = META_N_ORIENTATIONS - 1; i > META_ORIENTATION_UNDEFINED; i--)
    {
      g_test_message ("Closing lid");
      test_case.expect.monitors[0].current_mode = -1;
      test_case.expect.logical_monitors[0].monitors[0] = 1,
      test_case.expect.n_logical_monitors = 1;
      test_case.expect.crtcs[0].current_mode = -1;
      test_case.expect.crtcs[1].x = 0;
      test_case.expect.screen_width = 1024;

      test_setup = meta_create_monitor_test_setup (backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), TRUE);
      meta_emulate_hotplug (test_setup);

      /* Change orientation */
      n_orientation_changed = 0;
      meta_sensors_proxy_mock_set_orientation (orientation_mock, i);
      while (n_orientation_changed != 1)
        g_main_context_iteration (NULL, TRUE);

      meta_check_monitor_configuration (test_context,
                                        &test_case.expect);

      g_test_message ("Opening lid");
      test_case.expect.monitors[0].current_mode = 0;
      test_case.expect.logical_monitors[0].monitors[0] = 0,
      test_case.expect.logical_monitors[1].monitors[0] = 1,
      test_case.expect.n_logical_monitors = 2;
      test_case.expect.crtcs[0].current_mode = 0;
      test_case.expect.crtcs[1].x = 1024;

      test_setup = meta_create_monitor_test_setup (backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      meta_backend_test_set_is_lid_closed (META_BACKEND_TEST (backend), FALSE);
      meta_emulate_hotplug (test_setup);

      /* We don't actually expect the orientation to change here, so we
       * just wait for a moment (so that if the orientation *did* change,
       * mutter has had a chance to process it), and then continue. */
      meta_wait_for_possible_orientation_change (orientation_manager,
                                                 &times_signalled);
      g_assert_cmpuint (times_signalled, ==, 0);

      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      g_test_message ("External monitor disconnected");
      test_case.setup.n_outputs = 1;
      test_case.expect.n_outputs = 1;
      test_case.expect.n_monitors = 1;
      test_case.expect.n_logical_monitors = 1;
      test_case.expect.crtcs[1].current_mode = -1;

      test_setup = meta_create_monitor_test_setup (backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      meta_emulate_hotplug (test_setup);
      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));

      g_test_message ("External monitor connected");
      test_case.setup.n_outputs = 2;
      test_case.expect.n_outputs = 2;
      test_case.expect.n_monitors = 2;
      test_case.expect.n_logical_monitors = 2;
      test_case.expect.crtcs[1].current_mode = 0;
      test_case.expect.crtcs[1].x = 1024;

      test_setup = meta_create_monitor_test_setup (backend,
                                                   &test_case.setup,
                                                   MONITOR_TEST_FLAG_NO_STORED);
      meta_emulate_hotplug (test_setup);
      META_TEST_LOG_CALL ("Checking configuration per orientation",
                          check_monitor_configuration_per_orientation (
                            &test_case, 0, i, 1024, 768));
    }

  g_assert_cmpuint (
    meta_orientation_manager_get_orientation (orientation_manager),
    ==,
    META_ORIENTATION_NORMAL);

  g_signal_handlers_disconnect_by_data (orientation_manager, &n_orientation_changed);
}

static void
init_orientation_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/orientation/initial-portrait-mode-workaround",
                         meta_test_monitor_orientation_initial_portrait_mode_workaround);
  meta_add_monitor_test ("/backends/monitor/orientation/is-managed",
                         meta_test_monitor_orientation_is_managed);
  meta_add_monitor_test ("/backends/monitor/orientation/initial-rotated",
                         meta_test_monitor_orientation_initial_rotated);
  meta_add_monitor_test ("/backends/monitor/orientation/initial-rotated-no-touch",
                         meta_test_monitor_orientation_initial_rotated_no_touch_mode);
  meta_add_monitor_test ("/backends/monitor/orientation/initial-stored-rotated",
                         meta_test_monitor_orientation_initial_stored_rotated);
  meta_add_monitor_test ("/backends/monitor/orientation/initial-stored-rotated-no-touch",
                         meta_test_monitor_orientation_initial_stored_rotated_no_touch);
  meta_add_monitor_test ("/backends/monitor/orientation/changes",
                         meta_test_monitor_orientation_changes);
  meta_add_monitor_test ("/backends/monitor/orientation/changes-transformed-panel",
                         meta_test_monitor_orientation_changes_for_transformed_panel);
  meta_add_monitor_test ("/backends/monitor/orientation/changes-with-hotplugging",
                         meta_test_monitor_orientation_changes_with_hotplugging);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_orientation_tests);
}
