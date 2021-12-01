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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include "backends/meta-color-device.h"
#include "backends/meta-color-manager-private.h"
#include "backends/meta-color-profile.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-monitor-test-utils.h"

static MetaContext *test_context;

/* Profile ID is 'icc-$(md5sum sRGB.icc)' */
#define SRGB_ICC_PROFILE_ID "icc-112034c661b5e0c91c51f109684612a0";

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
    .gamma = 2.200000, \
    .red_x = 0.683594, \
    .red_y = 0.312500, \
    .green_x = 0.255859, \
    .green_y = 0.685547, \
    .blue_x = 0.139648, \
    .blue_y = 0.056641, \
    .white_x = 0.313477, \
    .white_y = 0.326172, \
  })

/* Extracted from a 'Ancor Communications Inc, VX239, ECLMRS004144' monitor. */
#define ANCOR_VX239_EDID (\
  (MetaEdidInfo) { \
    .gamma = 2.200000, \
    .red_x = 0.651367, \
    .red_y = 0.335938, \
    .green_x = 0.321289, \
    .green_y = 0.614258, \
    .blue_x = 0.154297, \
    .blue_y = 0.063477, \
    .white_x = 0.313477, \
    .white_y = 0.329102, \
  })

static GDBusProxy *
get_colord_mock_proxy (void)
{
  GDBusProxy *proxy;
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) ret = NULL;

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

  test_case_setup.n_outputs = 0;
  test_setup = meta_create_monitor_test_setup (backend, &test_case_setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_monitor_manager_test_emulate_hotplug (monitor_manager_test, test_setup);

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

      g_assert_cmpfloat_with_epsilon (expected_edid_info->gamma,
                                      monitor_edid_info->gamma,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->red_x,
                                      monitor_edid_info->red_x,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->red_y,
                                      monitor_edid_info->red_y,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->green_x,
                                      monitor_edid_info->green_x,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->green_y,
                                      monitor_edid_info->green_y,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->blue_x,
                                      monitor_edid_info->blue_x,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->blue_y,
                                      monitor_edid_info->blue_y,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->white_x,
                                      monitor_edid_info->white_x,
                                      FLT_EPSILON);
      g_assert_cmpfloat_with_epsilon (expected_edid_info->white_y,
                                      monitor_edid_info->white_y,
                                      FLT_EPSILON);

      color_device = meta_color_manager_get_color_device (color_manager,
                                                          monitor);
      g_assert_nonnull (color_device);

      g_assert (meta_color_device_get_monitor (color_device) == monitor);
    }
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

  path = g_test_get_filename (G_TEST_DIST, "tests", "icc-profiles", "sRGB.icc",
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
  MetaColorManager *color_manager =
    meta_backend_get_color_manager (backend);

  while (!meta_color_manager_is_ready (color_manager))
    g_main_context_iteration (NULL, TRUE);
}

static void
init_tests (void)
{
  meta_init_monitor_test_setup (create_stage_view_test_setup);

  g_test_add_func ("/color-management/device/basic",
                   meta_test_color_management_device_basic);
  g_test_add_func ("/color-management/profile/device",
                   meta_test_color_management_profile_device);
  g_test_add_func ("/color-management/profile/system",
                   meta_test_color_management_profile_system);
}

int
main (int argc, char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  char *path;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_NESTED,
                                      META_CONTEXT_TEST_FLAG_NONE);

  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  path = g_test_build_filename (G_TEST_BUILT,
                                "tests",
                                "share",
                                NULL);
  g_setenv ("XDG_DATA_HOME", path, TRUE);
  g_free (path);

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
