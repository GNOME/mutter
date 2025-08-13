
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

#include "backends/meta-logical-monitor-private.h"
#include "tests/meta-backend-test.h"
#include "tests/monitor-tests-common.h"

static void
meta_test_monitor_rebuild_normal (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 1920,
        .height = 1080,
        .refresh_rate = 60.0
      },
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
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10000",
      },
    },
    .n_outputs = 1,
    .crtcs = {
      {
        .current_mode = -1
      },
    },
    .n_crtcs = 1
  };
  GList *monitors;
  MetaMonitor *monitor;
  MetaMonitor *new_monitor;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  monitor = META_MONITOR (monitors->data);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  g_assert_nonnull (logical_monitor);
  g_assert_true (logical_monitors->data == logical_monitor);

  /* Keep a reference and make sure another hotplug doesn't replace the monitor
   * when nothing changed. */
  g_object_ref (monitor);
  g_object_ref (logical_monitor);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  new_monitor = META_MONITOR (monitors->data);
  g_assert_true (new_monitor == monitor);
  g_assert_true (meta_monitor_get_logical_monitor (new_monitor) ==
                 logical_monitor);

  g_object_unref (monitor);
  g_object_unref (logical_monitor);

  /* Make sure the monitor is disposed if it's disconnected and replaced with
   * something else. */
  g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer *) &monitor);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor),
                             (gpointer *) &logical_monitor);

  test_case_setup.outputs[0].serial = "0x10001";
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_null (monitor);
  g_assert_null (logical_monitor);
}

static void
meta_test_monitor_rebuild_tiled (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 960,
        .height = 1080,
        .refresh_rate = 30.0
      },
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
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10000",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 0,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        }
      },
      {
        .crtc = 1,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 0 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10001",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 1,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        }
      },
    },
    .n_outputs = 2,
    .n_crtcs = 2
  };
  GList *monitors;
  MetaMonitor *monitor;
  MetaMonitor *new_monitor;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  monitor = META_MONITOR (monitors->data);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  g_assert_nonnull (logical_monitor);
  g_assert_true (logical_monitors->data == logical_monitor);

  /* Keep a reference and make sure another hotplug doesn't replace the monitor
   * when nothing changed. */
  g_object_ref (monitor);
  g_object_ref (logical_monitor);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  new_monitor = META_MONITOR (monitors->data);
  g_assert_true (new_monitor == monitor);

  g_object_unref (monitor);
  g_object_unref (logical_monitor);

  /* Make sure the monitor is disposed if it's disconnected and replaced with
   * something else. */
  g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer *) &monitor);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor),
                             (gpointer *) &logical_monitor);

  test_case_setup.outputs[0].serial = "0x10001";
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_null (monitor);
  g_assert_null (logical_monitor);
}

static void
meta_test_monitor_rebuild_detiled (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 960,
        .height = 1080,
        .refresh_rate = 30.0
      },
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
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10000",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 0,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        }
      },
      {
        .crtc = 1,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 0 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10001",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 1,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        }
      },
    },
    .n_outputs = 2,
    .n_crtcs = 2
  };
  GList *monitors;
  MetaMonitor *monitor;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  monitor = META_MONITOR (monitors->data);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  logical_monitor = meta_monitor_get_logical_monitor (monitor);
  g_assert_nonnull (logical_monitor);
  g_assert_true (logical_monitors->data == logical_monitor);

  g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer *) &monitor);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor),
                             (gpointer *) &logical_monitor);

  test_case_setup.outputs[0].tile_info = (MetaTileInfo) {};
  test_case_setup.outputs[1].tile_info = (MetaTileInfo) {};

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_null (monitor);
  g_assert_null (logical_monitor);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 2);
}

static void
meta_test_monitor_rebuild_moved (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 1920,
        .height = 1080,
        .refresh_rate = 60.0
      },
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
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10200",
        .connector_type = META_CONNECTOR_TYPE_DisplayPort,
      },
    },
    .n_outputs = 1,
    .crtcs = {
      {
        .current_mode = -1
      },
    },
    .n_crtcs = 1
  };
  GList *monitors;
  GList *logical_monitors;
  MetaMonitor *monitor;
  MetaLogicalMonitor *logical_monitor;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  monitor = META_MONITOR (monitors->data);
  logical_monitor = META_LOGICAL_MONITOR (logical_monitors->data);

  g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer *) &monitor);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor),
                             (gpointer *) &logical_monitor);

  test_case_setup.outputs[0].connector_type = META_CONNECTOR_TYPE_HDMIA;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_null (monitor);
  g_assert_null (logical_monitor);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
}

