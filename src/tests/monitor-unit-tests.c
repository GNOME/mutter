/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2016 Red Hat, Inc.
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
#include "backends/meta-crtc.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor.h"
#include "backends/meta-monitor-config-store.h"
#include "backends/meta-output.h"
#include "core/window-private.h"
#include "meta-backend-test.h"
#include "meta/meta-orientation-manager.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-monitor-manager-test.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-test-utils.h"
#include "tests/monitor-tests-common.h"
#include "x11/meta-x11-display-private.h"

static void
wait_for_boolean_property (GDBusProxy *proxy,
                           const char *property_name,
                           gboolean    expected_value)
{
  g_debug ("Waiting for property '%s' to become %s on '%s'",
           property_name,
           expected_value ? "TRUE" : "FALSE",
           g_dbus_proxy_get_interface_name (proxy));

  while (TRUE)
    {
      g_autoptr (GVariant) value_variant = NULL;

      value_variant = g_dbus_proxy_get_cached_property (proxy, property_name);
      g_assert_nonnull (value_variant);

      if (g_variant_get_boolean (value_variant) == expected_value)
        break;

      g_main_context_iteration (NULL, TRUE);
    }
}

static void
proxy_ready_cb (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  GDBusProxy *proxy;
  GDBusProxy **display_config_proxy_ptr = user_data;
  g_autoptr (GError) error = NULL;

  proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
  g_assert_nonnull (proxy);
  g_assert_no_error (error);

  *display_config_proxy_ptr = proxy;
}

static void
meta_test_monitor_has_external_monitor (void)
{
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 800,
        .height = 600,
        .refresh_rate = 60.0
      },
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
        .is_laptop_panel = TRUE,
      },
      {
        .crtc = -1,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 1 },
        .n_possible_crtcs = 1,
        .width_mm = 222,
        .height_mm = 125,
        .is_laptop_panel = FALSE,
      }
    },
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = -1
      }
    },
    .n_crtcs = 2
  };
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  GList *monitors;
  MetaMonitor *first_monitor;
  MetaMonitor *second_monitor;
  g_autoptr (GDBusProxy) display_config_proxy = NULL;
  g_autoptr (GError) error = NULL;

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                            NULL,
                            "org.gnome.Mutter.DisplayConfig",
                            "/org/gnome/Mutter/DisplayConfig",
                            "org.gnome.Mutter.DisplayConfig",
                            NULL,
                            proxy_ready_cb,
                            &display_config_proxy);
  while (!display_config_proxy)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Connecting one builtin and one external monitor");

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  first_monitor = g_list_nth_data (monitors, 0);
  second_monitor = g_list_nth_data (monitors, 1);
  g_assert_true (meta_monitor_is_laptop_panel (first_monitor));
  g_assert_true (meta_monitor_is_active (first_monitor));
  g_assert_false (meta_monitor_is_laptop_panel (second_monitor));
  g_assert_true (meta_monitor_is_active (second_monitor));

  wait_for_boolean_property (G_DBUS_PROXY (display_config_proxy),
                             "HasExternalMonitor",
                             TRUE);

  g_debug ("Disconnecting external monitor");

  test_case_setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  first_monitor = g_list_nth_data (monitors, 0);
  g_assert_true (meta_monitor_is_laptop_panel (first_monitor));
  g_assert_true (meta_monitor_is_active (first_monitor));

  wait_for_boolean_property (G_DBUS_PROXY (display_config_proxy),
                             "HasExternalMonitor",
                             FALSE);

  g_debug ("Reconnect external monitor.");

  test_case_setup.n_outputs = 2;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  first_monitor = g_list_nth_data (monitors, 0);
  second_monitor = g_list_nth_data (monitors, 1);
  g_assert_true (meta_monitor_is_laptop_panel (first_monitor));
  g_assert_true (meta_monitor_is_active (first_monitor));
  g_assert_false (meta_monitor_is_laptop_panel (second_monitor));
  g_assert_true (meta_monitor_is_active (second_monitor));

  wait_for_boolean_property (G_DBUS_PROXY (display_config_proxy),
                             "HasExternalMonitor",
                             TRUE);

  g_debug ("Disable external monitor.");

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_BUILTIN);
  while (g_main_context_iteration (NULL, FALSE));

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  first_monitor = g_list_nth_data (monitors, 0);
  second_monitor = g_list_nth_data (monitors, 1);
  g_assert_true (meta_monitor_is_laptop_panel (first_monitor));
  g_assert_true (meta_monitor_is_active (first_monitor));
  g_assert_false (meta_monitor_is_laptop_panel (second_monitor));
  g_assert_false (meta_monitor_is_active (second_monitor));

  wait_for_boolean_property (G_DBUS_PROXY (display_config_proxy),
                             "HasExternalMonitor",
                             FALSE);
}

