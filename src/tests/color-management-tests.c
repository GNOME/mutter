/*
 * Copyright (C) 2021 Red Hat
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

#include <fcntl.h>

#include "backends/meta-color-device.h"
#include "backends/meta-color-manager-private.h"
#include "backends/meta-color-profile.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-crtc-test.h"
#include "tests/meta-monitor-test-utils.h"

static MetaContext *test_context;

/* Profile ID is 'icc-$(md5sum sRGB.icc)' */
#define SRGB_ICC_PROFILE_ID "icc-112034c661b5e0c91c51f109684612a0";
#define VX239_ICC_PROFILE_ID "icc-c5e479355c02452dd30c1256a154a8f4";

#define PRIMARY_EPSILON 0.000015

static MonitorTestCaseSetup base_monitor_setup = {
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
      .height_mm = 125
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
  .n_outputs = 1,
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

/* Extracted from a 'California Institute of Technology, 0x1403' monitor. */
#define CALTECH_MONITOR_EDID (\
  (MetaEdidInfo) { \
    .default_gamma = 2.200000f, \
    .default_color_primaries = { \
      .primary = { \
          { \
            .x = 0.683594f, \
            .y = 0.312500f, \
          }, \
          { \
            .x = 0.255859f, \
            .y = 0.685547f, \
          }, \
          { \
            .x = 0.139648f, \
            .y = 0.056641f, \
          }, \
      }, \
      .default_white = { \
        .x = 0.313477f, \
        .y = 0.326172f, \
      }, \
    } \
  })

/* Extracted from a 'Ancor Communications Inc, VX239, ECLMRS004144' monitor. */
#define ANCOR_VX239_EDID (\
  (MetaEdidInfo) { \
    .default_gamma = 2.200000f, \
    .default_color_primaries = { \
      .primary = { \
          { \
            .x = 0.651367f, \
            .y = 0.335938f, \
          }, \
          { \
            .x = 0.321289f, \
            .y = 0.614258f, \
          }, \
          { \
            .x = 0.154297f, \
            .y = 0.063477f, \
          }, \
      }, \
      .default_white = { \
        .x = 0.313477f, \
        .y = 0.329102f, \
      }, \
    } \
  })

#define assert_color_xyz_equal(color, expected_color) \
  g_assert_cmpfloat_with_epsilon (color->X, expected_color->X, \
                                  PRIMARY_EPSILON); \
  g_assert_cmpfloat_with_epsilon (color->Y, expected_color->Y, \
                                  PRIMARY_EPSILON); \
  g_assert_cmpfloat_with_epsilon (color->Z, expected_color->Z, \
                                  PRIMARY_EPSILON);

#define assert_color_yxy_equal(color, expected_color) \
  g_assert_cmpfloat_with_epsilon (color->x, expected_color->x, \
                                  PRIMARY_EPSILON); \
  g_assert_cmpfloat_with_epsilon (color->y, expected_color->y, \
                                  PRIMARY_EPSILON); \
  g_assert_cmpfloat_with_epsilon (color->Y, expected_color->Y, \
                                  PRIMARY_EPSILON);

static GDBusProxy *
get_colord_mock_proxy (void)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;

  proxy =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                   NULL,
                                   "org.freedesktop.ColorManager",
                                   "/org/freedesktop/ColorManager",
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
wait_for_profile_assigned (MetaColorDevice *color_device,
                           const char      *profile_id)
{
  while (TRUE)
    {
      MetaColorProfile *color_profile;

      color_profile = meta_color_device_get_assigned_profile (color_device);
      if (color_profile &&
          g_strcmp0 (meta_color_profile_get_id (color_profile),
                     profile_id) == 0)
        break;

      g_main_context_iteration (NULL, TRUE);
    }
}

static void
on_device_calibration_changed (MetaColorDevice *color_device,
                               gboolean        *run)
{
  *run = FALSE;
}

static void
wait_for_device_calibration_changed (MetaColorDevice *color_device)
{
  gulong handler_id;
  gboolean run = TRUE;

  handler_id = g_signal_connect (color_device, "calibration-changed",
                                 G_CALLBACK (on_device_calibration_changed),
                                 &run);
  while (run)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (color_device, handler_id);
}

static void
assert_gamma_array (uint16_t *expected,
                    uint16_t *values,
                    size_t    size)
{
  size_t i;

  for (i = 0; i < size; i++)
    {
      if (expected[i] != values[i])
        {
          g_error ("Expected %hu at but got %hu at index %zu",
                   expected[i], values[i], i);
        }
    }
}

static void
set_colord_device_profiles (const char  *cd_device_id,
                            const char **cd_profile_ids,
                            int          n_cd_profile_ids)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;
  GVariantBuilder profiles_builder;
  int i;

  proxy = get_colord_mock_proxy ();

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(sas)"));
  g_variant_builder_add (&params_builder, "s", cd_device_id);

  g_variant_builder_init (&profiles_builder, G_VARIANT_TYPE ("as"));
  for (i = 0; i < n_cd_profile_ids; i++)
    g_variant_builder_add (&profiles_builder, "s", cd_profile_ids[i]);
  g_variant_builder_add (&params_builder, "as", &profiles_builder);

  if (!g_dbus_proxy_call_sync (proxy,
                               "SetDeviceProfiles",
                               g_variant_builder_end (&params_builder),
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               &error))
    g_error ("Failed to set device profile: %s", error->message);
}

static void
add_colord_system_profile (const char *cd_profile_id,
                           const char *file_path)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;

  proxy = get_colord_mock_proxy ();

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(ss)"));
  g_variant_builder_add (&params_builder, "s", cd_profile_id);
  g_variant_builder_add (&params_builder, "s", file_path);

  if (!g_dbus_proxy_call_sync (proxy,
                               "AddSystemProfile",
                               g_variant_builder_end (&params_builder),
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               &error))
    g_error ("Failed to add system profile: %s", error->message);
}

static GDBusProxy *
get_gsd_color_mock_proxy (void)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;

  proxy =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START |
                                   G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                   NULL,
                                   "org.gnome.SettingsDaemon.Color",
                                   "/org/gnome/SettingsDaemon/Color",
                                   "org.freedesktop.DBus.Mock",
                                   NULL, &error);
  if (!proxy)
    {
      g_error ("Failed to find mocked gsd-color service, %s",
               error->message);
    }

  return proxy;
}

static void
set_night_light_temperature (unsigned int temperature)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;

  proxy = get_gsd_color_mock_proxy ();

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(u)"));
  g_variant_builder_add (&params_builder, "u", temperature);

  if (!g_dbus_proxy_call_sync (proxy,
                               "SetTemperature",
                               g_variant_builder_end (&params_builder),
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               &error))
    g_error ("Failed to set gsd-color temperature devices: %s", error->message);
}

static void
set_night_light_active (gboolean active)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;
  GVariantBuilder params_builder;

  proxy = get_gsd_color_mock_proxy ();

  g_variant_builder_init (&params_builder, G_VARIANT_TYPE ("(b)"));
  g_variant_builder_add (&params_builder, "b", active);

  if (!g_dbus_proxy_call_sync (proxy,
                               "SetNightLightActive",
                               g_variant_builder_end (&params_builder),
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               &error))
    g_error ("Failed to set enable or disable night light: %s", error->message);
}

