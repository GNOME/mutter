/*
 * Copyright (C) 2024 Red Hat Inc.
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
 *
 */

#include "config.h"

#include <stdarg.h>

#include "backends/meta-monitor-config-manager.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-test/meta-context-test.h"

static MetaContext *test_context;
static char *gdctl_path;

static MonitorTestCaseSetup test_case_setup = {
  .modes = {
    {
      .width = 3840,
      .height = 2160,
      .refresh_rate = 60.0
    },
    {
      .width = 3840,
      .height = 2160,
      .refresh_rate = 30.0
    },
    {
      .width = 2560,
      .height = 1440,
      .refresh_rate = 60.0
    },
    {
      .width = 1440,
      .height = 900,
      .refresh_rate = 60.0
    },
    {
      .width = 1366,
      .height = 768,
      .refresh_rate = 60.0
    },
    {
      .width = 800,
      .height = 600,
      .refresh_rate = 60.0
    },
  },
  .n_modes = 6,
  .outputs = {
    {
      .crtc = 0,
      .modes = { 0, 1, 2, 3 },
      .n_modes = 4,
      .preferred_mode = 0,
      .possible_crtcs = { 0 },
      .n_possible_crtcs = 1,
      .width_mm = 300,
      .height_mm = 190,
      .dynamic_scale = TRUE,
    },
    {
      .crtc = 1,
      .modes = { 2, 3, 4, 5 },
      .n_modes = 4,
      .preferred_mode = 2,
      .possible_crtcs = { 1 },
      .n_possible_crtcs = 1,
      .width_mm = 290,
      .height_mm = 180,
      .dynamic_scale = TRUE,
    },
  },
  .n_outputs = 2,
  .n_crtcs = 2
};

static MonitorTestCaseExpect test_case_expect = {
  .monitors = {
    {
      .outputs = { 0 },
      .n_outputs = 1,
      .modes = {
        {
          .width = 3840,
          .height = 2160,
          .refresh_rate = 60.0,
          .crtc_modes = {
            {
              .output = 0,
              .crtc_mode = 0,
            },
          },
        },
        {
          .width = 3840,
          .height = 2160,
          .refresh_rate = 30.0,
          .crtc_modes = {
            {
              .output = 0,
              .crtc_mode = 1,
            },
          },
        },
        {
          .width = 2560,
          .height = 1440,
          .refresh_rate = 60.0,
          .crtc_modes = {
            {
              .output = 0,
              .crtc_mode = 2,
            },
          },
        },
        {
          .width = 1440,
          .height = 900,
          .refresh_rate = 60.0,
          .crtc_modes = {
            {
              .output = 0,
              .crtc_mode = 3,
            },
          },
        },
      },
      .n_modes = 4,
      .current_mode = 0,
      .width_mm = 300,
      .height_mm = 190,
    },
    {
      .outputs = { 1 },
      .n_outputs = 1,
      .modes = {
        {
          .width = 2560,
          .height = 1440,
          .refresh_rate = 60.0,
          .crtc_modes = {
            {
              .output = 1,
              .crtc_mode = 2,
            },
          },
        },
        {
          .width = 1440,
          .height = 900,
          .refresh_rate = 60.0,
          .crtc_modes = {
            {
              .output = 1,
              .crtc_mode = 3,
            },
          },
        },
        {
          .width = 1366,
          .height = 768,
          .refresh_rate = 60.0,
          .crtc_modes = {
            {
              .output = 1,
              .crtc_mode = 4,
            },
          },
        },
        {
          .width = 800,
          .height = 600,
          .refresh_rate = 60.0,
          .crtc_modes = {
            {
              .output = 1,
              .crtc_mode = 5,
            },
          },
        },
      },
      .n_modes = 4,
      .current_mode = 0,
      .width_mm = 290,
      .height_mm = 180,
    },
  },
  .n_monitors = 2,
  .logical_monitors = {
    {
      .monitors = { 0 },
      .n_monitors = 1,
      .layout = { .x = 0, .y = 0, .width = 1536, .height = 864 },
      .scale = 2.5,
    },
    {
      .monitors = { 1 },
      .n_monitors = 1,
      .layout = { .x = 1536, .y = 0, .width = 1536, .height = 864},
      .scale = 1.6666666269302368,
    },
  },
  .n_logical_monitors = 2,
  .primary_logical_monitor = 0,
  .n_outputs = 2,
  .crtcs = {
    {
      .current_mode = 0,
    },
    {
      .current_mode = 2,
      .x = 1536,
    }
  },
  .n_crtcs = 2,
  .screen_width = 3072,
  .screen_height = 864,
};

