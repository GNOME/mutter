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
meta_test_monitor_migrated_rotated (void)
{
  MonitorTestCase test_case = {
    .setup = {
      .modes = {
        {
          .width = 1920,
          .height = 1080,
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
          .serial = "0x123456a",
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
          .serial = "0x123456b",
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 2 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456c",
        }
      },
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = 0
        },
        {
          .current_mode = 0
        },
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 3
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
              .width = 1920,
              .height = 1080,
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
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 2 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 1920,
              .height = 1080,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 2,
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
      .n_monitors = 3,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 0, .width = 1080, .height = 1920 },
          .scale = 1,
          .transform = MTK_MONITOR_TRANSFORM_270
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 1080, .y = 0, .width = 960, .height = 540 },
          .scale = 2
        },
        {
          .monitors = { 2 },
          .n_monitors = 1,
          .layout = { .x = 600, .y = 1920, .width = 1920, .height = 1080 },
          .scale = 1
        }
      },
      .n_logical_monitors = 3,
      .primary_logical_monitor = 0,
      .n_outputs = 3,
      .crtcs = {
        {
          .current_mode = 0,
          .transform = MTK_MONITOR_TRANSFORM_270
        },
        {
          .current_mode = 0,
          .x = 1080
        },
        {
          .current_mode = 0,
          .x = 600,
          .y = 1920
        }
      },
      .n_crtcs = 3,
      .screen_width = 2520,
      .screen_height = 3000,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  g_autofree char *old_config_path = NULL;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);

  old_config_path = g_test_build_filename (G_TEST_DIST,
                                           "migration",
                                           "rotated.xml",
                                           NULL);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);

  if (!meta_monitor_config_store_set_custom (config_store,
                                             old_config_path,
                                             migrated_path,
                                             META_MONITORS_CONFIG_FLAG_NONE,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "migration",
                                       "rotated-finished.xml",
                                       NULL);
  expected_data = meta_read_file (expected_path);
  migrated_data = meta_read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert_cmpint (strcmp (expected_data, migrated_data), ==, 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static void
meta_test_monitor_migrated_horizontal_strip (void)
{
  MonitorTestCase test_case = {
    .setup = {
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
          .serial = "0x123456a",
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
          .serial = "0x123456b",
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 2 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456c",
        },
        {
          .crtc = -1,
          .modes = { 0 },
          .n_modes = 1,
          .preferred_mode = 0,
          .possible_crtcs = { 3 },
          .n_possible_crtcs = 1,
          .width_mm = 222,
          .height_mm = 125,
          .serial = "0x123456d",
        }
      },
      .n_outputs = 4,
      .crtcs = {
        {
          .current_mode = 0
        },
        {
          .current_mode = 0
        },
        {
          .current_mode = 0
        },
        {
          .current_mode = 0
        }
      },
      .n_crtcs = 4
    },

    .expect = {
      .monitors = {
        {
          .outputs = { 0 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
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
              .width = 800,
              .height = 600,
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
          .width_mm = 222,
          .height_mm = 125
        },
        {
          .outputs = { 2 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 2,
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
          .outputs = { 3 },
          .n_outputs = 1,
          .modes = {
            {
              .width = 800,
              .height = 600,
              .refresh_rate = 60.0,
              .crtc_modes = {
                {
                  .output = 3,
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
      .n_monitors = 4,
      .logical_monitors = {
        {
          .monitors = { 0 },
          .n_monitors = 1,
          .layout = { .x = 0, .y = 150, .width = 400, .height = 300 },
          .scale = 2
        },
        {
          .monitors = { 1 },
          .n_monitors = 1,
          .layout = { .x = 400, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        },
        {
          .monitors = { 2 },
          .n_monitors = 1,
          .layout = { .x = 1200, .y = 199, .width = 268, .height = 201 },
          .scale = 2.985074520111084
        },
        {
          .monitors = { 3 },
          .n_monitors = 1,
          .layout = { .x = 1468, .y = 0, .width = 800, .height = 600 },
          .scale = 1
        }
      },
      .n_logical_monitors = 4,
      .primary_logical_monitor = 2,
      .n_outputs = 4,
      .crtcs = {
        {
          .current_mode = 0,
          .y = 150
        },
        {
          .current_mode = 0,
          .x = 400
        },
        {
          .current_mode = 0,
          .x = 1200,
          .y = 199
        },
        {
          .current_mode = 0,
          .x = 1468
        }
      },
      .n_crtcs = 4,
      .screen_width = 2268,
      .screen_height = 600,
    }
  };
  MetaMonitorTestSetup *test_setup;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager = monitor_manager->config_manager;
  MetaMonitorConfigStore *config_store =
    meta_monitor_config_manager_get_store (config_manager);
  g_autofree char *migrated_path = NULL;
  g_autofree char *old_config_path = NULL;
  g_autoptr (GFile) old_config_file = NULL;
  GError *error = NULL;
  const char *expected_path;
  g_autofree char *migrated_data = NULL;
  g_autofree char *expected_data = NULL;
  g_autoptr (GFile) migrated_file = NULL;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NONE);

  old_config_path = g_test_build_filename (G_TEST_DIST,
                                           "migration",
                                           "horizontal-strip.xml",
                                           NULL);

  migrated_path = g_build_filename (g_get_tmp_dir (),
                                    "test-finished-migrated-monitors.xml",
                                    NULL);

  if (!meta_monitor_config_store_set_custom (config_store,
                                             old_config_path,
                                             migrated_path,
                                             META_MONITORS_CONFIG_FLAG_NONE,
                                             &error))
    g_error ("Failed to set custom config store files: %s", error->message);

  meta_emulate_hotplug (test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case.expect));
  meta_check_monitor_test_clients_state ();

  expected_path = g_test_get_filename (G_TEST_DIST,
                                       "migration",
                                       "horizontal-strip-finished.xml",
                                       NULL);
  expected_data = meta_read_file (expected_path);
  migrated_data = meta_read_file (migrated_path);

  g_assert_nonnull (expected_data);
  g_assert_nonnull (migrated_data);

  g_assert_cmpint (strcmp (expected_data, migrated_data), ==, 0);

  migrated_file = g_file_new_for_path (migrated_path);
  if (!g_file_delete (migrated_file, NULL, &error))
    g_error ("Failed to remove test data output file: %s", error->message);
}

static gboolean
quit_main_loop (gpointer data)
{
  GMainLoop *loop = data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
dispatch (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                   quit_main_loop,
                   loop,
                   NULL);
  g_main_loop_run (loop);
}

static MetaTestClient *
create_test_window (MetaContext *context,
                    const char  *window_name)
{
  MetaTestClient *test_client;
  static int client_count = 0;
  g_autofree char *client_name = NULL;
  g_autoptr (GError) error = NULL;

  client_name = g_strdup_printf ("test_client_%d", client_count++);
  test_client = meta_test_client_new (context,
                                      client_name, META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  if (!meta_test_client_do (test_client, &error,
                            "create", window_name,
                            NULL))
    g_error ("Failed to create window: %s", error->message);

  return test_client;
}

static void
meta_test_monitor_wm_tiling (void)
{
  MetaContext *context = test_context;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  /*
   * 1) Start with two monitors connected.
   * 2) Tile it on the second monitor.
   * 3) Unplug both monitors.
   * 4) Replug in first monitor.
   */

  const char *test_window_name= "window1";
  test_client = create_test_window (context, test_window_name);

  if (!meta_test_client_do (test_client, &error,
                            "show", test_window_name,
                            NULL))
    g_error ("Failed to show the window: %s", error->message);

  MetaWindow *test_window =
    meta_test_client_find_window (test_client,
                                  test_window_name,
                                  &error);
  if (!test_window)
    g_error ("Failed to find the window: %s", error->message);
  meta_wait_for_window_shown (test_window);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);
  meta_window_move_to_monitor (test_window, 1);
  meta_check_test_client_state (test_client);

  test_case.setup.n_outputs = 0;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
  test_case.setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  dispatch ();

  /*
   * 1) Start with two monitors connected.
   * 2) Tile a window on the second monitor.
   * 3) Untile window.
   * 4) Unplug monitor.
   * 5) Tile window again.
   */

  test_case.setup.n_outputs = 2;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  meta_window_move_to_monitor (test_window, 1);
  meta_window_tile (test_window, META_TILE_NONE);

  test_case.setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);

  meta_test_client_destroy (test_client);
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

  meta_add_monitor_test ("/backends/monitor/migrated/rotated",
                         meta_test_monitor_migrated_rotated);
  meta_add_monitor_test ("/backends/monitor/migrated/horizontal-strip",
                         meta_test_monitor_migrated_horizontal_strip);

  meta_add_monitor_test ("/backends/monitor/wm/tiling",
                         meta_test_monitor_wm_tiling);

  meta_add_monitor_test ("/backends/monitor/policy/system-only",
                         meta_test_monitor_policy_system_only);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_monitor_tests);
}
