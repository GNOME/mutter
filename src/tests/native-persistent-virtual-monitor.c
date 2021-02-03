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

#include "backends/meta-logical-monitor.h"
#include "backends/meta-monitor-manager-private.h"
#include "compositor/meta-plugin-manager.h"
#include "meta/main.h"
#include "meta/meta-backend.h"
#include "tests/test-utils.h"

static gboolean
wait_for_paint (gpointer data)
{
  MetaBackend *backend = meta_get_backend ();
  ClutterActor *stage = meta_backend_get_stage (backend);
  MetaMonitorManager *monitor_manager =
    meta_backend_get_monitor_manager (backend);
  GMainLoop *loop;
  GList *monitors;
  GList *logical_monitors;
  MetaLogicalMonitor *logical_monitor;
  MetaRectangle layout;

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
  g_assert (meta_logical_monitor_get_monitors (logical_monitor)->data ==
            monitors->data);

  layout = meta_logical_monitor_get_layout (logical_monitor);
  g_assert_cmpint (layout.x, ==, 0);
  g_assert_cmpint (layout.y, ==, 0);
  g_assert_cmpint (layout.width, ==, 800);
  g_assert_cmpint (layout.height, ==, 600);

  g_main_loop_run (loop);

  meta_quit (META_EXIT_SUCCESS);

  return G_SOURCE_REMOVE;
}

int
main (int    argc,
      char **argv)
{
  char *fake_args[] = {
      NULL,
      (char *) "--wayland",
      (char *) "--headless",
      (char *) "--virtual-monitor",
      (char *) "800x600",
  };
  char **fake_argv = fake_args;
  int fake_argc = G_N_ELEMENTS (fake_args);
  GOptionContext *ctx;
  GError *error = NULL;

  test_init (&argc, &argv);

  fake_args[0] = argv[0];

  ctx = meta_get_option_context ();
  if (!g_option_context_parse (ctx, &fake_argc, &fake_argv, &error))
    g_error ("Failed to parse fake command line arguments: %s", error->message);
  g_option_context_free (ctx);

  meta_plugin_manager_load (test_get_plugin_name ());

  meta_init ();
  meta_register_with_session ();

  g_idle_add (wait_for_paint, NULL);

  return meta_run ();
}
