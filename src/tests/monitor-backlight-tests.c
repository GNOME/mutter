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

#include "backends/meta-backlight-private.h"
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
      .connector_type = META_CONNECTOR_TYPE_eDP,
      .backlight_min = 10,
      .backlight_max = 150,
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

static MonitorTestCaseSetup sysfs_test_case_setup = {
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
      .connector_type = META_CONNECTOR_TYPE_eDP,
      .sysfs_backlight = "backlight1",
      .backlight_min = 0,
      .backlight_max = 90,
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

static void
meta_test_backlight_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *monitors;
  MetaMonitor *first_monitor;
  MetaMonitor *second_monitor;
  MetaBacklight *backlight;
  int backlight_min;
  int backlight_max;
  int backlight_value;

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);

  first_monitor = g_list_nth_data (monitors, 0);
  second_monitor = g_list_nth_data (monitors, 1);

  backlight = meta_monitor_get_backlight (first_monitor);
  g_assert_nonnull (backlight);
  meta_backlight_get_brightness_info (backlight, &backlight_min, &backlight_max);
  backlight_value = meta_backlight_get_brightness (backlight);
  g_assert_cmpint (backlight_value, >=, 10);
  g_assert_cmpint (backlight_value, <=, 150);
  g_assert_cmpint (backlight_min, ==, 10);
  g_assert_cmpint (backlight_max, ==, 150);

  backlight = meta_monitor_get_backlight (second_monitor);
  g_assert_null (backlight);
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

static void
meta_test_backlight_sysfs_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorTestSetup *test_setup;
  GList *monitors;
  MetaMonitor *first_monitor;
  MetaBacklight *backlight;
  int backlight_min;
  int backlight_max;
  int backlight_value;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &sysfs_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  first_monitor = g_list_nth_data (monitors, 0);

  backlight = meta_monitor_get_backlight (first_monitor);
  g_assert_nonnull (backlight);

  meta_backlight_get_brightness_info (backlight, &backlight_min, &backlight_max);
  backlight_value = meta_backlight_get_brightness (backlight);
  g_assert_cmpint (backlight_min, ==, 0);
  g_assert_cmpint (backlight_max, ==, 90);
  g_assert_cmpint (backlight_value, >=, 0);
  g_assert_cmpint (backlight_value, <=, 90);
}

static GDBusProxy *
get_logind_mock_proxy (MetaBackend *backend)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;

  MetaLauncher *launcher = meta_backend_get_launcher (backend);
  MetaDBusLogin1Session *session_proxy = meta_launcher_get_session_proxy (launcher);
  const char *session_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (session_proxy));

  proxy =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                   NULL,
                                   "org.freedesktop.login1",
                                   session_path,
                                   "org.freedesktop.DBus.Mock",
                                   NULL, &error);
  if (!proxy)
    {
      g_error ("Failed to find mocked color manager system service, %s",
               error->message);
    }

  return proxy;
}

static void
create_logind_backlight (MetaBackend *backend,
                         const char  *name,
                         int          brightness)
{

  g_autoptr (GDBusProxy) mock_proxy = NULL;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;

  mock_proxy = get_logind_mock_proxy (backend);

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(ssu)"));
  g_variant_builder_add (&params_builder, "s", "backlight");
  g_variant_builder_add (&params_builder, "s", name);
  g_variant_builder_add (&params_builder, "u", brightness);

  if (!g_dbus_proxy_call_sync (mock_proxy,
                               "CreateBacklight",
                               g_variant_builder_end (&params_builder),
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               &error))
    g_error ("Failed to create logind backlight: %s", error->message);
}

static void
destroy_logind_backlight (MetaBackend *backend,
                          const char  *name)
{

  g_autoptr (GDBusProxy) mock_proxy = NULL;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;

  mock_proxy = get_logind_mock_proxy (backend);

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(ss)"));
  g_variant_builder_add (&params_builder, "s", "backlight");
  g_variant_builder_add (&params_builder, "s", name);

  if (!g_dbus_proxy_call_sync (mock_proxy,
                               "DestroyBacklight",
                               g_variant_builder_end (&params_builder),
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               &error))
    g_error ("Failed to destroy logind backlight: %s", error->message);
}

static int
get_logind_backlight (MetaBackend *backend,
                      const char  *name)
{

  g_autoptr (GDBusProxy) mock_proxy = NULL;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;
  g_autoptr (GVariant) result = NULL;
  int backlight_value;

  mock_proxy = get_logind_mock_proxy (backend);

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(ss)"));
  g_variant_builder_add (&params_builder, "s", "backlight");
  g_variant_builder_add (&params_builder, "s", name);

  result = g_dbus_proxy_call_sync (mock_proxy,
                                   "GetBacklight",
                                   g_variant_builder_end (&params_builder),
                                   G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                                   &error);
  if (!result)
    g_error ("Failed to destroy logind backlight: %s", error->message);

  g_variant_get (result, "(u)", &backlight_value);
  return backlight_value;
}

static void
meta_test_backlight_sysfs_set (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorTestSetup *test_setup;
  GList *monitors;
  MetaMonitor *first_monitor;
  MetaBacklight *backlight;
  int backlight_value;

  destroy_logind_backlight (backend, "backlight1");
  create_logind_backlight (backend, "backlight1", 90);

  test_setup = meta_create_monitor_test_setup (backend,
                                               &sysfs_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  first_monitor = g_list_nth_data (monitors, 0);

  backlight = meta_monitor_get_backlight (first_monitor);
  g_assert_nonnull (backlight);

  backlight_value = meta_backlight_get_brightness (backlight);
  g_assert_cmpint (backlight_value, ==, 90);
  g_assert_cmpint (get_logind_backlight (backend, "backlight1"), ==, 90);

  meta_backlight_set_brightness (backlight, 30);

  while (meta_backlight_has_pending (backlight))
    g_main_context_iteration (NULL, TRUE);

  backlight_value = meta_backlight_get_brightness (backlight);
  g_assert_cmpint (backlight_value, ==, 30);
  g_assert_cmpint (get_logind_backlight (backend, "backlight1"), ==, 30);
}

static MetaMonitorTestSetup *
create_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend,
                                         &initial_test_case_setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

static void
prepare_backlight_test (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &initial_test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
}

static void
finish_backlight_test (void)
{
}

static void
add_test (const char *test_path,
          GTestFunc   test_func)
{
  g_test_add_vtable (test_path, 0, NULL,
                     (GTestFixtureFunc) prepare_backlight_test,
                     (GTestFixtureFunc) test_func,
                     (GTestFixtureFunc) finish_backlight_test);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  meta_init_monitor_test_setup (create_test_setup);
  add_test ("/backends/backlight/sanity", meta_test_backlight_sanity);
  add_test ("/backends/backlight/api", meta_test_backlight_api);
  add_test ("/backends/backlight/sysfs/sanity", meta_test_backlight_sysfs_sanity);
  add_test ("/backends/backlight/sysfs/set", meta_test_backlight_sysfs_set);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