static void
prepare_color_test (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;

  proxy = get_colord_mock_proxy ();

  if (!g_dbus_proxy_call_sync (proxy,
                               "Reset",
                               NULL,
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL,
                               &error))
    g_error ("Failed to reset mocked colord state: %s", error->message);

  g_assert_null (meta_monitor_manager_get_monitors (monitor_manager));
  g_assert_cmpint (meta_color_manager_get_num_color_devices (color_manager),
                   ==,
                   0);
}

static void
finish_color_test (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MetaMonitorTestSetup *test_setup;

  test_case_setup.n_outputs = 0;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);
  g_assert_null (meta_monitor_manager_get_monitors (monitor_manager));
  g_assert_cmpint (meta_color_manager_get_num_color_devices (color_manager),
                   ==,
                   0);
}

static void
meta_test_color_management_device_basic (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  GList *monitors;
  GList *l;
  int i;

  test_case_setup.outputs[0].edid_info = CALTECH_MONITOR_EDID;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_case_setup.outputs[1].edid_info = ANCOR_VX239_EDID;
  test_case_setup.outputs[1].has_edid_info = TRUE;

  test_case_setup.n_outputs = 2;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  i = 0;
  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  g_assert_cmpuint (g_list_length (monitors),
                    ==,
                    meta_color_manager_get_num_color_devices (color_manager));

  test_case_setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  g_assert_cmpuint (g_list_length (monitors),
                    ==,
                    meta_color_manager_get_num_color_devices (color_manager));

  test_case_setup.n_outputs = 2;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 2);
  g_assert_cmpuint (g_list_length (monitors),
                    ==,
                    meta_color_manager_get_num_color_devices (color_manager));

  for (l = monitors, i = 0; l; l = l->next, i++)
    {
      MetaMonitor *monitor = META_MONITOR (l->data);
      const MetaEdidInfo *expected_edid_info =
        &test_case_setup.outputs[i].edid_info;
      const MetaEdidInfo *monitor_edid_info;
      MetaColorDevice *color_device;

      g_assert_nonnull (meta_monitor_get_edid_checksum_md5 (monitor));
      monitor_edid_info = meta_monitor_get_edid_info (monitor);

      g_assert_cmpfloat_with_epsilon (expected_edid_info->default_gamma,
                                      monitor_edid_info->default_gamma,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.primary[0].x,
        monitor_edid_info->default_color_primaries.primary[0].x,
        FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.primary[0].y,
        monitor_edid_info->default_color_primaries.primary[0].y,
        FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.primary[1].x,
        monitor_edid_info->default_color_primaries.primary[1].x,
        FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.primary[1].y,
        monitor_edid_info->default_color_primaries.primary[1].y,
        FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.primary[2].x,
        monitor_edid_info->default_color_primaries.primary[2].x,
        FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.primary[2].y,
        monitor_edid_info->default_color_primaries.primary[2].y,
        FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.default_white.x,
        monitor_edid_info->default_color_primaries.default_white.x,
        FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (
        expected_edid_info->default_color_primaries.default_white.y,
        monitor_edid_info->default_color_primaries.default_white.y,
        FLT_EPSILON);

      color_device = meta_color_manager_get_color_device (color_manager,
                                                          monitor);
      g_assert_nonnull (color_device);

      g_assert_true (meta_color_device_get_monitor (color_device) == monitor);
    }
}

static void
meta_test_color_management_device_no_gamma (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  GList *monitors;
  MetaMonitor *monitor;
  MetaColorDevice *color_device;
  MetaColorProfile *color_profile;
  const char *profile_id;
  const char *color_profiles[1];

  test_case_setup.outputs[0].edid_info = CALTECH_MONITOR_EDID;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_case_setup.crtcs[0].disable_gamma_lut = TRUE;

  test_case_setup.n_outputs = 1;
  test_case_setup.n_crtcs = 1;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpuint (g_list_length (monitors), ==, 1);
  g_assert_cmpuint (g_list_length (monitors),
                    ==,
                    meta_color_manager_get_num_color_devices (color_manager));

  monitor = META_MONITOR (monitors->data);
  g_assert_cmpint (meta_monitor_get_gamma_lut_size (monitor), ==, 0);

  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);
  g_assert_true (meta_color_device_get_monitor (color_device) == monitor);

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  color_profile = meta_color_device_get_device_profile (color_device);
  g_assert_nonnull (color_profile);

  profile_id = meta_color_profile_get_id (color_profile);
  color_profiles[0] = profile_id;
  set_colord_device_profiles (meta_color_device_get_id (color_device),
                              color_profiles, G_N_ELEMENTS (color_profiles));

  wait_for_profile_assigned (color_device, profile_id);
}

