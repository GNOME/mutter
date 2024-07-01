/*
 * Copyright (C) 2025 Red Hat, Inc.
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

#include <stdio.h>

#include "tests/meta-test/meta-context-test.h"
#include "tests/meta-monitor-test-utils.h"

#include "meta-dbus-display-config.h"

static MonitorTestCaseSetup initial_test_case_setup = {
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
      .is_laptop_panel = TRUE,
      .backlight_min = 0,
      .backlight_max = 300,
    },
    {
      .crtc = 1,
      .modes = { 0 },
      .n_modes = 1,
      .preferred_mode = 0,
      .possible_crtcs = { 1 },
      .n_possible_crtcs = 1,
      .width_mm = 220,
      .height_mm = 124
    }
  },
  .n_outputs = 2,
  .crtcs = {
    {
      .current_mode = 0
    },
    {
      .current_mode = 0
    }
  },
  .n_crtcs = 2
};

static MetaContext *test_context;

static MetaMonitorTestSetup *
create_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend,
                                         &initial_test_case_setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

static void
meta_test_backlight_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *monitors;
  MetaMonitor *first_monitor;
  MetaMonitor *second_monitor;
  MetaOutput *output;
  const MetaOutputInfo *output_info;
  int backlight_min;
  int backlight_max;
  int backlight;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);

  first_monitor = g_list_nth_data (monitors, 0);
  second_monitor = g_list_nth_data (monitors, 1);

  g_assert_true (meta_monitor_get_backlight_info (first_monitor,
                                                  &backlight_min,
                                                  &backlight_max));
  g_assert_cmpint (backlight_min, ==, 0);
  g_assert_cmpint (backlight_max, ==, 300);
  g_assert_true (meta_monitor_get_backlight (first_monitor, &backlight));
  g_assert_cmpint (backlight, >=, 0);
  g_assert_cmpuint (g_list_length (meta_monitor_get_outputs (first_monitor)),
                    ==,
                    1);
  output = meta_monitor_get_main_output (first_monitor);
  output_info = meta_output_get_info (output);
  g_assert_cmpint (meta_output_get_backlight (output), >=, 0);
  g_assert_cmpint (output_info->backlight_min, ==, 0);
  g_assert_cmpint (output_info->backlight_max, ==, 300);

  g_assert_false (meta_monitor_get_backlight_info (second_monitor,
                                                   NULL,
                                                   NULL));
  g_assert_false (meta_monitor_get_backlight (second_monitor, NULL));
  g_assert_cmpuint (g_list_length (meta_monitor_get_outputs (second_monitor)),
                    ==,
                    1);
  output = meta_monitor_get_main_output (second_monitor);
  output_info = meta_output_get_info (output);
  g_assert_cmpint (meta_output_get_backlight (output), ==, -1);
  g_assert_cmpint (output_info->backlight_min, ==, 0);
  g_assert_cmpint (output_info->backlight_max, ==, 0);
}

static char *
get_test_client_path (const char *test_client_name)
{
  return g_test_build_filename (G_TEST_BUILT,
                                test_client_name,
                                NULL);
}

static void
subprocess_wait_check_cb (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  gboolean *exited = user_data;

  g_assert_true (g_subprocess_wait_check_finish (G_SUBPROCESS (source_object),
                                                 res,
                                                 &error));
  g_assert_no_error (error);

  *exited = TRUE;
}

static void
meta_test_backlight_api (void)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *test_client_path = NULL;
  gboolean exited = FALSE;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher,
                                "G_MESSAGES_DEBUG", "all",
                                TRUE);
  g_subprocess_launcher_setenv (launcher,
                                "G_DEBUG", "fatal-warnings",
                                TRUE);
  test_client_path = get_test_client_path ("monitor-backlight-client");
  subprocess =
    g_subprocess_launcher_spawn (launcher,
                                 &error,
                                 test_client_path,
                                 NULL);
  g_assert_no_error (error);
  g_assert_nonnull (subprocess);

  g_subprocess_wait_check_async (subprocess, NULL,
                                 subprocess_wait_check_cb,
                                 &exited);
  while (!exited)
    g_main_context_iteration (NULL, TRUE);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  meta_init_monitor_test_setup (create_test_setup);
  g_test_add_func ("/backends/backlight/sanity", meta_test_backlight_sanity);
  g_test_add_func ("/backends/backlight/api", meta_test_backlight_api);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
