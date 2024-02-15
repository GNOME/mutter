/*
 * Copyright (C) 2021 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/meta-virtual-monitor.h"
#include "backends/native/meta-renderer-native.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-ref-test.h"

static MetaContext *test_context;

static MetaVirtualMonitor *virtual_monitor;

static void
setup_test_environment (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaSettings *settings = meta_backend_get_settings (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  g_autoptr (MetaVirtualMonitorInfo) monitor_info = NULL;
  GError *error = NULL;
  GList *views;

  meta_settings_override_experimental_features (settings);
  meta_settings_enable_experimental_feature (
    settings,
    META_EXPERIMENTAL_FEATURE_SCALE_MONITOR_FRAMEBUFFER);

  monitor_info = meta_virtual_monitor_info_new (100, 100, 60.0,
                                                "MetaTestVendor",
                                                "MetaVirtualMonitor",
                                                "0x1234");
  virtual_monitor = meta_monitor_manager_create_virtual_monitor (monitor_manager,
                                                                 monitor_info,
                                                                 &error);
  if (!virtual_monitor)
    g_error ("Failed to create virtual monitor: %s", error->message);

  meta_monitor_manager_reload (monitor_manager);

  views = meta_renderer_get_views (renderer);
  g_assert_cmpint (g_list_length (views), ==, 1);
}

static void
tear_down_test_environment (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);

  g_object_unref (virtual_monitor);
  meta_monitor_manager_reload (monitor_manager);
}

static ClutterStageView *
get_view (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);

  return CLUTTER_STAGE_VIEW (meta_renderer_get_views (renderer)->data);
}

static void
meta_test_ref_test_sanity (void)
{
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  ClutterActor *actor1;
  ClutterActor *actor2;

  meta_ref_test_verify_view (get_view (),
                             g_test_get_path (), 0,
                             meta_ref_test_determine_ref_test_flag ());

  actor1 = clutter_actor_new ();
  clutter_actor_set_position (actor1, 10, 10);
  clutter_actor_set_size (actor1, 50, 50);
  clutter_actor_set_background_color (actor1, &CLUTTER_COLOR_INIT (245, 121, 0, 255));
  clutter_actor_add_child (stage, actor1);

  meta_ref_test_verify_view (get_view (),
                             g_test_get_path (), 1,
                             meta_ref_test_determine_ref_test_flag ());

  actor2 = clutter_actor_new ();
  clutter_actor_set_position (actor2, 20, 20);
  clutter_actor_set_size (actor2, 50, 50);
  clutter_actor_set_background_color (actor2, &CLUTTER_COLOR_INIT (52, 101, 164, 255));
  clutter_actor_add_child (stage, actor2);

  g_test_expect_message ("libmutter-test",
                         G_LOG_LEVEL_CRITICAL,
                         "Pixel difference exceeds limits*");

  meta_ref_test_verify_view (get_view (),
                             g_test_get_path (), 1,
                             meta_ref_test_determine_ref_test_flag ());

  g_test_assert_expected_messages ();

  clutter_actor_destroy (actor2);
  clutter_actor_destroy (actor1);
}

static void
init_ref_test_sanity_tests (void)
{
  g_test_add_func ("/tests/ref-test/sanity",
                   meta_test_ref_test_sanity);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_ref_test_sanity_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (setup_test_environment), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (tear_down_test_environment), NULL);

  test_context = context;

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