static void
meta_test_color_management_profile_device (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MetaEdidInfo edid_info;
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  MetaMonitor *monitor;
  MetaColorDevice *color_device;
  MetaColorProfile *color_profile;
  CdIcc *cd_icc;
  const CdColorXYZ *red;
  const CdColorXYZ *green;
  const CdColorXYZ *blue;
  const CdColorXYZ *white;

  edid_info = CALTECH_MONITOR_EDID;
  test_case_setup.outputs[0].edid_info = edid_info;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  color_profile = meta_color_device_get_device_profile (color_device);
  g_assert_nonnull (color_profile);
  cd_icc = meta_color_profile_get_cd_icc (color_profile);
  g_assert_nonnull (cd_icc);

  red = cd_icc_get_red (cd_icc);
  green = cd_icc_get_green (cd_icc);
  blue = cd_icc_get_blue (cd_icc);
  white = cd_icc_get_white (cd_icc);

  /* Make sure we generate the same values as gsd-color did. */
  g_assert_cmpfloat_with_epsilon (red->X, 0.549637, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (red->Y, 0.250671, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (red->Z, 0.000977, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (green->X, 0.277420, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (green->Y, 0.689514, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (green->Z, 0.052185, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (blue->X, 0.137146 , PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (blue->Y, 0.059814, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (blue->Z, 0.771744, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (white->X, 0.961090088, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (white->Y, 1.0, PRIMARY_EPSILON);
  g_assert_cmpfloat_with_epsilon (white->Z, 1.10479736, PRIMARY_EPSILON);
}

static void
meta_test_color_management_profile_device_bogus (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MetaEdidInfo edid_info;
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  MetaMonitor *monitor;
  MetaColorDevice *color_device;
  MetaColorProfile *color_profile;

  edid_info = CALTECH_MONITOR_EDID;
  /* Decoding gamma is in [1, 4] */
  edid_info.default_gamma = 0.7;
  test_case_setup.outputs[0].serial = "profile_device_bogus/gamma";
  test_case_setup.outputs[0].edid_info = edid_info;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Failed to create device color profile:*"
                         "contains bogus Display Transfer Characteristics "
                         "(GAMMA)");

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  g_test_assert_expected_messages ();
  color_profile = meta_color_device_get_device_profile (color_device);
  g_assert_null (color_profile);

  edid_info = CALTECH_MONITOR_EDID;
  edid_info.default_color_primaries.primary[1].y = 0.0;
  test_case_setup.outputs[0].serial = "profile_device_bogus/chromaticity";
  test_case_setup.outputs[0].edid_info = edid_info;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Failed to create device color profile:*"
                         "contains bogus Color Characteristics");

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  color_profile = meta_color_device_get_device_profile (color_device);
  g_assert_null (color_profile);
  g_test_assert_expected_messages ();
}

static void
meta_test_color_management_profile_system (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MetaEdidInfo edid_info;
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  MetaMonitor *monitor;
  MetaColorDevice *color_device;
  const char *path;
  const char *color_profiles[1];
  MetaColorProfile *color_profile;
  const char *srgb_profile_id = SRGB_ICC_PROFILE_ID;

  edid_info = CALTECH_MONITOR_EDID;
  test_case_setup.outputs[0].edid_info = edid_info;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  g_assert_null (meta_color_device_get_assigned_profile (color_device));

  path = g_test_get_filename (G_TEST_DIST, "icc-profiles", "sRGB.icc",
                              NULL);
  add_colord_system_profile (srgb_profile_id, path);
  color_profiles[0] = srgb_profile_id;
  set_colord_device_profiles (meta_color_device_get_id (color_device),
                              color_profiles, G_N_ELEMENTS (color_profiles));

  while (TRUE)
    {
      color_profile = meta_color_device_get_assigned_profile (color_device);
      if (color_profile)
        break;

      g_main_context_iteration (NULL, TRUE);
    }

  g_assert_cmpstr (meta_color_profile_get_id (color_profile),
                   ==,
                   srgb_profile_id);
}

static void
generate_efi_test_profile (const char *path,
                           double      gamma,
                           CdColorYxy *red,
                           CdColorYxy *green,
                           CdColorYxy *blue,
                           CdColorYxy *white)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (CdIcc) cd_icc = NULL;
  g_autoptr (GFile) file = NULL;
  const CdColorXYZ *red_xyz;
  const CdColorXYZ *green_xyz;
  const CdColorXYZ *blue_xyz;
  const CdColorXYZ *white_xyz;

  cd_icc = cd_icc_new ();

  if (!cd_icc_create_from_edid (cd_icc, 2.2, red, green, blue, white, &error))
    g_error ("Failed to generate reference profile: %s", error->message);

  file = g_file_new_for_path (path);
  if (!cd_icc_save_file (cd_icc, file, CD_ICC_SAVE_FLAGS_NONE,
                         NULL, &error))
    g_error ("Failed to save reference profile: %s", error->message);

  g_clear_object (&cd_icc);

  cd_icc = cd_icc_new ();
  if (!cd_icc_load_file (cd_icc, file, CD_ICC_LOAD_FLAGS_PRIMARIES,
                         NULL, &error))
    g_error ("Failed to load reference profile: %s", error->message);

  red_xyz = cd_icc_get_red (cd_icc);
  green_xyz = cd_icc_get_green (cd_icc);
  blue_xyz = cd_icc_get_blue (cd_icc);
  white_xyz = cd_icc_get_white (cd_icc);
  cd_color_xyz_to_yxy (red_xyz, red);
  cd_color_xyz_to_yxy (green_xyz, green);
  cd_color_xyz_to_yxy (blue_xyz, blue);
  cd_color_xyz_to_yxy (white_xyz, white);
}

static void
meta_test_color_management_profile_efivar (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  char efivar_path[] = "/tmp/efivar-test-profile-XXXXXX";
  int fd;
  CdColorYxy *reference_red_yxy;
  CdColorYxy *reference_green_yxy;
  CdColorYxy *reference_blue_yxy;
  CdColorYxy *reference_white_yxy;
  MetaEdidInfo edid_info;
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  MetaMonitor *monitor;
  MetaColorDevice *color_device;
  MetaColorProfile *color_profile;
  CdIcc *cd_icc;
  g_autoptr (CdIcc) srgb_cd_icc = NULL;
  const CdColorXYZ *red_xyz;
  const CdColorXYZ *green_xyz;
  const CdColorXYZ *blue_xyz;
  const CdColorXYZ *white_xyz;
  const CdColorXYZ *srgb_red_xyz;
  const CdColorXYZ *srgb_green_xyz;
  const CdColorXYZ *srgb_blue_xyz;
  const CdColorXYZ *srgb_white_xyz;
  const MetaColorCalibration *color_calibration;

  fd = mkostemp (efivar_path, O_CLOEXEC);
  g_assert_cmpint (fd, >=, 0);
  close (fd);

  reference_red_yxy = cd_color_yxy_new ();
  reference_green_yxy = cd_color_yxy_new ();
  reference_blue_yxy = cd_color_yxy_new ();
  reference_white_yxy = cd_color_yxy_new ();

  cd_color_yxy_set (reference_red_yxy, 0.0, 0.3, 0.6);
  cd_color_yxy_set (reference_green_yxy, 0.0, 0.7, 0.2);
  cd_color_yxy_set (reference_blue_yxy, 0.0, 0.1, 0.2);
  cd_color_yxy_set (reference_white_yxy, 1.0, 0.3, 0.3);

  generate_efi_test_profile (efivar_path,
                             2.2,
                             reference_red_yxy,
                             reference_green_yxy,
                             reference_blue_yxy,
                             reference_white_yxy);
  meta_set_color_efivar_test_path (efivar_path);

  edid_info = ANCOR_VX239_EDID;
  test_case_setup.outputs[0].serial = __func__;
  test_case_setup.outputs[0].edid_info = edid_info;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_case_setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  color_profile = meta_color_device_get_device_profile (color_device);
  g_assert_nonnull (color_profile);

  cd_icc = meta_color_profile_get_cd_icc (color_profile);
  g_assert_nonnull (cd_icc);

  red_xyz = cd_icc_get_red (cd_icc);
  green_xyz = cd_icc_get_green (cd_icc);
  blue_xyz = cd_icc_get_blue (cd_icc);
  white_xyz = cd_icc_get_white (cd_icc);

  srgb_cd_icc = cd_icc_new ();
  g_assert_true (cd_icc_create_default_full (srgb_cd_icc,
                                             CD_ICC_LOAD_FLAGS_PRIMARIES,
                                             NULL));

  srgb_red_xyz = cd_icc_get_red (srgb_cd_icc);
  srgb_green_xyz = cd_icc_get_green (srgb_cd_icc);
  srgb_blue_xyz = cd_icc_get_blue (srgb_cd_icc);
  srgb_white_xyz = cd_icc_get_white (srgb_cd_icc);

  /* Make sure we the values from the sRGB profile. */
  assert_color_xyz_equal (red_xyz, srgb_red_xyz);
  assert_color_xyz_equal (green_xyz, srgb_green_xyz);
  assert_color_xyz_equal (blue_xyz, srgb_blue_xyz);
  assert_color_xyz_equal (white_xyz, srgb_white_xyz);

  color_calibration = meta_color_profile_get_calibration (color_profile);
  g_assert_true (color_calibration->has_adaptation_matrix);

  meta_set_color_efivar_test_path (NULL);
  unlink (efivar_path);
}

static void
meta_test_color_management_night_light_calibrated (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MetaEdidInfo edid_info;
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  MetaMonitor *monitor;
  MetaOutput *output;
  MetaCrtc *crtc;
  MetaCrtcTest *crtc_test;
  MetaColorDevice *color_device;
  const char *path;
  const char *color_profiles[1];
  const char *profile_id = VX239_ICC_PROFILE_ID;

  /* Night light disabled */
  uint16_t night_light_off_red[] = {
    0, 248, 499, 751, 1002, 1255, 1508, 1761, 2016, 2271, 2527, 2783, 3040,
    3298, 3556, 3814, 4074, 4333, 4593, 4854, 5114, 5375, 5636, 5897, 6160,
    6422, 6685, 6947, 7212, 7479, 7745, 8013, 8282, 8553, 8824, 9095, 9367,
    9641, 9915, 10189, 10465, 10741, 11016, 11292, 11571, 11847, 12125, 12403,
    12681, 12960, 13238, 13516, 13798, 14084, 14377, 14672, 14974, 15279,
    15586, 15896, 16209, 16523, 16840, 17156, 17475, 17792, 18109, 18426,
    18740, 19054, 19364, 19673, 19977, 20278, 20577, 20868, 21156, 21438,
    21709, 21970, 22220, 22461, 22695, 22922, 23143, 23357, 23568, 23776,
    23979, 24182, 24382, 24584, 24786, 24989, 25195, 25404, 25617, 25836,
    26060, 26290, 26529, 26776, 27032, 27298, 27573, 27857, 28148, 28444,
    28748, 29055, 29368, 29686, 30004, 30327, 30650, 30975, 31300, 31624,
    31948, 32268, 32587, 32902, 33213, 33519, 33818, 34112, 34399, 34678,
    34949, 35206, 35450, 35683, 35905, 36119, 36325, 36523, 36714, 36901,
    37086, 37267, 37445, 37625, 37805, 37985, 38169, 38359, 38552, 38750,
    38957, 39172, 39398, 39633, 39880, 40140, 40417, 40712, 41026, 41356,
    41700, 42055, 42424, 42799, 43185, 43574, 43970, 44366, 44765, 45162,
    45558, 45948, 46335, 46712, 47083, 47440, 47788, 48121, 48437, 48736,
    49017, 49280, 49529, 49768, 49999, 50221, 50436, 50645, 50848, 51044,
    51238, 51429, 51617, 51803, 51989, 52176, 52363, 52550, 52742, 52938,
    53138, 53342, 53553, 53771, 53998, 54232, 54476, 54730, 54993, 55266,
    55546, 55832, 56125, 56423, 56727, 57033, 57342, 57652, 57964, 58277,
    58588, 58897, 59205, 59509, 59811, 60106, 60396, 60678, 60954, 61221,
    61479, 61728, 61965, 62195, 62415, 62627, 62829, 63024, 63213, 63393,
    63564, 63730, 63888, 64038, 64183, 64320, 64453, 64577, 64697, 64810,
    64919, 65022, 65118, 65211, 65298, 65381, 65461, 65535
  };
  uint16_t night_light_off_green[] = {
    0, 147, 297, 451, 607, 767, 928, 1094, 1262, 1433, 1608, 1785, 1967, 2152,
    2339, 2530, 2724, 2923, 3124, 3329, 3537, 3749, 3966, 4186, 4409, 4636,
    4867, 5109, 5358, 5618, 5884, 6156, 6434, 6718, 7004, 7294, 7584, 7876,
    8168, 8459, 8750, 9035, 9320, 9598, 9870, 10137, 10396, 10647, 10889,
    11121, 11341, 11550, 11753, 11955, 12158, 12359, 12561, 12764, 12967,
    13170, 13372, 13573, 13776, 13978, 14182, 14384, 14586, 14788, 14990,
    15193, 15396, 15598, 15801, 16002, 16204, 16407, 16610, 16812, 17015,
    17217, 17419, 17622, 17824, 18027, 18228, 18431, 18634, 18835, 19039,
    19240, 19444, 19645, 19847, 20051, 20252, 20456, 20657, 20860, 21063,
    21264, 21468, 21669, 21872, 22077, 22288, 22506, 22728, 22953, 23183,
    23418, 23655, 23895, 24137, 24380, 24625, 24872, 25119, 25366, 25612,
    25858, 26104, 26346, 26587, 26825, 27059, 27291, 27519, 27743, 27960,
    28175, 28386, 28593, 28798, 29000, 29198, 29397, 29592, 29788, 29980,
    30175, 30366, 30561, 30753, 30949, 31143, 31341, 31539, 31741, 31945,
    32150, 32361, 32573, 32791, 33013, 33238, 33466, 33697, 33929, 34163,
    34399, 34637, 34876, 35116, 35356, 35597, 35839, 36081, 36322, 36562,
    36802, 37040, 37278, 37514, 37747, 37979, 38209, 38436, 38661, 38884,
    39103, 39321, 39538, 39757, 39975, 40194, 40413, 40632, 40850, 41068,
    41286, 41504, 41724, 41942, 42161, 42379, 42597, 42815, 43034, 43253,
    43472, 43690, 43908, 44126, 44344, 44563, 44782, 45001, 45219, 45437,
    45655, 45873, 46092, 46311, 46530, 46748, 46966, 47184, 47403, 47621,
    47841, 48059, 48277, 48495, 48713, 48932, 49151, 49370, 49588, 49807,
    50024, 50242, 50461, 50680, 50899, 51117, 51336, 51553, 51772, 51990,
    52210, 52428, 52646, 52865, 53082, 53301, 53519, 53739, 53957, 54176,
    54393, 54611, 54830, 55048, 55268, 55486, 55705
  };
  uint16_t night_light_off_blue[] = {
    0, 137, 277, 419, 564, 712, 864, 1019, 1180, 1343, 1511, 1684, 1862, 2046,
    2235, 2431, 2632, 2840, 3055, 3277, 3506, 3743, 3988, 4243, 4505, 4775,
    5055, 5342, 5631, 5927, 6227, 6529, 6836, 7145, 7456, 7771, 8086, 8401,
    8718, 9035, 9352, 9668, 9983, 10298, 10609, 10918, 11223, 11526, 11827,
    12121, 12412, 12696, 12980, 13262, 13545, 13826, 14108, 14390, 14670,
    14951, 15231, 15509, 15787, 16063, 16339, 16615, 16888, 17159, 17429,
    17699, 17966, 18232, 18497, 18759, 19019, 19277, 19533, 19787, 20034,
    20275, 20512, 20744, 20973, 21197, 21417, 21635, 21851, 22064, 22277,
    22490, 22701, 22913, 23126, 23339, 23554, 23772, 23991, 24214, 24442,
    24673, 24908, 25148, 25394, 25645, 25902, 26162, 26427, 26697, 26968,
    27243, 27519, 27798, 28078, 28362, 28645, 28927, 29211, 29494, 29778,
    30060, 30339, 30617, 30894, 31169, 31439, 31707, 31970, 32230, 32485,
    32733, 32975, 33211, 33442, 33669, 33890, 34109, 34325, 34540, 34752,
    34963, 35172, 35381, 35592, 35804, 36017, 36232, 36448, 36669, 36893,
    37122, 37357, 37596, 37841, 38092, 38353, 38626, 38908, 39200, 39500,
    39809, 40124, 40443, 40767, 41095, 41426, 41756, 42088, 42421, 42751,
    43077, 43402, 43722, 44035, 44342, 44642, 44935, 45217, 45489, 45749,
    45998, 46240, 46478, 46711, 46940, 47167, 47390, 47611, 47829, 48046,
    48261, 48474, 48687, 48901, 49115, 49330, 49546, 49762, 49981, 50203,
    50428, 50656, 50888, 51123, 51364, 51609, 51857, 52105, 52353, 52601,
    52850, 53100, 53351, 53602, 53853, 54106, 54359, 54613, 54868, 55124,
    55381, 55639, 55899, 56159, 56422, 56685, 56950, 57217, 57485, 57755,
    58026, 58299, 58575, 58852, 59132, 59412, 59696, 59981, 60267, 60556,
    60845, 61136, 61427, 61718, 62011, 62305, 62600, 62894, 63188, 63483,
    63776, 64071, 64364, 64659, 64952, 65244, 65535
  };
  /* Night light at 3305K */
  uint16_t night_light_on_red[] = {
    0, 248, 499, 751, 1002, 1255, 1508, 1761, 2016, 2271, 2527, 2783, 3040,
    3298, 3556, 3814, 4074, 4333, 4593, 4854, 5114, 5375, 5636, 5897, 6160,
    6422, 6685, 6947, 7212, 7479, 7745, 8013, 8282, 8553, 8824, 9095, 9367,
    9641, 9915, 10189, 10465, 10741, 11016, 11292, 11571, 11847, 12125, 12403,
    12681, 12960, 13238, 13516, 13798, 14084, 14377, 14672, 14974, 15279,
    15586, 15896, 16209, 16523, 16840, 17156, 17475, 17792, 18109, 18426,
    18740, 19054, 19364, 19673, 19977, 20278, 20577, 20868, 21156, 21438,
    21709, 21970, 22220, 22461, 22695, 22922, 23143, 23357, 23568, 23776,
    23979, 24182, 24382, 24584, 24786, 24989, 25195, 25404, 25617, 25836,
    26060, 26290, 26529, 26776, 27032, 27298, 27573, 27857, 28148, 28444,
    28748, 29055, 29368, 29686, 30004, 30327, 30650, 30975, 31300, 31624,
    31948, 32268, 32587, 32902, 33213, 33519, 33818, 34112, 34399, 34678,
    34949, 35206, 35450, 35683, 35905, 36119, 36325, 36523, 36714, 36901,
    37086, 37267, 37445, 37625, 37805, 37985, 38169, 38359, 38552, 38750,
    38957, 39172, 39398, 39633, 39880, 40140, 40417, 40712, 41026, 41356,
    41700, 42055, 42424, 42799, 43185, 43574, 43970, 44366, 44765, 45162,
    45558, 45948, 46335, 46712, 47083, 47440, 47788, 48121, 48437, 48736,
    49017, 49280, 49529, 49768, 49999, 50221, 50436, 50645, 50848, 51044,
    51238, 51429, 51617, 51803, 51989, 52176, 52363, 52550, 52742, 52938,
    53138, 53342, 53553, 53771, 53998, 54232, 54476, 54730, 54993, 55266,
    55546, 55832, 56125, 56423, 56727, 57033, 57342, 57652, 57964, 58277,
    58588, 58897, 59205, 59509, 59811, 60106, 60396, 60678, 60954, 61221,
    61479, 61728, 61965, 62195, 62415, 62627, 62829, 63024, 63213, 63393,
    63564, 63730, 63888, 64038, 64183, 64320, 64453, 64577, 64697, 64810,
    64919, 65022, 65118, 65211, 65298, 65381, 65461, 65535,
  };
  uint16_t night_light_on_green[] = {
    0, 112, 225, 341, 460, 581, 704, 829, 956, 1086, 1219, 1353, 1490, 1631,
    1773, 1918, 2065, 2215, 2368, 2524, 2681, 2842, 3006, 3172, 3341, 3513,
    3689, 3872, 4061, 4258, 4459, 4666, 4877, 5092, 5308, 5528, 5749, 5970,
    6191, 6412, 6632, 6849, 7064, 7275, 7481, 7684, 7880, 8070, 8254, 8429,
    8596, 8755, 8908, 9061, 9215, 9368, 9521, 9675, 9828, 9982, 10135, 10288,
    10442, 10595, 10749, 10902, 11055, 11209, 11362, 11516, 11669, 11822,
    11976, 12129, 12282, 12436, 12589, 12743, 12896, 13050, 13203, 13357,
    13510, 13664, 13817, 13970, 14124, 14277, 14431, 14584, 14738, 14891,
    15044, 15198, 15351, 15505, 15658, 15811, 15965, 16118, 16272, 16425,
    16578, 16734, 16894, 17058, 17227, 17398, 17572, 17750, 17929, 18111,
    18295, 18480, 18665, 18853, 19040, 19227, 19414, 19600, 19786, 19969,
    20152, 20332, 20510, 20686, 20858, 21028, 21193, 21356, 21515, 21673,
    21828, 21981, 22132, 22282, 22430, 22578, 22724, 22871, 23017, 23164,
    23310, 23458, 23606, 23755, 23906, 24058, 24213, 24369, 24528, 24690,
    24855, 25023, 25193, 25366, 25541, 25717, 25895, 26074, 26254, 26435,
    26617, 26799, 26982, 27165, 27348, 27531, 27713, 27894, 28075, 28255,
    28434, 28612, 28787, 28962, 29134, 29304, 29473, 29639, 29804, 29969,
    30135, 30300, 30466, 30632, 30798, 30963, 31129, 31294, 31459, 31625,
    31790, 31956, 32122, 32288, 32453, 32619, 32784, 32950, 33115, 33281,
    33447, 33612, 33778, 33943, 34109, 34274, 34440, 34606, 34771, 34937,
    35102, 35268, 35433, 35599, 35765, 35931, 36096, 36262, 36427, 36592,
    36758, 36923, 37089, 37255, 37421, 37586, 37752, 37917, 38082, 38248,
    38414, 38580, 38745, 38911, 39076, 39242, 39407, 39573, 39739, 39904,
    40070, 40235, 40401, 40566, 40732, 40898, 41064, 41229, 41394, 41560,
    41725, 41891, 42057, 42222
  };
  uint16_t night_light_on_blue[] = {
    0, 69, 139, 211, 283, 358, 434, 512, 593, 675, 759, 846, 935, 1028, 1123,
    1221, 1322, 1427, 1535, 1647, 1762, 1881, 2005, 2132, 2264, 2400, 2541,
    2685, 2830, 2979, 3129, 3282, 3436, 3591, 3748, 3906, 4064, 4223, 4382,
    4541, 4701, 4860, 5018, 5176, 5332, 5487, 5641, 5793, 5944, 6093, 6238,
    6382, 6524, 6666, 6808, 6950, 7091, 7232, 7374, 7514, 7655, 7795, 7935,
    8074, 8213, 8351, 8488, 8625, 8761, 8896, 9030, 9164, 9297, 9429, 9559,
    9689, 9818, 9945, 10069, 10191, 10310, 10427, 10541, 10654, 10765, 10875,
    10983, 11090, 11197, 11304, 11410, 11517, 11624, 11731, 11839, 11948,
    12059, 12171, 12285, 12401, 12519, 12640, 12764, 12890, 13019, 13150,
    13283, 13418, 13555, 13693, 13832, 13972, 14113, 14255, 14398, 14540,
    14683, 14825, 14967, 15109, 15250, 15389, 15529, 15666, 15802, 15937,
    16069, 16200, 16328, 16452, 16574, 16693, 16809, 16923, 17034, 17145,
    17253, 17361, 17467, 17573, 17679, 17784, 17890, 17996, 18103, 18211,
    18320, 18431, 18544, 18659, 18777, 18897, 19020, 19146, 19277, 19414,
    19556, 19703, 19854, 20009, 20167, 20328, 20491, 20655, 20822, 20988,
    21155, 21322, 21488, 21652, 21815, 21976, 22134, 22288, 22439, 22586,
    22727, 22864, 22995, 23120, 23242, 23361, 23479, 23594, 23708, 23820,
    23931, 24040, 24149, 24257, 24365, 24472, 24580, 24687, 24795, 24903,
    25012, 25122, 25234, 25347, 25461, 25578, 25696, 25817, 25940, 26065,
    26190, 26314, 26439, 26565, 26690, 26816, 26943, 27069, 27196, 27323,
    27451, 27579, 27708, 27837, 27966, 28097, 28228, 28360, 28492, 28625,
    28759, 28894, 29029, 29166, 29303, 29442, 29581, 29722, 29863, 30006,
    30149, 30293, 30437, 30583, 30729, 30875, 31022, 31169, 31317, 31465,
    31613, 31760, 31909, 32056, 32205, 32352, 32500, 32647, 32794, 32940
  };
  unsigned int temperature = 3305;

  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_off_green));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_off_blue));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_on_red));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_on_green));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_on_blue));

  edid_info = ANCOR_VX239_EDID;
  test_case_setup.outputs[0].edid_info = edid_info;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  set_night_light_temperature (6500);
  set_night_light_active (FALSE);
  path = g_test_get_filename (G_TEST_DIST,
                              "icc-profiles", "vx239-calibrated.icc",
                              NULL);
  add_colord_system_profile (profile_id, path);
  color_profiles[0] = profile_id;
  set_colord_device_profiles (meta_color_device_get_id (color_device),
                              color_profiles, G_N_ELEMENTS (color_profiles));

  wait_for_profile_assigned (color_device, profile_id);

  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);
  crtc_test = META_CRTC_TEST (crtc);

  g_assert_cmpuint (crtc_test->gamma.size,
                    ==,
                    G_N_ELEMENTS (night_light_off_red));

  assert_gamma_array (night_light_off_red, crtc_test->gamma.red,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_off_green, crtc_test->gamma.green,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_off_blue, crtc_test->gamma.blue,
                      crtc_test->gamma.size);

  set_night_light_temperature (temperature);
  set_night_light_active (TRUE);
  wait_for_device_calibration_changed (color_device);

  assert_gamma_array (night_light_on_red, crtc_test->gamma.red,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_on_green, crtc_test->gamma.green,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_on_blue, crtc_test->gamma.blue,
                      crtc_test->gamma.size);
}

