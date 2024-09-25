/*
 * Copyright (C) 2024 Red Hat, Inc.
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
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;

static void
test_drm_lease_client_connection (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client = meta_wayland_test_client_new_with_args (test_context,
                                                                "drm-lease",
                                                                "client-connection",
                                                                NULL);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
test_drm_lease_release_device (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client = meta_wayland_test_client_new_with_args (test_context,
                                                                "drm-lease",
                                                                "release-device",
                                                                NULL);
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
test_drm_lease_lease_request (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client = meta_wayland_test_client_new_with_args (test_context,
                                                                "drm-lease",
                                                                "lease-request",
                                                                NULL);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
test_drm_lease_lease_leased_connector (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client = meta_wayland_test_client_new_with_args (test_context,
                                                                "drm-lease",
                                                                "lease-leased-connector",
                                                                NULL);
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "Failed to create lease from connector list:*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
test_drm_lease_lease_duplicated_connector (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client = meta_wayland_test_client_new_with_args (test_context,
                                                                "drm-lease",
                                                                "lease-duplicated-connector",
                                                                NULL);
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
test_drm_lease_lease_no_connectors (void)
{
  MetaWaylandTestClient *wayland_test_client;

  wayland_test_client = meta_wayland_test_client_new_with_args (test_context,
                                                                "drm-lease",
                                                                "lease-no-connectors",
                                                                NULL);
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  meta_wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/drm-lease/client-connection",
                   test_drm_lease_client_connection);
  g_test_add_func ("/wayland/drm-lease/release-device",
                   test_drm_lease_release_device);
  g_test_add_func ("/wayland/drm-lease/lease-request",
                   test_drm_lease_lease_request);
  g_test_add_func ("/wayland/drm-lease/lease-leased-connector",
                   test_drm_lease_lease_leased_connector);
  g_test_add_func ("/wayland/drm-lease/lease-duplicated-connector",
                   test_drm_lease_lease_duplicated_connector);
  g_test_add_func ("/wayland/drm-lease/lease-no-connectors",
                   test_drm_lease_lease_no_connectors);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsDevice *kms_device = meta_kms_get_devices (kms)->data;

  test_driver = meta_wayland_test_driver_new (compositor);

  meta_wayland_test_driver_set_property (test_driver,
                                         "gpu-path",
                                         meta_kms_device_get_path (kms_device));

  meta_set_custom_monitor_config_full (backend,
                                       "vkms-640x480.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);

  meta_monitor_manager_reload (monitor_manager);
}

static void
on_after_tests (void)
{
  g_clear_object (&test_driver);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

#ifndef MUTTER_PRIVILEGED_TEST
  return 0;
#endif

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