static void
meta_test_monitor_rebuild_disconnect_one (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 1920,
        .height = 1080,
        .refresh_rate = 30.0
      },
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
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10100",
      },
      {
        .crtc = 1,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 0 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10001",
      },
    },
    .n_outputs = 2,
    .n_crtcs = 2
  };
  GList *monitors;
  GList *logical_monitors;
  MetaMonitor *monitor_1;
  MetaMonitor *monitor_2;
  MetaLogicalMonitor *logical_monitor_1;
  MetaLogicalMonitor *logical_monitor_2;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  monitor_1 = META_MONITOR (monitors->data);
  monitor_2 = META_MONITOR (monitors->next->data);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 2);
  logical_monitor_1 = META_LOGICAL_MONITOR (logical_monitors->data);
  logical_monitor_2 = META_LOGICAL_MONITOR (logical_monitors->next->data);

  g_object_add_weak_pointer (G_OBJECT (monitor_1), (gpointer *) &monitor_1);
  g_object_add_weak_pointer (G_OBJECT (monitor_2), (gpointer *) &monitor_2);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor_1),
                             (gpointer *) &logical_monitor_1);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor_2),
                             (gpointer *) &logical_monitor_2);

  test_case_setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_nonnull (monitor_1);
  g_assert_null (monitor_2);
  g_assert_nonnull (logical_monitor_1);
  g_assert_null (logical_monitor_2);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  g_assert_true (monitors->data == monitor_1);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  g_assert_true (logical_monitors->data == logical_monitor_1);

  test_case_setup.n_outputs = 2;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_nonnull (monitor_1);
  g_assert_nonnull (logical_monitor_1);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  g_assert_true (monitors->data == monitor_1);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 2);
  g_assert_true (logical_monitors->data == logical_monitor_1);

  g_object_remove_weak_pointer (G_OBJECT (monitor_1), (gpointer *) &monitor_1);
  g_object_remove_weak_pointer (G_OBJECT (logical_monitor_1),
                                (gpointer *) &logical_monitor_1);
}

static gboolean
check_monitor_mode (MetaMonitor         *monitor,
                    MetaMonitorMode     *mode,
                    MetaMonitorCrtcMode *monitor_crtc_mode,
                    gpointer             user_data,
                    GError             **error)
{
  GList **l = user_data;
  MetaOutput *output = META_OUTPUT ((*l)->data);

  g_assert_nonnull (*l);
  *l = (*l)->next;

  g_assert_true (monitor_crtc_mode->output == output);
  return TRUE;
}

static void
verify_monitor_monitor_mode (MetaMonitor     *monitor,
                             MetaMonitorMode *monitor_mode)
{
  GList *outputs;

  g_assert_nonnull (monitor_mode);
  g_assert_true (meta_monitor_mode_get_monitor (monitor_mode) == monitor);

  outputs = meta_monitor_get_outputs (monitor);
  meta_monitor_mode_foreach_output (monitor, monitor_mode,
                                    check_monitor_mode,
                                    &outputs,
                                    NULL);
}

static void
meta_test_monitor_rebuild_disable (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 1920,
        .height = 1080,
        .refresh_rate = 30.0
      },
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
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x30000",
        .connector_type = META_CONNECTOR_TYPE_eDP,
      },
      {
        .crtc = 1,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 0 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x30001",
        .connector_type = META_CONNECTOR_TYPE_DisplayPort,
      },
    },
    .n_outputs = 2,
    .n_crtcs = 2
  };
  GList *monitors;
  MetaMonitor *monitor_1;
  MetaMonitor *monitor_2;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor_1;
  MetaLogicalMonitor *logical_monitor_2;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  monitor_1 = META_MONITOR (monitors->data);
  monitor_2 = META_MONITOR (monitors->next->data);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 2);
  logical_monitor_1 = META_LOGICAL_MONITOR (logical_monitors->data);
  logical_monitor_2 = META_LOGICAL_MONITOR (logical_monitors->next->data);

  g_object_add_weak_pointer (G_OBJECT (monitor_1), (gpointer *) &monitor_1);
  g_object_add_weak_pointer (G_OBJECT (monitor_2), (gpointer *) &monitor_2);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor_1),
                             (gpointer *) &logical_monitor_1);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor_2),
                             (gpointer *) &logical_monitor_2);

  verify_monitor_monitor_mode (monitor_1,
                               meta_monitor_get_current_mode (monitor_1));
  verify_monitor_monitor_mode (monitor_2,
                               meta_monitor_get_current_mode (monitor_2));

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_BUILTIN);
  while (g_main_context_iteration (NULL, FALSE));
  g_assert_nonnull (monitor_1);
  g_assert_nonnull (monitor_2);

  g_assert_nonnull (logical_monitor_1);
  g_assert_null (logical_monitor_2);

  verify_monitor_monitor_mode (monitor_1,
                               meta_monitor_get_current_mode (monitor_1));
  g_assert_null (meta_monitor_get_current_mode (monitor_2));

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  g_assert_true (logical_monitors->data == logical_monitor_1);

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_EXTERNAL);
  while (g_main_context_iteration (NULL, FALSE));
  g_assert_nonnull (monitor_1);
  g_assert_nonnull (monitor_2);

  g_assert_null (logical_monitor_1);

  g_assert_null (meta_monitor_get_current_mode (monitor_1));
  verify_monitor_monitor_mode (monitor_2,
                               meta_monitor_get_current_mode (monitor_2));

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);

  g_object_remove_weak_pointer (G_OBJECT (monitor_1), (gpointer *) &monitor_1);
  g_object_remove_weak_pointer (G_OBJECT (monitor_2), (gpointer *) &monitor_2);
}

