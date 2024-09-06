/*
 * Copyright (C) 2022 Red Hat Inc.
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

#include "meta/meta-selection.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-xwayland.h"
#include "x11/meta-x11-display-private.h"

static MetaContext *test_context;

static void
test_client_do_check (MetaTestClient *test_client,
                      ...)
{
  g_autoptr (GError) error = NULL;
  va_list vap;
  gboolean retval;

  va_start (vap, test_client);
  retval = meta_test_client_dov (test_client, &error, vap);
  va_end (vap);

  if (!retval)
    g_error ("Failed to process test client command: %s", error->message);
}

static void
test_client_wait_check (MetaTestClient *test_client)
{
  g_autoptr (GError) error = NULL;

  if (!meta_test_client_wait (test_client, &error))
    g_error ("Failed to wait for test client: %s", error->message);
}

static void
transfer_ready_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  GMainLoop *loop = user_data;
  g_autoptr (GError) error = NULL;

  if (!meta_selection_transfer_finish (META_SELECTION (source_object), res,
                                       &error))
    g_warning ("Failed to transfer: %s", error->message);

  g_main_loop_quit (loop);
}

static void
ensure_xwayland (MetaContext *context)
{
  MetaDisplay *display = meta_context_get_display (test_context);

  if (meta_display_get_x11_display (display))
    return;

  while (!meta_display_get_x11_display (display))
    g_main_context_iteration (NULL, TRUE);
}

static void
meta_test_xwayland_restart_selection (void)
{
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaXWaylandManager *xwayland_manager =
    meta_wayland_compositor_get_xwayland_manager (wayland_compositor);
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaSelection *selection = meta_display_get_selection (display);
  MetaX11Display *x11_display;
  MetaTestClient *test_client;
  static int client_count = 0;
  g_autofree char *client_name = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (GOutputStream) output = NULL;
  const char *window_name = "clipboard-window";

  client_name = g_strdup_printf ("test_client_%d", client_count++);
  test_client = meta_test_client_new (test_context,
                                      client_name, META_WINDOW_CLIENT_TYPE_X11,
                                      &error);
  if (!test_client)
    g_error ("Failed to launch test client: %s", error->message);

  ensure_xwayland (test_context);
  x11_display = meta_display_get_x11_display (display);

  g_assert_null (x11_display->selection.owners[META_SELECTION_CLIPBOARD]);

  test_client_do_check (test_client,
                        "create", window_name,
                        NULL);
  test_client_do_check (test_client,
                        "clipboard-set", "application/mutter-test", "hello",
                        NULL);
  test_client_wait_check (test_client);

  while (!x11_display->selection.owners[META_SELECTION_CLIPBOARD])
    g_main_context_iteration (NULL, TRUE);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "*Connection to xwayland lost*");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "X Wayland crashed*; attempting to recover");

  if (!meta_xwayland_signal (xwayland_manager, SIGKILL, &error))
    g_error ("Failed to signal SIGSEGV to Xwayland");

  while (meta_display_get_x11_display (display))
    g_main_context_iteration (NULL, TRUE);

  g_test_assert_expected_messages ();

  loop = g_main_loop_new (NULL, FALSE);
  output = g_memory_output_stream_new (NULL, 0, g_realloc, g_free);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "*Tried to transfer from NULL selection source");
  meta_selection_transfer_async (selection,
                                 META_SELECTION_CLIPBOARD,
                                 "text/plain",
                                 -1,
                                 output,
                                 NULL,
                                 transfer_ready_cb,
                                 loop);

  g_main_loop_run (loop);

  g_test_assert_expected_messages ();

  meta_test_client_destroy (test_client);
}

static void
meta_test_xwayland_crash_only_x11 (void)
{
  MetaWaylandCompositor *wayland_compositor =
    meta_context_get_wayland_compositor (test_context);
  MetaXWaylandManager *xwayland_manager =
    meta_wayland_compositor_get_xwayland_manager (wayland_compositor);
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaTestClient *test_client1;
  MetaTestClient *test_client2;
  g_autoptr (GError) error = NULL;

  g_assert_null (meta_display_list_all_windows (display));

  test_client1 = meta_test_client_new (test_context,
                                       "client1", META_WINDOW_CLIENT_TYPE_X11,
                                       &error);
  if (!test_client1)
    g_error ("Failed to launch test client: %s", error->message);

  test_client2 = meta_test_client_new (test_context,
                                       "client1", META_WINDOW_CLIENT_TYPE_X11,
                                       &error);
  if (!test_client2)
    g_error ("Failed to launch test client: %s", error->message);

  ensure_xwayland (test_context);

  test_client_do_check (test_client2, "create", "test-window", NULL);
  test_client_do_check (test_client1, "create", "test-window", NULL);
  test_client_do_check (test_client2, "show", "test-window", NULL);
  test_client_do_check (test_client1, "show", "test-window", NULL);
  test_client_wait_check (test_client2);
  test_client_wait_check (test_client1);

  while (!meta_find_window_from_title (test_context, "test/client1/test-window") ||
         !meta_find_window_from_title (test_context, "test/client1/test-window"))
    g_main_context_iteration (NULL, TRUE);

  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "*Connection to xwayland lost*");
  g_test_expect_message ("libmutter", G_LOG_LEVEL_WARNING,
                         "X Wayland crashed*; attempting to recover");

  if (!meta_xwayland_signal (xwayland_manager, SIGKILL, &error))
    g_error ("Failed to signal SIGSEGV to Xwayland");

  while (meta_display_get_x11_display (display))
    g_main_context_iteration (NULL, TRUE);

  g_test_assert_expected_messages ();

  g_assert_null (meta_display_list_all_windows (display));

  meta_test_client_destroy (test_client1);
  meta_test_client_destroy (test_client2);
}

static void
meta_test_hammer_activate (void)
{
  MetaTestClient *x11_client;
  MetaTestClient *wayland_client;
  g_autoptr (GError) error = NULL;
  int i;

  x11_client = meta_test_client_new (test_context, "x11-client",
                                     META_WINDOW_CLIENT_TYPE_X11,
                                     &error);
  g_assert_nonnull (x11_client);
  wayland_client = meta_test_client_new (test_context, "wayland-client",
                                         META_WINDOW_CLIENT_TYPE_WAYLAND,
                                         &error);
  g_assert_nonnull (wayland_client);

  meta_test_client_run (x11_client,
                        "create 1\n"
                        "show 1\n");

  meta_test_client_run (wayland_client,
                        "create 2\n"
                        "show 2\n");

  meta_test_client_run (x11_client, "activate 1");
  for (i = 0; i < 10000; i++)
    meta_test_client_run (wayland_client, "activate 2");

  meta_test_client_destroy (x11_client);
  meta_test_client_destroy (wayland_client);
}

static void
compositor_check_proc_async (GObject      *source_object,
                             GAsyncResult *res,
                             gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  GMainLoop *loop = user_data;

  g_subprocess_wait_check_finish (G_SUBPROCESS (source_object), res, &error);
  g_assert_no_error (error);
  g_main_loop_quit (loop);
}

static void
meta_test_xwayland_compositor_selection (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaWaylandCompositor *compositor;
  const char *x11_display_name;
  const char *x11_compositor_checker;

  g_assert_null (meta_display_get_x11_display (display));

  g_assert_true (meta_is_wayland_compositor ());
  compositor = meta_context_get_wayland_compositor (test_context);
  x11_display_name = meta_wayland_get_public_xwayland_display_name (compositor);
  g_assert_nonnull (x11_display_name);

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher,
                                "DISPLAY", x11_display_name,
                                TRUE);

  x11_compositor_checker = g_test_build_filename (G_TEST_BUILT,
                                                  "x11-compositor-checker",
                                                  NULL);

  subprocess = g_subprocess_launcher_spawn (launcher,
                                            &error,
                                            x11_compositor_checker,
                                            NULL);
  g_assert_no_error (error);

  loop = g_main_loop_new (NULL, FALSE);
  g_subprocess_wait_check_async (subprocess, NULL,
                                 compositor_check_proc_async, loop);
  g_main_loop_run (loop);

  g_assert_nonnull (meta_display_get_x11_display (display));
}

static void
init_tests (void)
{
  g_test_add_func ("/backends/xwayland/compositor/selection",
                   meta_test_xwayland_compositor_selection);
  g_test_add_func ("/backends/xwayland/restart/selection",
                   meta_test_xwayland_restart_selection);
  g_test_add_func ("/backends/xwayland/crash/only-x11",
                   meta_test_xwayland_crash_only_x11);
  g_test_add_func ("/backends/xwayland/crash/hammer-activate",
                   meta_test_hammer_activate);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                              META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
