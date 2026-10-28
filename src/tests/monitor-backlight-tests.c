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

#include <float.h>
#include <stdio.h>

#include "backends/meta-backlight-private.h"
#include "backends/meta-color-device.h"
#include "backends/meta-color-manager.h"
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
      .backlight_type = META_OUTPUT_TEST_BACKLIGHT_MOCK,
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
      .backlight_type = META_OUTPUT_TEST_BACKLIGHT_SYSFS,
      .sysfs_backlight_name = "backlight1",
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

static MetaMonitorTestSetup *
create_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend,
                                         &initial_test_case_setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

static void
emulate_hotplug (MonitorTestCaseSetup *test_case_setup)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaMonitorTestSetup *test_setup;

  test_setup = meta_create_monitor_test_setup (backend,
                                               test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
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
  MetaBacklight *backlight;
  int backlight_min;
  int backlight_max;
  int backlight_value;

  emulate_hotplug (&initial_test_case_setup);

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
  g_autoptr (GVariant) result = NULL;

  mock_proxy = get_logind_mock_proxy (backend);

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(ssu)"));
  g_variant_builder_add (&params_builder, "s", "backlight");
  g_variant_builder_add (&params_builder, "s", name);
  g_variant_builder_add (&params_builder, "u", brightness);

  result = g_dbus_proxy_call_sync (mock_proxy,
                                   "CreateBacklight",
                                   g_variant_builder_end (&params_builder),
                                   G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                                   &error);
  if (!result)
    g_error ("Failed to create logind backlight: %s", error->message);
}

static void
destroy_logind_backlight (MetaBackend *backend,
                          const char  *name)
{

  g_autoptr (GDBusProxy) mock_proxy = NULL;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;
  g_autoptr (GVariant) result = NULL;

  mock_proxy = get_logind_mock_proxy (backend);

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(ss)"));
  g_variant_builder_add (&params_builder, "s", "backlight");
  g_variant_builder_add (&params_builder, "s", name);

  result = g_dbus_proxy_call_sync (mock_proxy,
                                   "DestroyBacklight",
                                   g_variant_builder_end (&params_builder),
                                   G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                                   &error);
  if (!result)
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

static MonitorTestCaseSetup ref_white_test_case_setup = {
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
      .backlight_type = META_OUTPUT_TEST_BACKLIGHT_REF_WHITE,
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

static void
meta_test_backlight_ref_white_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitor *monitor;
  MetaBacklight *backlight;
  int backlight_min;
  int backlight_max;
  int backlight_value;

  emulate_hotplug (&ref_white_test_case_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  backlight = meta_monitor_get_backlight (monitor);
  g_assert_nonnull (backlight);

  meta_backlight_get_brightness_info (backlight, &backlight_min, &backlight_max);
  backlight_value = meta_backlight_get_brightness (backlight);
  g_assert_cmpint (backlight_min, ==, 10);
  g_assert_cmpint (backlight_max, ==, 210);
  g_assert_cmpint (backlight_value, ==, 100);
}

static void
meta_test_backlight_ref_white_set (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaColorManager *color_manager = meta_backend_get_color_manager (backend);
  MetaMonitor *monitor;
  MetaBacklight *backlight;
  MetaColorDevice *color_device;

  emulate_hotplug (&ref_white_test_case_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  backlight = meta_monitor_get_backlight (monitor);
  color_device = meta_color_manager_get_color_device (color_manager, monitor);

  meta_backlight_set_brightness (backlight, 140);
  meta_backlight_set_brightness (backlight, 160);

  while (meta_backlight_has_pending (backlight))
    g_main_context_iteration (NULL, TRUE);

  g_assert_cmpint (meta_backlight_get_brightness (backlight), ==, 160);
  g_assert_cmpfloat_with_epsilon (
    meta_color_device_get_reference_luminance_factor (color_device),
    1.6f, FLT_EPSILON);
}

static void
meta_test_backlight_ref_white_monitor_removed (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MonitorTestCaseSetup test_case_setup = ref_white_test_case_setup;
  g_autoptr (MetaBacklight) backlight = NULL;
  MetaMonitorTestSetup *test_setup;
  MetaMonitor *monitor;

  emulate_hotplug (&ref_white_test_case_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  backlight = g_object_ref (meta_monitor_get_backlight (monitor));

  meta_backlight_set_brightness (backlight, 140);
  g_assert_true (meta_backlight_has_pending (backlight));

  test_case_setup.n_outputs = 0;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (
    META_MONITOR_MANAGER_TEST (monitor_manager), test_setup);
  g_assert_null (meta_monitor_manager_get_monitors (monitor_manager));

  while (meta_backlight_has_pending (backlight))
    g_main_context_iteration (NULL, TRUE);
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
  g_test_add_func ("/backends/backlight/sanity",
                   meta_test_backlight_sanity);
  g_test_add_func ("/backends/backlight/ref-white/sanity",
                   meta_test_backlight_ref_white_sanity);
  g_test_add_func ("/backends/backlight/ref-white/set",
                   meta_test_backlight_ref_white_set);
  g_test_add_func ("/backends/backlight/ref-white/monitor-removed",
                   meta_test_backlight_ref_white_monitor_removed);
  g_test_add_func ("/backends/backlight/sysfs/sanity",
                   meta_test_backlight_sysfs_sanity);
  g_test_add_func ("/backends/backlight/sysfs/set",
                   meta_test_backlight_sysfs_set);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