static void
meta_test_monitor_rebuild_preferred_mode (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 1920,
        .height = 1080,
        .refresh_rate = 60.0
      },
      {
        .width = 960,
        .height = 1080,
        .refresh_rate = 144.0
      },
      {
        .width = 1920,
        .height = 1080,
        .refresh_rate = 30.0
      },
      {
        .width = 960,
        .height = 1080,
        .refresh_rate = 120.0
      },
    },
    .n_modes = 4,
    .outputs = {
      {
        .crtc = 0,
        .modes = { 0, 2 },
        .n_modes = 2,
        .preferred_mode = 0,
        .possible_crtcs = { 0 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10000",
      },
      {
        .crtc = 0,
        .modes = { 1, 3 },
        .n_modes = 2,
        .preferred_mode = 1,
        .possible_crtcs = { 1 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10001",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 0,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        }
      },
      {
        .crtc = 1,
        .modes = { 1, 3 },
        .n_modes = 2,
        .preferred_mode = 0,
        .possible_crtcs = { 2 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10002",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 1,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        }
      },
    },
    .n_outputs = 3,
    .n_crtcs = 3
  };
  GList *monitors;
  MetaMonitor *monitor_1;
  MetaMonitor *monitor_2;
  MetaMonitorMode *monitor_mode_1;
  MetaMonitorMode *monitor_mode_2;
  g_autofree char *monitor_mode_id_1 = NULL;
  g_autofree char *monitor_mode_id_2 = NULL;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  monitor_1 = META_MONITOR (monitors->data);
  monitor_2 = META_MONITOR (monitors->next->data);

  monitor_mode_1 = meta_monitor_get_preferred_mode (monitor_1);
  monitor_mode_id_1 = g_strdup (meta_monitor_mode_get_id (monitor_mode_1));
  monitor_mode_2 = meta_monitor_get_preferred_mode (monitor_2);
  monitor_mode_id_2 = g_strdup (meta_monitor_mode_get_id (monitor_mode_2));

  g_object_ref (monitor_1);
  g_object_ref (monitor_2);

  test_case_setup.outputs[0].preferred_mode = 2;
  test_case_setup.outputs[1].preferred_mode = 3;
  test_case_setup.outputs[2].preferred_mode = 3;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  g_assert_true (monitor_1 == META_MONITOR (monitors->data));
  g_assert_true (monitor_2 == META_MONITOR (monitors->next->data));

  monitor_mode_1 = meta_monitor_get_preferred_mode (monitor_1);
  monitor_mode_2 = meta_monitor_get_preferred_mode (monitor_2);

  g_assert_cmpstr (monitor_mode_id_1,
                   !=,
                   meta_monitor_mode_get_id (monitor_mode_1));
  g_assert_cmpstr (monitor_mode_id_2,
                   !=,
                   meta_monitor_mode_get_id (monitor_mode_2));

  g_object_unref (monitor_1);
  g_object_unref (monitor_2);
}

static void
meta_test_monitor_rebuild_changed_connector (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 1920,
        .height = 1080,
        .refresh_rate = 60.0
      },
      {
        .width = 960,
        .height = 1080,
        .refresh_rate = 144.0
      },
    },
    .n_modes = 2,
    .outputs = {
      {
        .crtc = 0,
        .modes = { 0, },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 0 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10000",
        .connector_number = 1,
      },
      {
        .crtc = 0,
        .modes = { 1, },
        .n_modes = 1,
        .preferred_mode = 1,
        .possible_crtcs = { 1 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10001",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 0,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        },
        .connector_number = 2,
      },
      {
        .crtc = 1,
        .modes = { 1, },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 2 },
        .n_possible_crtcs = 1,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x10002",
        .tile_info = {
          .group_id = 1,
          .max_h_tiles = 2,
          .max_v_tiles = 1,
          .loc_h_tile = 1,
          .loc_v_tile = 0,
          .tile_w = 960,
          .tile_h = 1080
        },
        .connector_number = 3,
      },
    },
    .n_outputs = 3,
    .n_crtcs = 3
  };
  GList *monitors;
  MetaMonitor *monitor_1;
  MetaMonitor *monitor_2;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor_1;
  MetaLogicalMonitor *logical_monitor_2;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  monitor_1 = META_MONITOR (monitors->data);
  monitor_2 = META_MONITOR (monitors->next->data);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 2);
  logical_monitor_1 = META_LOGICAL_MONITOR (logical_monitors->data);
  logical_monitor_2 = META_LOGICAL_MONITOR (logical_monitors->next->data);

  g_object_add_weak_pointer (G_OBJECT (monitor_1), (gpointer *) &monitor_1);
  g_object_add_weak_pointer (G_OBJECT (monitor_2), (gpointer *) &monitor_2);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor_1),
                             (gpointer *) &logical_monitor_1);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor_2),
                             (gpointer *) &logical_monitor_2);

  test_case_setup.outputs[0].connector_number = 2;
  test_case_setup.outputs[1].connector_number = 3;
  test_case_setup.outputs[2].connector_number = 4;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  g_assert_null (monitor_1);
  g_assert_null (monitor_2);
  g_assert_null (logical_monitor_1);
  g_assert_null (logical_monitor_2);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
}

