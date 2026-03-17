/*
 * Copyright (C) 2026 Red Hat, Inc.
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

#include "meta-wayland-test-runner.h"

#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/native/meta-backend-native.h"
#include "backends/native/meta-kms-device.h"
#include "backends/native/meta-kms.h"
#include "meta-test-utils.h"
#include "meta-test/meta-context-test.h"

MetaContext *test_context;
MetaWaylandTestDriver *test_driver;

static MetaVirtualMonitor *virtual_monitor;

static void
on_before_tests (MetaContext *context)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (context);
  MetaBackend *backend = meta_context_get_backend (context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
#ifdef MUTTER_PRIVILEGED_TEST
  MetaKms *kms = meta_backend_native_get_kms (META_BACKEND_NATIVE (backend));
  MetaKmsDevice *kms_device = meta_kms_get_devices (kms)->data;
#endif

  test_driver = meta_wayland_test_driver_new (compositor);

#ifdef MUTTER_PRIVILEGED_TEST
  meta_wayland_test_driver_set_property (test_driver,
                                         "gpu-path",
                                         meta_kms_device_get_path (kms_device));

  meta_set_custom_monitor_config_full (backend,
                                       "vkms-640x480.xml",
                                       META_MONITORS_CONFIG_FLAG_NONE);
#else
  virtual_monitor = meta_create_test_monitor (context,
                                              640, 480, 60.0);
#endif
  meta_monitor_manager_reload (monitor_manager);
}

static void
on_after_tests (MetaContext *context)
{
  g_clear_object (&test_driver);
  g_clear_object (&virtual_monitor);
}

int
meta_run_wayland_tests (int        argc,
                        char      *argv[],
                        GTestFunc  init_tests)
{
  g_autoptr (MetaContext) context = NULL;
  MetaTestRunFlags test_run_flags;

#ifdef MUTTER_PRIVILEGED_TEST
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
#else
  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
#endif
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));
  meta_context_test_set_background_color (META_CONTEXT_TEST (context),
                                          COGL_COLOR_INIT (255, 255, 255, 255));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), context);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), context);

#ifdef MUTTER_PRIVILEGED_TEST
  test_run_flags = META_TEST_RUN_FLAG_CAN_SKIP;
#else
  test_run_flags = META_TEST_RUN_FLAG_NONE;
#endif
  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      test_run_flags);
}
