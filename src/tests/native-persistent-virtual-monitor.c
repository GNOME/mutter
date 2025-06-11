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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include "backends/meta-logical-monitor-private.h"
#include "backends/meta-monitor-manager-private.h"
#include "meta/meta-context.h"
#include "meta/meta-backend.h"
#include "tests/meta-test-shell.h"
#include "tests/meta-test-utils.h"

static MetaContext *test_context;

static gboolean
wait_for_paint (gpointer user_data)
{
  MetaContext *context = user_data;
  MetaBackend *backend = meta_context_get_backend (test_context);
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GMainLoop *loop;
  GList *monitors;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor;
  MtkRectangle layout;

  loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect_swapped (stage, "presented",
                            G_CALLBACK (g_main_loop_quit),
                            loop);
  clutter_actor_queue_redraw (stage);

  monitors = meta_monitor_manager_get_monitors (monitor_manager);
  g_assert_cmpint (g_list_length (monitors), ==, 1);

  logical_monitors =
    meta_monitor_manager_get_logical_monitors (monitor_manager);
  g_assert_cmpint (g_list_length (logical_monitors), ==, 1);

  logical_monitor = logical_monitors->data;
  g_assert_true (meta_logical_monitor_get_monitors (logical_monitor)->data ==
            monitors->data);

  layout = meta_logical_monitor_get_layout (logical_monitor);
  g_assert_cmpint (layout.x, ==, 0);
  g_assert_cmpint (layout.y, ==, 0);
  g_assert_cmpint (layout.width, ==, 800);
  g_assert_cmpint (layout.height, ==, 600);

  g_main_loop_run (loop);

  meta_context_terminate (context);

  return G_SOURCE_REMOVE;
}

int
main (int    argc,
      char **argv)
{
  char *fake_args[] = {
      argv[0],
      (char *) "--wayland",
      (char *) "--headless",
      (char *) "--virtual-monitor",
      (char *) "800x600",
  };
  char **fake_argv = fake_args;
  int fake_argc = G_N_ELEMENTS (fake_args);
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_context ("Persistent virtual monitor test");
  g_assert_true (meta_context_configure (context, &fake_argc, &fake_argv, &error));
  meta_context_set_plugin_gtype (context, META_TYPE_TEST_SHELL);
  g_assert_true (meta_context_setup (context, &error));
  g_assert_true (meta_context_start (context, &error));

  g_idle_add (wait_for_paint, context);

  test_context = context;

  g_assert_true (meta_context_run_main_loop (context, &error));

  return EXIT_SUCCESS;
}
