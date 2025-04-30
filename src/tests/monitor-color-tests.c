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

#include "tests/monitor-tests-common.h"

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
init_color_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/color-modes",
                         meta_test_monitor_color_modes);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_color_tests);
}