static void
read_all_cb (GObject      *source_object,
             GAsyncResult *res,
             gpointer      user_data)
{
  gboolean *done = user_data;
  g_autoptr (GError) error = NULL;

  g_input_stream_read_all_finish (G_INPUT_STREAM (source_object),
                                  res,
                                  NULL,
                                  &error);
  g_assert_no_error (error);

  *done = TRUE;
}

typedef struct
{
  gboolean done;
  GError *error;
} WaitCheckData;

static void
wait_check_data_clear (WaitCheckData *data)
{
  g_clear_error (&data->error);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (WaitCheckData, wait_check_data_clear);

static void
wait_check_cb (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
  WaitCheckData *data = user_data;
  g_autoptr (GError) error = NULL;

  g_subprocess_wait_check_finish (G_SUBPROCESS (source_object), res, &error);

  data->done = TRUE;
  data->error = g_steal_pointer (&error);
}

static char *
save_output (const char *output,
             const char *expected_output_file)
{
  const char *gdctl_result_dir;
  char *output_path;
  g_autoptr (GError) error = NULL;

  gdctl_result_dir = g_getenv ("MUTTER_GDCTL_TEST_RESULT_DIR");
  g_assert_no_errno (g_mkdir_with_parents (gdctl_result_dir, 0755));

  output_path = g_strdup_printf ("%s/%s",
                                 gdctl_result_dir,
                                 expected_output_file);

  g_file_set_contents (output_path, output, -1, &error);
  g_assert_no_error (error);

  return output_path;
}

static void
run_diff (const char *output_path,
          const char *expected_output_path)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autoptr (GError) error = NULL;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            "diff",
                                            "-u",
                                            expected_output_path,
                                            output_path,
                                            NULL);
  g_subprocess_wait (subprocess, NULL, &error);
  g_assert_no_error (error);
}

static void
check_gdctl_result (const char *first_argument,
                    ...)
{
  g_autoptr (GPtrArray) args = NULL;
  va_list va_args;
  char *arg;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autoptr (GError) error = NULL;
  g_auto (WaitCheckData) wait_data = {0};

  args = g_ptr_array_new ();
  g_ptr_array_add (args, gdctl_path);
  g_ptr_array_add (args, (char *) first_argument);
  va_start (va_args, first_argument);
  while ((arg = va_arg (va_args, char *)))
    g_ptr_array_add (args, arg);
  va_end (va_args);
  g_ptr_array_add (args, NULL);

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

  subprocess = g_subprocess_launcher_spawnv (launcher,
                                             (const char * const*) args->pdata,
                                             &error);
  g_subprocess_wait_check_async (subprocess, NULL,
                                 wait_check_cb, &wait_data);

  while (!wait_data.done)
    g_main_context_iteration (NULL, TRUE);

  g_assert_no_error (wait_data.error);
}

static void
check_gdctl_output (const char *expected_output_file,
                    ...)
{
  g_autoptr (GPtrArray) args = NULL;
  va_list va_args;
  char *arg;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) subprocess = NULL;
  GInputStream *stdout_pipe;
  size_t max_output_size;
  g_autofree char *output = NULL;
  gboolean read_done = FALSE;
  g_autoptr (GError) error = NULL;
  g_autofree char *expected_output_path = NULL;
  g_autofree char *expected_output = NULL;
  g_auto (WaitCheckData) wait_data = {0};

  args = g_ptr_array_new ();
  g_ptr_array_add (args, gdctl_path);
  va_start (va_args, expected_output_file);
  while ((arg = va_arg (va_args, char *)))
    g_ptr_array_add (args, arg);
  va_end (va_args);
  g_ptr_array_add (args, NULL);

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                        G_SUBPROCESS_FLAGS_STDERR_MERGE);

  subprocess = g_subprocess_launcher_spawnv (launcher,
                                             (const char * const*) args->pdata,
                                             &error);
  stdout_pipe = g_subprocess_get_stdout_pipe (subprocess);
  max_output_size = 1024 * 1024;
  output = g_malloc0 (max_output_size);
  g_input_stream_read_all_async (stdout_pipe,
                                 output,
                                 max_output_size - 1,
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 read_all_cb,
                                 &read_done);

  g_subprocess_wait_check_async (subprocess, NULL,
                                 wait_check_cb, &wait_data);

  while (!read_done || !wait_data.done)
    g_main_context_iteration (NULL, TRUE);

  g_test_message ("%s", output);
  g_assert_no_error (wait_data.error);

  expected_output_path = g_test_build_filename (G_TEST_DIST,
                                                "gdctl",
                                                expected_output_file,
                                                NULL);
  g_file_get_contents (expected_output_path,
                       &expected_output,
                       NULL,
                       &error);
  g_assert_no_error (error);

  if (g_strcmp0 (expected_output, output) != 0)
    {
      g_autofree char *output_path = NULL;

      output_path = save_output (output, expected_output_file);
      run_diff (output_path, expected_output_path);
      g_error ("Incorrect gdctl output");
    }
}

