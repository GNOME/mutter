/*
 * Copyright (C) 2016-2025 Red Hat, Inc.
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

#include "core/window-private.h"
#include "meta/compositor.h"
#include "tests/monitor-tests-common.h"

static gboolean
quit_main_loop (gpointer data)
{
  GMainLoop *loop = data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
dispatch (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaCompositor *compositor = meta_display_get_compositor (display);
  MetaLaters *laters = meta_compositor_get_laters (compositor);
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  meta_laters_add (laters, META_LATER_BEFORE_REDRAW,
                   quit_main_loop,
                   loop,
                   NULL);
  g_main_loop_run (loop);
}

static MetaTestClient *
create_test_window (MetaContext *context,
                    const char  *window_name)
{
  MetaTestClient *test_client;
  static int client_count = 0;
  g_autofree char *client_name = NULL;
  g_autoptr (GError) error = NULL;

  client_name = g_strdup_printf ("test_client_%d", client_count++);
  test_client = meta_test_client_new (context,
                                      client_name, META_WINDOW_CLIENT_TYPE_WAYLAND,
                                      &error);
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  if (!meta_test_client_do (test_client, &error,
                            "create", window_name,
                            NULL))
    g_error ("Failed to create window: %s", error->message);

  return test_client;
}

static void
meta_test_monitor_wm_tiling (void)
{
  MetaContext *context = test_context;
  MetaBackend *backend = meta_context_get_backend (test_context);
  MonitorTestCase test_case = initial_test_case;
  MetaMonitorTestSetup *test_setup;
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;

  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  /*
   * 1) Start with two monitors connected.
   * 2) Tile it on the second monitor.
   * 3) Unplug both monitors.
   * 4) Replug in first monitor.
   */

  const char *test_window_name= "window1";
  test_client = create_test_window (context, test_window_name);

  if (!meta_test_client_do (test_client, &error,
                            "show", test_window_name,
                            NULL))
    g_error ("Failed to show the window: %s", error->message);

  MetaWindow *test_window =
    meta_test_client_find_window (test_client,
                                  test_window_name,
                                  &error);
  if (!test_window)
    g_error ("Failed to find the window: %s", error->message);
  meta_wait_for_window_shown (test_window);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);
  meta_window_move_to_monitor (test_window, 1);
  meta_check_test_client_state (test_client);

  test_case.setup.n_outputs = 0;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);
  test_case.setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  dispatch ();

  /*
   * 1) Start with two monitors connected.
   * 2) Tile a window on the second monitor.
   * 3) Untile window.
   * 4) Unplug monitor.
   * 5) Tile window again.
   */

  test_case.setup.n_outputs = 2;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  meta_window_move_to_monitor (test_window, 1);
  meta_window_tile (test_window, META_TILE_NONE);

  test_case.setup.n_outputs = 1;
  test_setup = meta_create_monitor_test_setup (backend,
                                               &test_case.setup,
                                               MONITOR_TEST_FLAG_NO_STORED);
  meta_emulate_hotplug (test_setup);

  meta_window_tile (test_window, META_TILE_MAXIMIZED);

  meta_test_client_destroy (test_client);
}

static void
init_tiling_hotplug_tests (void)
{
  meta_add_monitor_test ("/backends/monitor/wm/tiling",
                         meta_test_monitor_wm_tiling);
}

int
main (int   argc,
      char *argv[])
{
  return meta_monitor_test_main (argc, argv, init_tiling_hotplug_tests);
}
