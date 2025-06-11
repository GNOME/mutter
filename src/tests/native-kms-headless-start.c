/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2017 Red Hat, Inc.
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

#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "backends/meta-udev.h"
#include "backends/native/meta-backend-native.h"
#include "core/display-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/drm-mock/drm-mock.h"
#include "tests/meta-kms-test-utils.h"
#include "tests/meta-monitor-manager-test.h"

static MetaContext *test_context;

static void
meta_test_headless_start (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GList *gpus;
  MetaGpu *gpu;

  gpus = meta_backend_get_gpus (backend);
  g_assert_cmpint ((int) g_list_length (gpus), ==, 1);

  gpu = gpus->data;
  g_assert_null (meta_gpu_get_outputs (gpu));
  g_assert_null (monitor_manager->monitors);
  g_assert_null (monitor_manager->logical_monitors);

  g_assert_cmpint (monitor_manager->screen_width,
                   ==,
                   META_MONITOR_MANAGER_MIN_SCREEN_WIDTH);
  g_assert_cmpint (monitor_manager->screen_height,
                   ==,
                   META_MONITOR_MANAGER_MIN_SCREEN_HEIGHT);
}

static void
meta_test_headless_monitor_getters (void)
{
  MetaDisplay *display;
  int index;

  display = meta_context_get_display (test_context);

  index = meta_display_get_monitor_index_for_rect (display,
                                                   &(MtkRectangle) { 0 });
  g_assert_cmpint (index, ==, -1);
}

static void
meta_test_headless_monitor_connect (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaUdev *udev = meta_backend_get_udev (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  g_autoptr (GUdevDevice) udev_device = NULL;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle monitor_layout;
  ClutterActor *stage;

  drm_mock_unset_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR);

  udev_device = meta_get_test_udev_device (udev);
  g_signal_emit_by_name (udev, "hotplug", udev_device);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpint (g_list_length (logical_monitors), ==, 1);
  logical_monitor = g_list_first (logical_monitors)->data;
  monitor_layout = meta_logical_monitor_get_layout (logical_monitor);

  g_assert_cmpint (monitor_manager->screen_width, ==, monitor_layout.width);
  g_assert_cmpint (monitor_manager->screen_height, ==, monitor_layout.height);

  stage = meta_backend_get_stage (backend);
  g_assert_cmpint ((int) clutter_actor_get_width (stage), ==, monitor_layout.width);
  g_assert_cmpint ((int) clutter_actor_get_height (stage), ==, monitor_layout.height);
}

static MetaMonitorTestSetup *
create_headless_test_setup (MetaBackend *backend)
{
  return g_new0 (MetaMonitorTestSetup, 1);
}

static void
init_tests (void)
{
  meta_init_monitor_test_setup (create_headless_test_setup);

  g_test_add_func ("/headless-start/start", meta_test_headless_start);
  g_test_add_func ("/headless-start/monitor-getters",
                   meta_test_headless_monitor_getters);
  g_test_add_func ("/headless-start/connect",
                   meta_test_headless_monitor_connect);
}

static void
disconnect_connector_filter (gpointer resource,
                             gpointer user_data)
{
  drmModeConnector *drm_connector = resource;

  drm_connector->connection = DRM_MODE_DISCONNECTED;
}

int
main (int argc, char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_VKMS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  test_context = context;

  drm_mock_set_resource_filter (DRM_MOCK_CALL_FILTER_GET_CONNECTOR,
                                disconnect_connector_filter, NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
