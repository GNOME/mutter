/*
 * Copyright (C) 2021-2022 Red Hat, Inc.
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

#include "tests/meta-wayland-test-utils.h"

#include <gio/gio.h>

#include "wayland/meta-wayland.h"

struct _MetaWaylandTestClient
{
  GSubprocess *subprocess;
  char *path;
  GMainLoop *main_loop;
};

static char *
get_test_client_path (const char *test_client_name)
{
  return g_test_build_filename (G_TEST_BUILT,
                                "src",
                                "tests",
                                "wayland-test-clients",
                                test_client_name,
                                NULL);
}

MetaWaylandTestClient *
meta_wayland_test_client_new (const char *test_client_name)
{
  MetaWaylandCompositor *compositor;
  const char *wayland_display_name;
  g_autofree char *test_client_path = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  GSubprocess *subprocess;
  GError *error = NULL;
  MetaWaylandTestClient *wayland_test_client;

  compositor = meta_wayland_compositor_get_default ();
  wayland_display_name = meta_wayland_get_wayland_display_name (compositor);
  test_client_path = get_test_client_path (test_client_name);

  launcher =  g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher,
                                "WAYLAND_DISPLAY", wayland_display_name,
                                TRUE);

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            test_client_path,
                                            NULL);
  if (!subprocess)
    {
      g_error ("Failed to launch Wayland test client '%s': %s",
               test_client_path, error->message);
    }

  wayland_test_client = g_new0 (MetaWaylandTestClient, 1);
  wayland_test_client->subprocess = subprocess;
  wayland_test_client->path = g_strdup (test_client_name);
  wayland_test_client->main_loop = g_main_loop_new (NULL, FALSE);

  return wayland_test_client;
}

static void
wayland_test_client_finished (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  MetaWaylandTestClient *wayland_test_client = user_data;
  GError *error = NULL;

  if (!g_subprocess_wait_finish (wayland_test_client->subprocess,
                                 res,
                                 &error))
    {
      g_error ("Failed to wait for Wayland test client '%s': %s",
               wayland_test_client->path, error->message);
    }

  g_main_loop_quit (wayland_test_client->main_loop);
}

void
meta_wayland_test_client_finish (MetaWaylandTestClient *wayland_test_client)
{
  g_subprocess_wait_async (wayland_test_client->subprocess, NULL,
                           wayland_test_client_finished, wayland_test_client);

  g_main_loop_run (wayland_test_client->main_loop);

  g_assert_true (g_subprocess_get_successful (wayland_test_client->subprocess));

  g_main_loop_unref (wayland_test_client->main_loop);
  g_free (wayland_test_client->path);
  g_object_unref (wayland_test_client->subprocess);
  g_free (wayland_test_client);
}