static void
meta_test_monitor_dbus_get_state (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  check_gdctl_output ("show",
                      "show", NULL);
  check_gdctl_output ("show-properties",
                      "show", "--properties", NULL);
  check_gdctl_output ("show-modes",
                      "show", "--modes", NULL);
  check_gdctl_output ("show-verbose",
                      "show", "--verbose", NULL);
}

static void
meta_test_monitor_dbus_apply_verify (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager =
    meta_monitor_manager_get_config_manager (monitor_manager);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorTestSetup *test_setup;
  MetaMonitorsConfig *config;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  config = meta_monitor_config_manager_get_current (config_manager);

  check_gdctl_result ("set",
                      "--verbose",
                      "--verify",
                      "--layout-mode", "logical",
                      "--logical-monitor",
                      "--primary",
                      "--monitor", "DP-1",
                      "--logical-monitor",
                      "--monitor", "DP-2",
                      "--right-of", "DP-1",
                      NULL);
  g_assert_true (config ==
                 meta_monitor_config_manager_get_current (config_manager));
}

static void
setup_apply_configuration_test (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &test_case_expect));
}

static void
meta_test_monitor_dbus_apply_left_of (void)
{
  MonitorTestCaseExpect expect;

  setup_apply_configuration_test ();

  check_gdctl_result ("set",
                      "--verbose",
                      "--layout-mode", "logical",
                      "--logical-monitor",
                      "--primary",
                      "--monitor", "DP-1",
                      "--logical-monitor",
                      "--monitor", "DP-2",
                      "--left-of", "DP-1",
                      NULL);

  expect = test_case_expect;
  expect.logical_monitors[0].layout.x = 1536;
  expect.logical_monitors[1].layout.x = 0;
  expect.crtcs[0].x = 1536;
  expect.crtcs[1].x = 0;
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &expect));
}

static void
meta_test_monitor_dbus_apply_right_of_transform (void)
{
  MonitorTestCaseExpect expect;

  setup_apply_configuration_test ();

  check_gdctl_result ("set",
                      "--verbose",
                      "--layout-mode", "logical",
                      "--logical-monitor",
                      "--primary",
                      "--monitor", "DP-2",
                      "--transform", "270",
                      "--logical-monitor",
                      "--monitor", "DP-1",
                      "--right-of", "DP-2",
                      "--y", "400",
                      NULL);

  expect = test_case_expect;
  expect.logical_monitors[0].layout.x = 0;
  expect.logical_monitors[0].layout.y = 0;
  expect.logical_monitors[0].layout.width = 864;
  expect.logical_monitors[0].layout.height = 1536;
  expect.logical_monitors[0].scale = 1.6666666269302368;
  expect.logical_monitors[0].transform = MTK_MONITOR_TRANSFORM_270;
  expect.logical_monitors[0].monitors[0] = 1;

  expect.logical_monitors[1].layout.x = 864;
  expect.logical_monitors[1].layout.y = 400;
  expect.logical_monitors[1].layout.width = 1536;
  expect.logical_monitors[1].layout.height = 864;
  expect.logical_monitors[1].scale = 2.5;
  expect.logical_monitors[1].monitors[0] = 0;

  expect.crtcs[1].x = 0;
  expect.crtcs[1].y = 0;
  expect.crtcs[1].transform = MTK_MONITOR_TRANSFORM_270;
  expect.crtcs[0].x = 864;
  expect.crtcs[0].y = 400;
  expect.screen_width = 2400;
  expect.screen_height = 1536;
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &expect));
}

