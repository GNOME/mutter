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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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

static gboolean
tests_alarm_filter (MetaX11Display        *x11_display,
                    XSyncAlarmNotifyEvent *event,
                    gpointer               user_data)
{
  MetaTestClient *test_client = user_data;
  return meta_test_client_process_x11_event (test_client,
                                             x11_display, event);
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
  meta_x11_display_set_alarm_filter (x11_display,
                                     tests_alarm_filter,
                                     test_client);

  test_client_do_check (test_client,
                        "create", window_name,
                        NULL);
  test_client_do_check (test_client,
                        "clipboard-set", "application/mutter-test", "hello",
                        NULL);
  test_client_wait_check (test_client);

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
init_tests (void)
{
  g_test_add_func ("/backends/xwayland/restart/selection",
                   meta_test_xwayland_restart_selection);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = test_context =
    meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                              META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  init_tests ();

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