static void
meta_test_monitor_rebuild_mirror (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorTestSetup *test_setup;
  MonitorTestCaseSetup test_case_setup = {
    .modes = {
      {
        .width = 1920,
        .height = 1080,
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
        .possible_crtcs = { 0, 1 },
        .n_possible_crtcs = 2,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x40000",
      },
      {
        .crtc = -1,
        .modes = { 0 },
        .n_modes = 1,
        .preferred_mode = 0,
        .possible_crtcs = { 0, 1 },
        .n_possible_crtcs = 2,
        .width_mm = 150,
        .height_mm = 85,
        .serial = "0x40001",
      },
    },
    .n_outputs = 2,
    .crtcs = {
      {
        .current_mode = -1
      },
    },
    .n_crtcs = 2
  };
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor_1;
  MetaLogicalMonitor *logical_monitor_2;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 2);
  logical_monitor_1 = META_LOGICAL_MONITOR (logical_monitors->data);
  logical_monitor_2 = META_LOGICAL_MONITOR (logical_monitors->next->data);

  g_object_add_weak_pointer (G_OBJECT (logical_monitor_1),
                             (gpointer *) &logical_monitor_1);
  g_object_add_weak_pointer (G_OBJECT (logical_monitor_2),
                             (gpointer *) &logical_monitor_2);

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_ALL_MIRROR);
  while (g_main_context_iteration (NULL, FALSE));

  g_assert_null (logical_monitor_1);
  g_assert_null (logical_monitor_2);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 1);
  logical_monitor_1 = META_LOGICAL_MONITOR (logical_monitors->data);

  g_object_add_weak_pointer (G_OBJECT (logical_monitor_1),
                             (gpointer *) &logical_monitor_1);

  meta_monitor_manager_switch_config (monitor_manager,
                                      META_MONITOR_SWITCH_CONFIG_ALL_LINEAR);
  while (g_main_context_iteration (NULL, FALSE));

  g_assert_null (logical_monitor_1);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (logical_monitors), ==, 2);
}

static void
init_abstraction_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/rebuild/normal",
                         meta_test_monitor_rebuild_normal);
  meta_add_monitor_test ("/backends/monitor/rebuild/tiled",
                         meta_test_monitor_rebuild_tiled);
  meta_add_monitor_test ("/backends/monitor/rebuild/detiled",
                         meta_test_monitor_rebuild_detiled);
  meta_add_monitor_test ("/backends/monitor/rebuild/moved",
                         meta_test_monitor_rebuild_moved);
  meta_add_monitor_test ("/backends/monitor/rebuild/disconnect-one",
                         meta_test_monitor_rebuild_disconnect_one);
  meta_add_monitor_test ("/backends/monitor/rebuild/disable",
                         meta_test_monitor_rebuild_disable);
  meta_add_monitor_test ("/backends/monitor/rebuild/preferred-mode",
                         meta_test_monitor_rebuild_preferred_mode);
  meta_add_monitor_test ("/backends/monitor/rebuild/changed-connector",
                         meta_test_monitor_rebuild_changed_connector);
  meta_add_monitor_test ("/backends/monitor/rebuild/mirror",
                         meta_test_monitor_rebuild_mirror);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_abstraction_tests);
}
