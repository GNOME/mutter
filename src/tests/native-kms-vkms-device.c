/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include "backends/native/meta-backend-native-private.h"
#include "backends/native/meta-kms-connector.h"
#include "backends/native/meta-kms-crtc.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms-plane.h"
#include "backends/native/meta-kms.h"
#include "tests/meta-context-test-private.h"
#include "tests/meta-kms-test-utils.h"

static MetaContext *test_context;

static MetaKmsDevice *
find_kms_device_for_udev (MetaKms     *kms,
                          GUdevDevice *udev_device)
{
  const char *device_file;
  GList *l;

  device_file = g_udev_device_get_device_file (udev_device);

  for (l = meta_kms_get_devices (kms); l; l = l->next)
    {
      MetaKmsDevice *kms_device = META_KMS_DEVICE (l->data);

      if (g_strcmp0 (meta_kms_device_get_path (kms_device), device_file) == 0)
        return kms_device;
    }

  return NULL;
}

static void
wait_for_kms_device_removed (MetaKms    *kms,
                             const char *device_path)
{
  while (TRUE)
    {
      gboolean found = FALSE;

      for (GList *l = meta_kms_get_devices (kms); l; l = l->next)
        {
          MetaKmsDevice *kms_device = META_KMS_DEVICE (l->data);

          if (g_strcmp0 (meta_kms_device_get_path (kms_device),
                         device_path) == 0)
            {
              found = TRUE;
              break;
            }
        }

      if (!found)
        return;

      g_main_context_iteration (NULL, TRUE);
    }
}

static MetaKmsDevice *
wait_for_new_kms_device (const char *vkms_name)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaUdev *udev = meta_backend_get_udev (backend);
  g_autofree char *expected_prefix = NULL;

  expected_prefix = g_strdup_printf ("/devices/faux/%s/", vkms_name);

  for (;;)
    {
      g_autolist (GObject) udev_devices = NULL;
      GList *l;

      udev_devices = meta_udev_list_drm_devices (udev,
                                                 META_UDEV_DEVICE_TYPE_CARD,
                                                 NULL);

      for (l = udev_devices; l; l = l->next)
        {
          GUdevDevice *udev_device = l->data;
          const char *devpath;
          MetaKmsDevice *kms_device;

          devpath = g_udev_device_get_property (udev_device, "DEVPATH");
          if (!devpath || !g_str_has_prefix (devpath, expected_prefix))
            continue;

          kms_device = find_kms_device_for_udev (kms, udev_device);
          if (kms_device)
            return kms_device;
        }

      g_main_context_iteration (NULL, TRUE);
    }
}

static const MetaVkmsDeviceConfig device_config = {
  .name = "mutter-overlay-test",
  .crtcs = {
    { .name = "crtc0", .writeback = TRUE },
  },
  .planes = {
    {
      .name = "primary",
      .type = META_VKMS_PLANE_TYPE_PRIMARY,
      .possible_crtcs = { "crtc0" },
    },
    {
      .name = "cursor",
      .type = META_VKMS_PLANE_TYPE_CURSOR,
      .possible_crtcs = { "crtc0" },
    },
    {
      .name = "overlay0",
      .type = META_VKMS_PLANE_TYPE_OVERLAY,
      .possible_crtcs = { "crtc0" },
    },
  },
  .encoders = {
    { .name = "encoder0", .possible_crtcs = { "crtc0" } },
  },
  .connectors = {
    { .name = "connector0", .possible_encoders = { "encoder0" } },
  },
};

static void
meta_test_vkms_device_overlay_plane (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  GList *devices;
  MetaKmsDevice *device;
  GList *planes, *l;
  MetaKmsCrtc *crtc;
  MetaKmsPlane *primary_plane, *cursor_plane;
  size_t n_overlay_planes;

  devices = meta_kms_get_devices (kms);
  g_assert_cmpuint (g_list_length (devices), ==, 1);
  device = META_KMS_DEVICE (devices->data);

  g_assert_cmpstr (meta_kms_device_get_driver_name (device), ==, "vkms");

  crtc = meta_get_test_kms_crtc (device);
  g_assert_nonnull (crtc);

  planes = meta_kms_device_get_planes (device);
  g_assert_cmpuint (g_list_length (planes), ==, 3);

  primary_plane = meta_get_primary_test_plane_for (device, crtc);
  g_assert_nonnull (primary_plane);

  cursor_plane = meta_get_cursor_test_plane_for (device, crtc);
  g_assert_nonnull (cursor_plane);

  n_overlay_planes = 0;
  for (l = planes; l; l = l->next)
    {
      MetaKmsPlane *plane = META_KMS_PLANE (l->data);

      if (meta_kms_plane_get_plane_type (plane) == META_KMS_PLANE_TYPE_OVERLAY)
        {
          g_assert_true (meta_kms_plane_is_usable_with (plane, crtc));
          n_overlay_planes++;
        }
    }

  g_assert_cmpuint (n_overlay_planes, ==, 1);
}

