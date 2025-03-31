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

#include <glib.h>

#include "backends/meta-monitor-config-manager.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms.h"
#include "backends/native/meta-kms-device.h"
#include "meta-test/meta-context-test.h"
#include "tests/drm-mock/drm-mock.h"
#include "tests/meta-test-utils.h"

static MetaContext *test_context;

static void
fake_udev_hotplug (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaUdev *udev = meta_backend_get_udev (backend);
  g_autolist (GUdevDevice) devices = NULL;
  g_autoptr (GError) error = NULL;
  GList *l;

  devices = meta_udev_list_drm_devices (udev,
                                        META_UDEV_DEVICE_TYPE_CARD,
                                        &error);
  g_assert_no_error (error);
  g_assert_nonnull (devices);

  for (l = devices; l; l = l->next)
    {
      GUdevDevice *device = l->data;

      g_signal_emit_by_name (udev, "hotplug", device);
    }
}

static void
disconnect_connector_filter (gpointer resource,
                             gpointer user_data)
{
  drmModeConnector *drm_connector = resource;

  drm_connector->connection = DRM_MODE_DISCONNECTED;
}

static void
test_drm_lease_lease_suspend_resume (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);

  meta_backend_pause (backend);
  drm_mock_set_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR,
                                disconnect_connector_filter, NULL);
  fake_udev_hotplug ();
  meta_backend_resume (backend);

  drm_mock_unset_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR);
  fake_udev_hotplug ();
}

static void
test_drm_lease_lease_suspend_no_resume (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);

  drm_mock_set_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR,
                                disconnect_connector_filter, NULL);
  fake_udev_hotplug ();
  meta_backend_pause (backend);

  drm_mock_unset_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR);
  fake_udev_hotplug ();
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/drm-lease/suspend-resume",
                   test_drm_lease_lease_suspend_resume);
  g_test_add_func ("/wayland/drm-lease/suspend-no-resume",
                   test_drm_lease_lease_suspend_no_resume);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