static void
meta_test_color_management_night_light_uncalibrated (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MetaEdidInfo edid_info;
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;
  MetaMonitor *monitor;
  MetaOutput *output;
  MetaCrtc *crtc;
  MetaCrtcTest *crtc_test;
  MetaColorDevice *color_device;
  const char *path;
  const char *color_profiles[1];
  const char *srgb_profile_id = SRGB_ICC_PROFILE_ID;

  /* Night light disabled */
  uint16_t night_light_off_red[] = {
    0, 257, 514, 771, 1028, 1285, 1542, 1799, 2056, 2313, 2570, 2827, 3084,
    3341, 3598, 3855, 4112, 4369, 4626, 4883, 5140, 5397, 5654, 5911, 6168,
    6425, 6682, 6939, 7196, 7453, 7710, 7967, 8224, 8481, 8738, 8995, 9252,
    9509, 9766, 10023, 10280, 10537, 10794, 11051, 11308, 11565, 11822, 12079,
    12336, 12593, 12850, 13107, 13364, 13621, 13878, 14135, 14392, 14649,
    14906, 15163, 15420, 15677, 15934, 16191, 16448, 16705, 16962, 17219,
    17476, 17733, 17990, 18247, 18504, 18761, 19018, 19275, 19532, 19789,
    20046, 20303, 20560, 20817, 21074, 21331, 21588, 21845, 22102, 22359,
    22616, 22873, 23130, 23387, 23644, 23901, 24158, 24415, 24672, 24929,
    25186, 25443, 25700, 25957, 26214, 26471, 26728, 26985, 27242, 27499,
    27756, 28013, 28270, 28527, 28784, 29041, 29298, 29555, 29812, 30069,
    30326, 30583, 30840, 31097, 31354, 31611, 31868, 32125, 32382, 32639,
    32896, 33153, 33410, 33667, 33924, 34181, 34438, 34695, 34952, 35209,
    35466, 35723, 35980, 36237, 36494, 36751, 37008, 37265, 37522, 37779,
    38036, 38293, 38550, 38807, 39064, 39321, 39578, 39835, 40092, 40349,
    40606, 40863, 41120, 41377, 41634, 41891, 42148, 42405, 42662, 42919,
    43176, 43433, 43690, 43947, 44204, 44461, 44718, 44975, 45232, 45489,
    45746, 46003, 46260, 46517, 46774, 47031, 47288, 47545, 47802, 48059,
    48316, 48573, 48830, 49087, 49344, 49601, 49858, 50115, 50372, 50629,
    50886, 51143, 51400, 51657, 51914, 52171, 52428, 52685, 52942, 53199,
    53456, 53713, 53970, 54227, 54484, 54741, 54998, 55255, 55512, 55769,
    56026, 56283, 56540, 56797, 57054, 57311, 57568, 57825, 58082, 58339,
    58596, 58853, 59110, 59367, 59624, 59881, 60138, 60395, 60652, 60909,
    61166, 61423, 61680, 61937, 62194, 62451, 62708, 62965, 63222, 63479,
    63736, 63993, 64250, 64507, 64764, 65021, 65278, 65535
  };
  uint16_t night_light_off_green[] = {
    0, 257, 514, 771, 1028, 1285, 1542, 1799, 2056, 2313, 2570, 2827, 3084,
    3341, 3598, 3855, 4112, 4369, 4626, 4883, 5140, 5397, 5654, 5911, 6168,
    6425, 6682, 6939, 7196, 7453, 7710, 7967, 8224, 8481, 8738, 8995, 9252,
    9509, 9766, 10023, 10280, 10537, 10794, 11051, 11308, 11565, 11822, 12079,
    12336, 12593, 12850, 13107, 13364, 13621, 13878, 14135, 14392, 14649,
    14906, 15163, 15420, 15677, 15934, 16191, 16448, 16705, 16962, 17219,
    17476, 17733, 17990, 18247, 18504, 18761, 19018, 19275, 19532, 19789,
    20046, 20303, 20560, 20817, 21074, 21331, 21588, 21845, 22102, 22359,
    22616, 22873, 23130, 23387, 23644, 23901, 24158, 24415, 24672, 24929,
    25186, 25443, 25700, 25957, 26214, 26471, 26728, 26985, 27242, 27499,
    27756, 28013, 28270, 28527, 28784, 29041, 29298, 29555, 29812, 30069,
    30326, 30583, 30840, 31097, 31354, 31611, 31868, 32125, 32382, 32639,
    32896, 33153, 33410, 33667, 33924, 34181, 34438, 34695, 34952, 35209,
    35466, 35723, 35980, 36237, 36494, 36751, 37008, 37265, 37522, 37779,
    38036, 38293, 38550, 38807, 39064, 39321, 39578, 39835, 40092, 40349,
    40606, 40863, 41120, 41377, 41634, 41891, 42148, 42405, 42662, 42919,
    43176, 43433, 43690, 43947, 44204, 44461, 44718, 44975, 45232, 45489,
    45746, 46003, 46260, 46517, 46774, 47031, 47288, 47545, 47802, 48059,
    48316, 48573, 48830, 49087, 49344, 49601, 49858, 50115, 50372, 50629,
    50886, 51143, 51400, 51657, 51914, 52171, 52428, 52685, 52942, 53199,
    53456, 53713, 53970, 54227, 54484, 54741, 54998, 55255, 55512, 55769,
    56026, 56283, 56540, 56797, 57054, 57311, 57568, 57825, 58082, 58339,
    58596, 58853, 59110, 59367, 59624, 59881, 60138, 60395, 60652, 60909,
    61166, 61423, 61680, 61937, 62194, 62451, 62708, 62965, 63222, 63479,
    63736, 63993, 64250, 64507, 64764, 65021, 65278, 65535
  };
  uint16_t night_light_off_blue[] = {
    0, 257, 514, 771, 1028, 1285, 1542, 1799, 2056, 2313, 2570, 2827, 3084,
    3341, 3598, 3855, 4112, 4369, 4626, 4883, 5140, 5397, 5654, 5911, 6168,
    6425, 6682, 6939, 7196, 7453, 7710, 7967, 8224, 8481, 8738, 8995, 9252,
    9509, 9766, 10023, 10280, 10537, 10794, 11051, 11308, 11565, 11822, 12079,
    12336, 12593, 12850, 13107, 13364, 13621, 13878, 14135, 14392, 14649,
    14906, 15163, 15420, 15677, 15934, 16191, 16448, 16705, 16962, 17219,
    17476, 17733, 17990, 18247, 18504, 18761, 19018, 19275, 19532, 19789,
    20046, 20303, 20560, 20817, 21074, 21331, 21588, 21845, 22102, 22359,
    22616, 22873, 23130, 23387, 23644, 23901, 24158, 24415, 24672, 24929,
    25186, 25443, 25700, 25957, 26214, 26471, 26728, 26985, 27242, 27499,
    27756, 28013, 28270, 28527, 28784, 29041, 29298, 29555, 29812, 30069,
    30326, 30583, 30840, 31097, 31354, 31611, 31868, 32125, 32382, 32639,
    32896, 33153, 33410, 33667, 33924, 34181, 34438, 34695, 34952, 35209,
    35466, 35723, 35980, 36237, 36494, 36751, 37008, 37265, 37522, 37779,
    38036, 38293, 38550, 38807, 39064, 39321, 39578, 39835, 40092, 40349,
    40606, 40863, 41120, 41377, 41634, 41891, 42148, 42405, 42662, 42919,
    43176, 43433, 43690, 43947, 44204, 44461, 44718, 44975, 45232, 45489,
    45746, 46003, 46260, 46517, 46774, 47031, 47288, 47545, 47802, 48059,
    48316, 48573, 48830, 49087, 49344, 49601, 49858, 50115, 50372, 50629,
    50886, 51143, 51400, 51657, 51914, 52171, 52428, 52685, 52942, 53199,
    53456, 53713, 53970, 54227, 54484, 54741, 54998, 55255, 55512, 55769,
    56026, 56283, 56540, 56797, 57054, 57311, 57568, 57825, 58082, 58339,
    58596, 58853, 59110, 59367, 59624, 59881, 60138, 60395, 60652, 60909,
    61166, 61423, 61680, 61937, 62194, 62451, 62708, 62965, 63222, 63479,
    63736, 63993, 64250, 64507, 64764, 65021, 65278, 65535
  };
  /* Night light at 3305K */
  uint16_t night_light_on_red[] = {
    0, 257, 514, 771, 1028, 1285, 1542, 1799, 2056, 2313, 2570, 2827, 3084,
    3341, 3598, 3855, 4112, 4369, 4626, 4883, 5140, 5397, 5654, 5911, 6168,
    6425, 6682, 6939, 7196, 7453, 7710, 7967, 8224, 8481, 8738, 8995, 9252,
    9509, 9766, 10023, 10280, 10537, 10794, 11051, 11308, 11565, 11822, 12079,
    12336, 12593, 12850, 13107, 13364, 13621, 13878, 14135, 14392, 14649,
    14906, 15163, 15420, 15677, 15934, 16191, 16448, 16705, 16962, 17219,
    17476, 17733, 17990, 18247, 18504, 18761, 19018, 19275, 19532, 19789,
    20046, 20303, 20560, 20817, 21074, 21331, 21588, 21845, 22102, 22359,
    22616, 22873, 23130, 23387, 23644, 23901, 24158, 24415, 24672, 24929,
    25186, 25443, 25700, 25957, 26214, 26471, 26728, 26985, 27242, 27499,
    27756, 28013, 28270, 28527, 28784, 29041, 29298, 29555, 29812, 30069,
    30326, 30583, 30840, 31097, 31354, 31611, 31868, 32125, 32382, 32639,
    32896, 33153, 33410, 33667, 33924, 34181, 34438, 34695, 34952, 35209,
    35466, 35723, 35980, 36237, 36494, 36751, 37008, 37265, 37522, 37779,
    38036, 38293, 38550, 38807, 39064, 39321, 39578, 39835, 40092, 40349,
    40606, 40863, 41120, 41377, 41634, 41891, 42148, 42405, 42662, 42919,
    43176, 43433, 43690, 43947, 44204, 44461, 44718, 44975, 45232, 45489,
    45746, 46003, 46260, 46517, 46774, 47031, 47288, 47545, 47802, 48059,
    48316, 48573, 48830, 49087, 49344, 49601, 49858, 50115, 50372, 50629,
    50886, 51143, 51400, 51657, 51914, 52171, 52428, 52685, 52942, 53199,
    53456, 53713, 53970, 54227, 54484, 54741, 54998, 55255, 55512, 55769,
    56026, 56283, 56540, 56797, 57054, 57311, 57568, 57825, 58082, 58339,
    58596, 58853, 59110, 59367, 59624, 59881, 60138, 60395, 60652, 60909,
    61166, 61423, 61680, 61937, 62194, 62451, 62708, 62965, 63222, 63479,
    63736, 63993, 64250, 64507, 64764, 65021, 65278, 65535
  };
  uint16_t night_light_on_green[] = {
    0, 194, 389, 584, 779, 973, 1168, 1363, 1558, 1753, 1947, 2142, 2337, 2532,
    2727, 2921, 3116, 3311, 3506, 3701, 3895, 4090, 4285, 4480, 4675, 4869,
    5064, 5259, 5454, 5649, 5843, 6038, 6233, 6428, 6623, 6817, 7012, 7207,
    7402, 7597, 7791, 7986, 8181, 8376, 8571, 8765, 8960, 9155, 9350, 9545,
    9739, 9934, 10129, 10324, 10519, 10713, 10908, 11103, 11298, 11493, 11687,
    11882, 12077, 12272, 12467, 12661, 12856, 13051, 13246, 13441, 13635,
    13830, 14025, 14220, 14415, 14609, 14804, 14999, 15194, 15389, 15583,
    15778, 15973, 16168, 16363, 16557, 16752, 16947, 17142, 17337, 17531,
    17726, 17921, 18116, 18311, 18505, 18700, 18895, 19090, 19285, 19479,
    19674, 19869, 20064, 20259, 20453, 20648, 20843, 21038, 21233, 21427,
    21622, 21817, 22012, 22207, 22401, 22596, 22791, 22986, 23181, 23375,
    23570, 23765, 23960, 24155, 24349, 24544, 24739, 24934, 25129, 25323,
    25518, 25713, 25908, 26103, 26297, 26492, 26687, 26882, 27077, 27271,
    27466, 27661, 27856, 28051, 28245, 28440, 28635, 28830, 29025, 29219,
    29414, 29609, 29804, 29999, 30193, 30388, 30583, 30778, 30973, 31167,
    31362, 31557, 31752, 31947, 32141, 32336, 32531, 32726, 32921, 33115,
    33310, 33505, 33700, 33895, 34089, 34284, 34479, 34674, 34869, 35063,
    35258, 35453, 35648, 35843, 36037, 36232, 36427, 36622, 36817, 37011,
    37206, 37401, 37596, 37791, 37985, 38180, 38375, 38570, 38765, 38959,
    39154, 39349, 39544, 39739, 39933, 40128, 40323, 40518, 40713, 40907,
    41102, 41297, 41492, 41687, 41881, 42076, 42271, 42466, 42661, 42855,
    43050, 43245, 43440, 43635, 43829, 44024, 44219, 44414, 44609, 44803,
    44998, 45193, 45388, 45583, 45777, 45972, 46167, 46362, 46557, 46751,
    46946, 47141, 47336, 47531, 47725, 47920, 48115, 48310, 48505, 48699,
    48894, 49089, 49284, 49479, 49673
  };
  uint16_t night_light_on_blue[] = {
    0, 129, 258, 387, 516, 645, 775, 904, 1033, 1162, 1291, 1420, 1550, 1679,
    1808, 1937, 2066, 2196, 2325, 2454, 2583, 2712, 2841, 2971, 3100, 3229,
    3358, 3487, 3616, 3746, 3875, 4004, 4133, 4262, 4392, 4521, 4650, 4779,
    4908, 5037, 5167, 5296, 5425, 5554, 5683, 5813, 5942, 6071, 6200, 6329,
    6458, 6588, 6717, 6846, 6975, 7104, 7233, 7363, 7492, 7621, 7750, 7879,
    8009, 8138, 8267, 8396, 8525, 8654, 8784, 8913, 9042, 9171, 9300, 9430,
    9559, 9688, 9817, 9946, 10075, 10205, 10334, 10463, 10592, 10721, 10850,
    10980, 11109, 11238, 11367, 11496, 11626, 11755, 11884, 12013, 12142,
    12271, 12401, 12530, 12659, 12788, 12917, 13047, 13176, 13305, 13434,
    13563, 13692, 13822, 13951, 14080, 14209, 14338, 14467, 14597, 14726,
    14855, 14984, 15113, 15243, 15372, 15501, 15630, 15759, 15888, 16018,
    16147, 16276, 16405, 16534, 16664, 16793, 16922, 17051, 17180, 17309,
    17439, 17568, 17697, 17826, 17955, 18084, 18214, 18343, 18472, 18601,
    18730, 18860, 18989, 19118, 19247, 19376, 19505, 19635, 19764, 19893,
    20022, 20151, 20281, 20410, 20539, 20668, 20797, 20926, 21056, 21185,
    21314, 21443, 21572, 21701, 21831, 21960, 22089, 22218, 22347, 22477,
    22606, 22735, 22864, 22993, 23122, 23252, 23381, 23510, 23639, 23768,
    23898, 24027, 24156, 24285, 24414, 24543, 24673, 24802, 24931, 25060,
    25189, 25318, 25448, 25577, 25706, 25835, 25964, 26094, 26223, 26352,
    26481, 26610, 26739, 26869, 26998, 27127, 27256, 27385, 27515, 27644,
    27773, 27902, 28031, 28160, 28290, 28419, 28548, 28677, 28806, 28935,
    29065, 29194, 29323, 29452, 29581, 29711, 29840, 29969, 30098, 30227,
    30356, 30486, 30615, 30744, 30873, 31002, 31132, 31261, 31390, 31519,
    31648, 31777, 31907, 32036, 32165, 32294, 32423, 32552, 32682, 32811,
    32940
  };
  unsigned int temperature = 3305;

  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_off_green));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_off_blue));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_on_red));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_on_green));
  G_STATIC_ASSERT (G_N_ELEMENTS (night_light_off_red) ==
                   G_N_ELEMENTS (night_light_on_blue));

  edid_info = ANCOR_VX239_EDID;
  test_case_setup.outputs[0].edid_info = edid_info;
  test_case_setup.outputs[0].has_edid_info = TRUE;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  monitor = meta_monitor_manager_get_monitors (monitor_manager)->data;
  color_device = meta_color_manager_get_color_device (color_manager, monitor);
  g_assert_nonnull (color_device);

  while (!meta_color_device_is_ready (color_device))
    g_main_context_iteration (NULL, TRUE);

  set_night_light_temperature (6500);
  set_night_light_active (FALSE);
  path = g_test_get_filename (G_TEST_DIST, "icc-profiles", "sRGB.icc",
                              NULL);
  add_colord_system_profile (srgb_profile_id, path);
  color_profiles[0] = srgb_profile_id;
  set_colord_device_profiles (meta_color_device_get_id (color_device),
                              color_profiles, G_N_ELEMENTS (color_profiles));

  wait_for_profile_assigned (color_device, srgb_profile_id);

  output = meta_monitor_get_main_output (monitor);
  crtc = meta_output_get_assigned_crtc (output);
  crtc_test = META_CRTC_TEST (crtc);

  g_assert_cmpuint (crtc_test->gamma.size,
                    ==,
                    G_N_ELEMENTS (night_light_off_red));

  assert_gamma_array (night_light_off_red, crtc_test->gamma.red,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_off_green, crtc_test->gamma.green,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_off_blue, crtc_test->gamma.blue,
                      crtc_test->gamma.size);

  set_night_light_temperature (temperature);
  set_night_light_active (TRUE);
  wait_for_device_calibration_changed (color_device);

  assert_gamma_array (night_light_on_red, crtc_test->gamma.red,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_on_green, crtc_test->gamma.green,
                      crtc_test->gamma.size);
  assert_gamma_array (night_light_on_blue, crtc_test->gamma.blue,
                      crtc_test->gamma.size);
}