static void
meta_test_monitor_color_modes (void)
{
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 800,
        .height = 600,
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
        .serial = "0x123456",
        .supported_color_spaces = ((1 << META_OUTPUT_COLORSPACE_DEFAULT) |
                                   (1 << META_OUTPUT_COLORSPACE_BT2020)),
        .supported_hdr_eotfs =
          ((1 << META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR) |
           (1 << META_OUTPUT_HDR_METADATA_EOTF_PQ)),
      },
      {
        .crtc = -1,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 1 },
        .n_possible_crtcs = 1,
        .width_mm = 222,
        .height_mm = 125,
        .serial = "0x654321",
        .supported_color_spaces = 1 << META_OUTPUT_COLORSPACE_DEFAULT,
        .supported_hdr_eotfs =
          1 << META_OUTPUT_HDR_METADATA_EOTF_TRADITIONAL_GAMMA_SDR,
      }
    },
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = -1
      },
      {
        .current_mode = -1
      }
    },
    .n_crtcs = 2
  };
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *monitors;
  MetaMonitor *first_monitor;
  MetaMonitor *second_monitor;
  GList *color_modes;
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NONE);
  meta_emulate_hotplug (test_setup);
  meta_check_monitor_test_clients_state ();

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);

  first_monitor = g_list_nth_data (monitors, 0);
  second_monitor = g_list_nth_data (monitors, 1);

  color_modes = meta_monitor_get_supported_color_modes (first_monitor);
  g_assert_cmpuint (g_list_length (color_modes), ==, 2);
  g_assert_nonnull (g_list_find (color_modes,
                                 GINT_TO_POINTER (META_COLOR_MODE_DEFAULT)));
  g_assert_nonnull (g_list_find (color_modes,
                                 GINT_TO_POINTER (META_COLOR_MODE_BT2100)));

  color_modes = meta_monitor_get_supported_color_modes (second_monitor);
  g_assert_cmpuint (g_list_length (color_modes), ==, 1);
  g_assert_nonnull (g_list_find (color_modes,
                                 GINT_TO_POINTER (META_COLOR_MODE_DEFAULT)));
}

static void
meta_test_monitor_policy_system_only (void)
{
  MetaMonitorTestSetup *test_setup;
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1024,
          .height = 768,
          .refresh_rate = 60.0
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0
        },
        {
          .width = 640,
          .height = 480,
          .refresh_rate = 60.0
        }
      },
      .n_modes = 3,
      .outputs = {
         {
          .crtc = 0,
          .modes = { 0, 1, 2 },
          .n_modes = 3,
          .preferred_mode = 0,
          .possible_crtcs = { 0 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456",
        },
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
            },
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 1
                }
              }
            },
            {
              .width = 640,
              .height = 480,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 0,
                  .crtc_mode = 2
                }
              }
            }
          },
          .n_modes = 3,
          .current_mode = 2,
          .width_mm = 222,
          .height_mm = 125
        },
      },
      .n_monitors = 1,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 640, .height = 480 },
          .scale = 1
        },
      },
      .n_logical_monitors = 1,
      .primary_logical_monitor = 0,
      .n_outputs = 1,
      .crtcs = {
        {
          .current_mode = 2,
          .x = 0,
        }
      },
      .n_crtcs = 1,
      .screen_width = 640,
      .screen_height = 480,
    }
  };
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);

  meta_monitor_config_store_reset (config_store);
  meta_emulate_hotplug (test_setup);
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
}

static void
init_monitor_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/has-external-monitor",
                         meta_test_monitor_has_external_monitor);

  meta_add_monitor_test ("/backends/monitor/color-modes",
                         meta_test_monitor_color_modes);

  meta_add_monitor_test ("/backends/monitor/policy/system-only",
                         meta_test_monitor_policy_system_only);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_monitor_tests);
}
