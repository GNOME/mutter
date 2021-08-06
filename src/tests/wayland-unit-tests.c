/*
 * Copyright (C) 2019 Red Hat, Inc.
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

#include "tests/wayland-unit-tests.h"

#include <gio/gio.h>

#include "core/display-private.h"
#include "core/window-private.h"
#include "tests/meta-wayland-test-driver.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-surface.h"

typedef struct _WaylandTestClient
{
  GSubprocess *subprocess;
  char *path;
  GMainLoop *main_loop;
} WaylandTestClient;

static MetaWaylandTestDriver *test_driver;

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

static WaylandTestClient *
wayland_test_client_new (const char *test_client_name)
{
  MetaWaylandCompositor *compositor;
  const char *wayland_display_name;
  g_autofree char *test_client_path = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  GSubprocess *subprocess;
  GError *error = NULL;
  WaylandTestClient *wayland_test_client;

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

  wayland_test_client = g_new0 (WaylandTestClient, 1);
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
  WaylandTestClient *wayland_test_client = user_data;
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

static void
wayland_test_client_finish (WaylandTestClient *wayland_test_client)
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

static MetaWindow *
find_client_window (const char *title)
{
  MetaDisplay *display = meta_get_display ();
  g_autoptr (GSList) windows = NULL;
  GSList *l;

  windows = meta_display_list_windows (display, META_LIST_DEFAULT);
  for (l = windows; l; l = l->next)
    {
      MetaWindow *window = l->data;

      if (g_strcmp0 (meta_window_get_title (window), title) == 0)
        return window;
    }

  return NULL;
}

static void
subsurface_remap_toplevel (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("subsurface-remap-toplevel");
  wayland_test_client_finish (wayland_test_client);
}

static void
subsurface_reparenting (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("subsurface-reparenting");
  wayland_test_client_finish (wayland_test_client);
}

static void
subsurface_invalid_subsurfaces (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("invalid-subsurfaces");
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "WL: error in client communication*");
  wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

static void
subsurface_invalid_xdg_shell_actions (void)
{
  WaylandTestClient *wayland_test_client;

  wayland_test_client = wayland_test_client_new ("invalid-xdg-shell-actions");
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "Invalid geometry * set on xdg_surface*");
  wayland_test_client_finish (wayland_test_client);
  g_test_assert_expected_messages ();
}

typedef enum _ApplyLimitState
{
  APPLY_LIMIT_STATE_INIT,
  APPLY_LIMIT_STATE_RESET,
  APPLY_LIMIT_STATE_FINISH,
} ApplyLimitState;

typedef struct _ApplyLimitData
{
  GMainLoop *loop;
  WaylandTestClient *wayland_test_client;
  ApplyLimitState state;
} ApplyLimitData;

static void
on_sync_point (MetaWaylandTestDriver *test_driver,
               unsigned int           sequence,
               struct wl_client      *wl_client,
               ApplyLimitData        *data)
{
  MetaWindow *window;

  if (sequence == 0)
    g_assert (data->state == APPLY_LIMIT_STATE_INIT);
  else if (sequence == 0)
    g_assert (data->state == APPLY_LIMIT_STATE_RESET);

  window = find_client_window ("toplevel-limits-test");

  if (sequence == 0)
    {
      g_assert_nonnull (window);
      g_assert_cmpint (window->size_hints.max_width, ==, 700);
      g_assert_cmpint (window->size_hints.max_height, ==, 500);
      g_assert_cmpint (window->size_hints.min_width, ==, 700);
      g_assert_cmpint (window->size_hints.min_height, ==, 500);

      data->state = APPLY_LIMIT_STATE_RESET;
    }
  else if (sequence == 1)
    {
      g_assert_null (window);
      data->state = APPLY_LIMIT_STATE_FINISH;
      g_main_loop_quit (data->loop);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
toplevel_apply_limits (void)
{
  ApplyLimitData data = {};

  data.loop = g_main_loop_new (NULL, FALSE);
  data.wayland_test_client = wayland_test_client_new ("xdg-apply-limits");
  g_signal_connect (test_driver, "sync-point", G_CALLBACK (on_sync_point), &data);
  g_main_loop_run (data.loop);
  g_assert_cmpint (data.state, ==, APPLY_LIMIT_STATE_FINISH);
  wayland_test_client_finish (data.wayland_test_client);
  g_test_assert_expected_messages ();
}

void
pre_run_wayland_tests (void)
{
  MetaWaylandCompositor *compositor;

  compositor = meta_wayland_compositor_get_default ();
  g_assert_nonnull (compositor);

  test_driver = meta_wayland_test_driver_new (compositor);
}

void
init_wayland_tests (void)
{
  g_test_add_func ("/wayland/subsurface/remap-toplevel",
                   subsurface_remap_toplevel);
  g_test_add_func ("/wayland/subsurface/reparent",
                   subsurface_reparenting);
  g_test_add_func ("/wayland/subsurface/invalid-subsurfaces",
                   subsurface_invalid_subsurfaces);
  g_test_add_func ("/wayland/subsurface/invalid-xdg-shell-actions",
                   subsurface_invalid_xdg_shell_actions);
  g_test_add_func ("/wayland/toplevel/apply-limits",
                   toplevel_apply_limits);
}
