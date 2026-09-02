/*
 * Copyright (C) 2026 Red Hat Inc.
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

#include <X11/Xatom.h>

#include "backends/meta-virtual-monitor.h"
#include "compositor/meta-surface-actor.h"
#include "compositor/meta-window-actor-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-ref-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-x11-test-utils.h"
#include "x11/meta-x11-display-private.h"
#include "x11/window-x11.h"

static MetaContext *test_context;
static MetaVirtualMonitor *virtual_monitor;

static MetaWindow *
wait_for_property (const char *title,
                   Atom        atom)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaX11Display *x11_display = meta_display_get_x11_display (display);
  Display *xdisplay = meta_x11_display_get_xdisplay (x11_display);

  while (TRUE)
    {
      MetaWindow *window;
      Window xwindow;
      int status;
      Atom actual_type;
      int actual_format;
      unsigned long nitems, bytes_after;
      unsigned char *prop;

      window = meta_find_window_from_title (test_context, title);
      if (!window)
        goto retry;

      xwindow = meta_window_x11_get_xwindow (window);

      status = XGetWindowProperty (xdisplay, xwindow, atom, 0, 1, False,
                                   XA_CARDINAL, &actual_type, &actual_format,
                                   &nitems, &bytes_after, &prop);

      g_clear_pointer (&prop, XFree);
      if (status == Success && actual_type == XA_CARDINAL)
        return window;

retry:
      g_main_context_iteration (NULL, TRUE);
    }
}

static void
wait_for_surface_actor (MetaWindow *window)
{
  MetaWindowActor *window_actor;

  window_actor = meta_window_actor_from_window (window);
  g_assert_nonnull (window_actor);
  while (TRUE)
    {
      MetaSurfaceActor *surface_actor;

      surface_actor = meta_window_actor_get_surface (window_actor);
      if (surface_actor)
        break;

      g_main_context_iteration (NULL, TRUE);
    }
}

static void
meta_test_x11_allow_commits_race (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaBackend *backend = meta_context_get_backend (test_context);
  MetaRenderer *renderer = meta_backend_get_renderer (backend);
  GList *views;
  MetaX11TestClient *test_client;
  MetaX11Display *x11_display = NULL;
  Display *xdisplay;
  Atom test_done_atom;
  MetaWindow *window;
  MtkRectangle rect;

  test_client = meta_x11_test_client_new_with_args (test_context,
                                                    "xwayland-allow-commits-test",
                                                    NULL);

  while (TRUE)
    {
      x11_display = meta_display_get_x11_display (display);
      if (x11_display)
        break;

      g_main_context_iteration (NULL, TRUE);
    }

  xdisplay = meta_x11_display_get_xdisplay (x11_display);
  test_done_atom = XInternAtom (xdisplay, "_TEST_DONE", False);

  window = wait_for_property ("xwayland-allow-commits-test", test_done_atom);
  meta_wait_for_window_shown (window);
  meta_wait_for_effects (window);
  wait_for_surface_actor (window);
  meta_wait_for_window_shown (window);
  meta_wait_for_effects (window);

  meta_window_get_frame_rect (window, &rect);

  views = meta_renderer_get_views (renderer);
  g_assert_cmpint (g_list_length (views), ==, 1);

  meta_ref_test_verify_view (CLUTTER_STAGE_VIEW (views->data),
                             g_test_get_path (), 0,
                             meta_ref_test_determine_ref_test_flag ());

  meta_x11_test_client_send_sigterm (test_client);
  meta_x11_test_client_finish (test_client);
}

static void
meta_test_x11_user_position (void)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  MetaX11TestClient *test_client;
  MetaX11Display *x11_display = NULL;
  MetaWindow *window;
  MtkRectangle frame_rect;
  MtkRectangle client_rect;

  test_client = meta_x11_test_client_new_with_args (test_context,
                                                    "user-position",
                                                    NULL);

  while (TRUE)
    {
      x11_display = meta_display_get_x11_display (display);
      if (x11_display)
        break;

      g_main_context_iteration (NULL, TRUE);
    }

  window = meta_wait_for_client_window (test_context, "user-position");
  meta_wait_for_window_shown (window);
  meta_wait_for_effects (window);

  meta_window_get_frame_rect (window, &frame_rect);
  meta_window_get_client_content_rect (window, &client_rect);

  /* The client window should have the expected size, but its the frame that
   * currently gets the user position, not the client window. */
  g_assert_cmpint (client_rect.width, ==, 200);
  g_assert_cmpint (client_rect.height, ==, 300);
  g_assert_cmpint (frame_rect.x, ==, 50);
  g_assert_cmpint (frame_rect.y, ==, 125);

  meta_x11_test_client_send_sigterm (test_client);
  meta_x11_test_client_finish (test_client);
}

static void
on_before_tests (void)
{
  virtual_monitor = meta_create_test_monitor (test_context,
                                              1024, 768, 60.0);
}

static void
on_after_tests (void)
{
  g_clear_object (&virtual_monitor);
}

static void
init_tests (void)
{
  g_test_add_func ("/x11/client/allow-commits-race",
                   meta_test_x11_allow_commits_race);
  g_test_add_func ("/x11/client/user-position",
                   meta_test_x11_user_position);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NONE);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));
  meta_context_test_set_background_color (META_CONTEXT_TEST (context),
                                          COGL_COLOR_INIT (255, 255, 255, 255));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