static void
meta_test_monitor_dbus_apply_mode_scale_below_transform (void)
{
  MonitorTestCaseExpect expect;

  setup_apply_configuration_test ();

  check_gdctl_result ("set",
                      "--verbose",
                      "--layout-mode", "logical",
                      "--logical-monitor",
                      "--primary",
                      "--monitor", "DP-2",
                      "--transform", "270",
                      "--logical-monitor",
                      "--monitor", "DP-1",
                      "--below", "DP-2",
                      "--transform", "90",
                      "--x", "100",
                      "--mode", "1440x900@60.000",
                      "--scale", "1.5",
                      NULL);

  expect = test_case_expect;
  expect.monitors[0].current_mode = 3;
  expect.logical_monitors[0].layout.x = 0;
  expect.logical_monitors[0].layout.y = 0;
  expect.logical_monitors[0].layout.width = 864;
  expect.logical_monitors[0].layout.height = 1536;
  expect.logical_monitors[0].scale = 1.6666666269302368;
  expect.logical_monitors[0].transform = MTK_MONITOR_TRANSFORM_270;
  expect.logical_monitors[0].monitors[0] = 1;
  expect.logical_monitors[1].layout.x = 100;
  expect.logical_monitors[1].layout.y = 1536;
  expect.logical_monitors[1].layout.width = 600;
  expect.logical_monitors[1].layout.height = 960;
  expect.logical_monitors[1].scale = 1.5;
  expect.logical_monitors[1].transform = MTK_MONITOR_TRANSFORM_90;
  expect.logical_monitors[1].monitors[0] = 0;
  expect.crtcs[0].x = 100;
  expect.crtcs[0].y = 1536;
  expect.crtcs[0].current_mode = 3;
  expect.crtcs[0].transform = MTK_MONITOR_TRANSFORM_90;
  expect.crtcs[1].x = 0;
  expect.crtcs[1].y = 0;
  expect.crtcs[1].transform = MTK_MONITOR_TRANSFORM_270;
  expect.screen_width = 864;
  expect.screen_height = 2496;

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &expect));
}

static void
meta_test_monitor_dbus_apply_mirror (void)
{
  MonitorTestCaseExpect expect;

  setup_apply_configuration_test ();

  check_gdctl_result ("set",
                      "--verbose",
                      "--layout-mode", "logical",
                      "--logical-monitor",
                      "--primary",
                      "--monitor", "DP-1",
                      "--mode", "2560x1440@60.000",
                      "--monitor", "DP-2",
                      "--scale", "1.6666666269302368",
                      NULL);

  expect = test_case_expect;
  expect.monitors[0].current_mode = 2;
  expect.logical_monitors[0].layout.width = 1536;
  expect.logical_monitors[0].layout.height = 864;
  expect.logical_monitors[0].scale = 1.6666666269302368;
  expect.logical_monitors[0].monitors[0] = 0;
  expect.logical_monitors[0].monitors[1] = 1;
  expect.logical_monitors[0].n_monitors = 2;
  expect.n_logical_monitors = 1;
  expect.screen_width = 1536;
  expect.screen_height = 864;
  expect.crtcs[0].x = 0;
  expect.crtcs[0].y = 0;
  expect.crtcs[0].current_mode = 2;
  expect.crtcs[1].x = 0;
  expect.crtcs[1].y = 0;

  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &expect));
}

static void
meta_test_monitor_dbus_apply_for_lease (void)
{
  MonitorTestCaseExpect expect;

  setup_apply_configuration_test ();

  check_gdctl_result ("set",
                      "--verbose",
                      "--layout-mode", "logical",
                      "--logical-monitor",
                      "--primary",
                      "--monitor", "DP-1",
                      "--for-lease-monitor", "DP-2",
                      NULL);

  expect = test_case_expect;
  expect.n_logical_monitors = 1;
  expect.screen_width = 1536;
  expect.monitors[1].current_mode = -1;
  expect.crtcs[1].current_mode = -1;
  META_TEST_LOG_CALL ("Checking monitor configuration",
                      meta_check_monitor_configuration (test_context,
                                                        &expect));
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/monitor/dbus/get-state",
                   meta_test_monitor_dbus_get_state);
  g_test_add_func ("/backends/native/monitor/dbus/apply/verify",
                   meta_test_monitor_dbus_apply_verify);
  g_test_add_func ("/backends/native/monitor/dbus/apply/left-of",
                   meta_test_monitor_dbus_apply_left_of);
  g_test_add_func ("/backends/native/monitor/dbus/apply/right-of-transform",
                   meta_test_monitor_dbus_apply_right_of_transform);
  g_test_add_func ("/backends/native/monitor/dbus/apply/mode-scale-below-transform",
                   meta_test_monitor_dbus_apply_mode_scale_below_transform);
  g_test_add_func ("/backends/native/monitor/dbus/apply/mirror",
                   meta_test_monitor_dbus_apply_mirror);
  g_test_add_func ("/backends/native/monitor/dbus/apply/for-lease",
                   meta_test_monitor_dbus_apply_for_lease);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GFile) gdctl_file = NULL;
  char **argv_ignored = NULL;
  GOptionEntry options[] = {
    {
      G_OPTION_REMAINING,
      .arg = G_OPTION_ARG_STRING_ARRAY,
      &argv_ignored,
      .arg_description = "GDCTL-PATH"
    },
    { NULL }
  };

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  meta_context_add_option_entries (context, options, NULL);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  g_assert_nonnull (argv_ignored);
  g_assert_nonnull (argv_ignored[0]);
  g_assert_null (argv_ignored[1]);
  gdctl_path = argv_ignored[0];

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
