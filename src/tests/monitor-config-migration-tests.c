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

#include "backends/meta-monitor-config-store.h"
#include "tests/monitor-tests-common.h"

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
          .layout = { .x = 1200, .y = 150, .width = 400, .height = 300 },
          .scale = 2
        },
        {
          .monitors = { 3 },
          .n_monitors = 1,
          .layout = { .x = 1600, .y = 0, .width = 800, .height = 600 },
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
          .y = 150
        },
        {
          .current_mode = 0,
          .x = 1600
        }
      },
      .n_crtcs = 4,
      .screen_width = 2400,
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

static void
init_config_migration_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/migrated/rotated",
                         meta_test_monitor_migrated_rotated);
  meta_add_monitor_test ("/backends/monitor/migrated/horizontal-strip",
                         meta_test_monitor_migrated_horizontal_strip);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_config_migration_tests);
}