static MetaMonitorTestSetup *
create_stage_view_test_setup (MetaBackend *backend)
{
  return meta_create_monitor_test_setup (backend, &base_monitor_setup,
                                         MONITOR_TEST_FLAG_NO_STORED);
}

static void
on_before_tests (MetaContext *context)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaMonitorManagerTest *monitor_manager_test =
    META_MONITOR_MANAGER_TEST (monitor_manager);
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);
  MonitorTestCaseSetup test_case_setup = base_monitor_setup;
  MetaMonitorTestSetup *test_setup;

  test_case_setup.n_outputs = 0;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

  while (!meta_color_manager_is_ready (color_manager))
    g_main_context_iteration (NULL, TRUE);
}

static void
add_color_test (const char  *test_path,
                GTestFunc    test_func)
{
  g_test_add_vtable (test_path, 0, NULL,
                     (GTestFixtureFunc) prepare_color_test,
                     (GTestFixtureFunc) test_func,
                     (GTestFixtureFunc) finish_color_test);
}

static void
init_tests (void)
{
  meta_init_monitor_test_setup (create_stage_view_test_setup);

  add_color_test ("/color-management/device/basic",
                  meta_test_color_management_device_basic);
  add_color_test ("/color-management/device/no-gamma",
                  meta_test_color_management_device_no_gamma);
  add_color_test ("/color-management/profile/device",
                  meta_test_color_management_profile_device);
  add_color_test ("/color-management/profile/device-bogus",
                  meta_test_color_management_profile_device_bogus);
  add_color_test ("/color-management/profile/system",
                  meta_test_color_management_profile_system);
  add_color_test ("/color-management/profile/efivar",
                  meta_test_color_management_profile_efivar);
  add_color_test ("/color-management/night-light/calibrated",
                  meta_test_color_management_night_light_calibrated);
  add_color_test ("/color-management/night-light/uncalibrated",
                  meta_test_color_management_night_light_uncalibrated);
}

int
main (int argc, char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_TEST,
                                      META_CONTEXT_TEST_FLAG_NONE);

  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