static const MetaVkmsDeviceConfig second_device_config = {
  .name = "mutter-second-test",
  .crtcs = {
    { .name = "crtc0", .writeback = TRUE },
  },
  .planes = {
    {
      .name = "primary",
      .type = META_VKMS_PLANE_TYPE_PRIMARY,
      .possible_crtcs = { "crtc0" },
    },
    {
      .name = "cursor",
      .type = META_VKMS_PLANE_TYPE_CURSOR,
      .possible_crtcs = { "crtc0" },
    },
  },
  .encoders = {
    { .name = "encoder0", .possible_crtcs = { "crtc0" } },
  },
  .connectors = {
    { .name = "connector0", .possible_encoders = { "encoder0" } },
  },
};

static void
meta_test_vkms_device_second_device (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  g_autoptr (MetaVkmsDevice) second_device = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *device_path = NULL;
  MetaKmsDevice *kms_device;
  GList *crtcs, *planes, *connectors;
  MetaKmsCrtc *crtc;
  MetaKmsPlane *primary_plane, *cursor_plane;
  gboolean success;

  second_device = meta_vkms_device_new (&second_device_config);

  success = meta_vkms_device_enable (second_device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  kms_device = wait_for_new_kms_device ("mutter-second-test");

  g_assert_cmpstr (meta_kms_device_get_driver_name (kms_device), ==, "vkms");

  crtcs = meta_kms_device_get_crtcs (kms_device);
  g_assert_cmpuint (g_list_length (crtcs), ==, 1);
  crtc = META_KMS_CRTC (crtcs->data);

  connectors = meta_kms_device_get_connectors (kms_device);
  g_assert_cmpuint (g_list_length (connectors), ==, 1);

  planes = meta_kms_device_get_planes (kms_device);
  g_assert_cmpuint (g_list_length (planes), ==, 2);

  primary_plane = meta_get_primary_test_plane_for (kms_device, crtc);
  g_assert_nonnull (primary_plane);

  cursor_plane = meta_get_cursor_test_plane_for (kms_device, crtc);
  g_assert_nonnull (cursor_plane);

  g_assert_true (primary_plane != cursor_plane);

  device_path = g_strdup (meta_kms_device_get_path (kms_device));

  success = meta_vkms_device_disable (second_device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  wait_for_kms_device_removed (kms, device_path);
}

#define N_OVERLAY_PLANES 4

static void
meta_test_vkms_device_programmatic (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaVkmsDeviceConfig config = {
    .name = "mutter-programmatic-test",
    .crtcs = {
      { .name = "crtc0", .writeback = TRUE },
    },
    .encoders = {
      { .name = "encoder0", .possible_crtcs = { "crtc0" } },
    },
    .connectors = {
      { .name = "connector0", .possible_encoders = { "encoder0" } },
    },
  };
  g_autoptr (MetaVkmsDevice) device = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *device_path = NULL;
  MetaKmsDevice *kms_device;
  GList *crtcs, *planes, *connectors, *l;
  MetaKmsCrtc *crtc;
  size_t n_planes, n_overlay_planes, n, i;
  gboolean success;

  n = 0;
  config.planes[n++] = (MetaVkmsPlaneConfig) {
    .name = "primary",
    .type = META_VKMS_PLANE_TYPE_PRIMARY,
    .possible_crtcs = { "crtc0" },
  };
  config.planes[n++] = (MetaVkmsPlaneConfig) {
    .name = "cursor",
    .type = META_VKMS_PLANE_TYPE_CURSOR,
    .possible_crtcs = { "crtc0" },
  };

  for (i = 0; i < N_OVERLAY_PLANES; i++)
    {
      g_autofree char *name = NULL;

      name = g_strdup_printf ("overlay%zu", i);
      config.planes[n++] = (MetaVkmsPlaneConfig) {
        .name = g_intern_string (name),
        .type = META_VKMS_PLANE_TYPE_OVERLAY,
        .possible_crtcs = { "crtc0" },
      };
    }

  device = meta_vkms_device_new (&config);

  success = meta_vkms_device_enable (device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  kms_device = wait_for_new_kms_device ("mutter-programmatic-test");

  g_assert_cmpstr (meta_kms_device_get_driver_name (kms_device), ==, "vkms");

  crtcs = meta_kms_device_get_crtcs (kms_device);
  g_assert_cmpuint (g_list_length (crtcs), ==, 1);
  crtc = META_KMS_CRTC (crtcs->data);

  connectors = meta_kms_device_get_connectors (kms_device);
  g_assert_cmpuint (g_list_length (connectors), ==, 1);

  n_planes = n;
  planes = meta_kms_device_get_planes (kms_device);
  g_assert_cmpuint (g_list_length (planes), ==, n_planes);

  g_assert_nonnull (meta_get_primary_test_plane_for (kms_device, crtc));
  g_assert_nonnull (meta_get_cursor_test_plane_for (kms_device, crtc));

  n_overlay_planes = 0;
  for (l = planes; l; l = l->next)
    {
      MetaKmsPlane *plane = META_KMS_PLANE (l->data);

      if (meta_kms_plane_get_plane_type (plane) != META_KMS_PLANE_TYPE_OVERLAY)
        continue;

      g_assert_true (meta_kms_plane_is_usable_with (plane, crtc));
      n_overlay_planes++;
    }
  g_assert_cmpuint (n_overlay_planes, ==, N_OVERLAY_PLANES);

  device_path = g_strdup (meta_kms_device_get_path (kms_device));

  success = meta_vkms_device_disable (device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  wait_for_kms_device_removed (kms, device_path);
}

static void
meta_test_vkms_device_reconfigure (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaBackendNative *backend_native = META_BACKEND_NATIVE (backend);
  MetaKms *kms = meta_backend_native_get_kms (backend_native);
  MetaVkmsDeviceConfig config_a = {
    .name = "mutter-reconfigure-test",
    .crtcs = {
      { .name = "crtc0", .writeback = TRUE },
    },
    .planes = {
      {
        .name = "primary",
        .type = META_VKMS_PLANE_TYPE_PRIMARY,
        .possible_crtcs = { "crtc0" },
      },
      {
        .name = "cursor",
        .type = META_VKMS_PLANE_TYPE_CURSOR,
        .possible_crtcs = { "crtc0" },
      },
    },
    .encoders = {
      { .name = "encoder0", .possible_crtcs = { "crtc0" } },
    },
    .connectors = {
      { .name = "connector0", .possible_encoders = { "encoder0" } },
    },
  };
  MetaVkmsDeviceConfig config_b = {
    .name = "mutter-reconfigure-test",
    .crtcs = {
      { .name = "crtc0", .writeback = TRUE },
    },
    .planes = {
      {
        .name = "primary",
        .type = META_VKMS_PLANE_TYPE_PRIMARY,
        .possible_crtcs = { "crtc0" },
      },
      {
        .name = "cursor",
        .type = META_VKMS_PLANE_TYPE_CURSOR,
        .possible_crtcs = { "crtc0" },
      },
      {
        .name = "overlay0",
        .type = META_VKMS_PLANE_TYPE_OVERLAY,
        .possible_crtcs = { "crtc0" },
      },
    },
    .encoders = {
      { .name = "encoder0", .possible_crtcs = { "crtc0" } },
    },
    .connectors = {
      { .name = "connector0", .possible_encoders = { "encoder0" } },
    },
  };
  g_autoptr (MetaVkmsDevice) device = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *device_path = NULL;
  MetaKmsDevice *kms_device;
  GList *planes;
  gboolean success;

  device = meta_vkms_device_new (&config_a);

  success = meta_vkms_device_enable (device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  kms_device = wait_for_new_kms_device ("mutter-reconfigure-test");
  planes = meta_kms_device_get_planes (kms_device);
  g_assert_cmpuint (g_list_length (planes), ==, 2);

  device_path = g_strdup (meta_kms_device_get_path (kms_device));

  success = meta_vkms_device_disable (device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  wait_for_kms_device_removed (kms, device_path);

  g_clear_pointer (&device, meta_vkms_device_free);
  device = meta_vkms_device_new (&config_b);

  success = meta_vkms_device_enable (device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  kms_device = wait_for_new_kms_device ("mutter-reconfigure-test");
  planes = meta_kms_device_get_planes (kms_device);
  g_assert_cmpuint (g_list_length (planes), ==, 3);

  g_clear_pointer (&device_path, g_free);
  device_path = g_strdup (meta_kms_device_get_path (kms_device));

  success = meta_vkms_device_disable (device, &error);
  g_assert_no_error (error);
  g_assert_true (success);

  wait_for_kms_device_removed (kms, device_path);
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/native/kms/vkms-device/overlay-plane",
                   meta_test_vkms_device_overlay_plane);
  g_test_add_func ("/backends/native/kms/vkms-device/second-device",
                   meta_test_vkms_device_second_device);
  g_test_add_func ("/backends/native/kms/vkms-device/programmatic",
                   meta_test_vkms_device_programmatic);
  g_test_add_func ("/backends/native/kms/vkms-device/reconfigure",
                   meta_test_vkms_device_reconfigure);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                              META_CONTEXT_TEST_FLAG_NO_X11);

  meta_context_test_set_vkms_device_configs (META_CONTEXT_TEST (context),
                                             &device_config, 1);

  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
