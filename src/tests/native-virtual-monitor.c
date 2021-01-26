/*
 * Copyright (C) 2021 Red Hat Inc.
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

#include "tests/native-virtual-monitor.h"

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-config-manager.h"
#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-renderer-native.h"
#include "tests/meta-ref-test.h"

static void
meta_test_virtual_monitor_create (void)
{
  MetaBackend *backend = meta_get_backend ();
  MetaMonitorManager *monitor_manager = meta_backend_get_monitor_manager (backend);
  MetaMonitorConfigManager *config_manager =
    meta_monitor_manager_get_config_manager (monitor_manager);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  MetaVirtualMonitor *virtual_monitor;
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  GError *error = NULL;
  GList *monitors;
  MetaMonitor *monitor;
  MetaMonitorsConfig *monitors_config;
  GList *logical_monitors;
  GList *logical_monitor_monitors;
  GList *views;
  int i;
  ClutterActor *actor;

  g_assert_null (meta_monitor_config_manager_get_current (config_manager));
  g_assert_null (meta_monitor_manager_get_logical_monitors (monitor_manager));
  g_assert_null (meta_monitor_manager_get_monitors (monitor_manager));
  g_assert_null (meta_renderer_get_views (renderer));

  monitor_info = meta_virtual_monitor_info_new (80, 60, 60.0,
                                                "MetaTestVendor",
                                                "MetaVirtualMonitor",
                                                "0x1234");
  virtual_monitor = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                 monitor_info,
                                                                 &error);
  if (!virtual_monitor)
    g_error ("Failed to create virtual monitor: %s", error->message);

  meta_monitor_manager_reload (monitor_manager);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpint (g_list_length (monitors), ==, 1);
  monitor = META_MONITOR (monitors->data);
  g_assert_cmpstr (meta_monitor_get_vendor (monitor), ==, "MetaTestVendor");
  g_assert_cmpstr (meta_monitor_get_product (monitor), ==, "MetaVirtualMonitor");
  g_assert_cmpstr (meta_monitor_get_serial (monitor), ==, "0x1234");
  g_assert (meta_monitor_get_main_output (monitor) ==
            meta_virtual_monitor_get_output (virtual_monitor));

  monitors_config = meta_monitor_manager_ensure_configured (monitor_manager);
  g_assert_nonnull (monitors_config);
  g_assert_cmpint (g_list_length (monitors_config->logical_monitor_configs),
                   ==,
                   1);

  g_assert_cmpint (g_list_length (monitors_config->disabled_monitor_specs),
                   ==,
                   0);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpint (g_list_length (logical_monitors), ==, 1);
  logical_monitor_monitors =
    meta_logical_monitor_get_monitors (logical_monitors->data);
  g_assert_cmpint (g_list_length (logical_monitor_monitors), ==, 1);
  g_assert (logical_monitor_monitors->data == monitor);

  views = meta_renderer_get_views (renderer);
  g_assert_cmpint (g_list_length (views), ==, 1);

  for (i = 0; i < 5; i++)
    {
      meta_ref_test_verify_view (CLUTTER_STAGE_VIEW (views->data),
                                 g_test_get_path (), 0,
                                 meta_ref_test_determine_ref_test_flag ());
    }

  actor = clutter_actor_new ();
  clutter_actor_set_position (actor, 10, 10);
  clutter_actor_set_size (actor, 40, 40);
  clutter_actor_set_background_color (actor, CLUTTER_COLOR_LightSkyBlue);
  clutter_actor_add_child (meta_backend_get_stage (backend), actor);

  for (i = 0; i < 5; i++)
    {
      meta_ref_test_verify_view (CLUTTER_STAGE_VIEW (views->data),
                                 g_test_get_path (), 1,
                                 meta_ref_test_determine_ref_test_flag ());
    }

  g_object_unref (virtual_monitor);
  meta_monitor_manager_reload (monitor_manager);

  g_assert_null (meta_monitor_manager_ensure_configured (monitor_manager));
  g_assert_null (meta_monitor_manager_get_logical_monitors (monitor_manager));
  g_assert_null (meta_monitor_manager_get_monitors (monitor_manager));
  g_assert_null (meta_renderer_get_views (renderer));

  clutter_actor_destroy (actor);
}

void
init_virtual_monitor_tests (void)
{
  g_test_add_func ("/backends/native/virtual-monitor/create",
                   meta_test_virtual_monitor_create);
}
