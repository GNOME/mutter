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

#include "tests/monitor-tests-common.h"

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
        .connector_type = META_CONNECTOR_TYPE_eDP,
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
        .connector_type = META_CONNECTOR_TYPE_DisplayPort,
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
  g_assert_true (meta_monitor_is_builtin (first_monitor));
  g_assert_true (meta_monitor_is_active (first_monitor));
  g_assert_false (meta_monitor_is_builtin (second_monitor));
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
  g_assert_true (meta_monitor_is_builtin (first_monitor));
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
  g_assert_true (meta_monitor_is_builtin (first_monitor));
  g_assert_true (meta_monitor_is_active (first_monitor));
  g_assert_false (meta_monitor_is_builtin (second_monitor));
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
  g_assert_true (meta_monitor_is_builtin (first_monitor));
  g_assert_true (meta_monitor_is_active (first_monitor));
  g_assert_false (meta_monitor_is_builtin (second_monitor));
  g_assert_false (meta_monitor_is_active (second_monitor));

  wait_for_boolean_property (G_DBUS_PROXY (display_config_proxy),
                             "HasExternalMonitor",
                             FALSE);
}

static void
init_dbus_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/has-external-monitor",
                         meta_test_monitor_has_external_monitor);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_dbus_tests);
}
