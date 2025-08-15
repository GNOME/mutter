/*
 * Copyright (C) 2025 Red Hat Inc.
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

#include "backends/meta-virtual-monitor.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "wayland/meta-wayland-surface-private.h"
#include "compositor/meta-window-actor-private.h"

static MetaContext *test_context;
static MetaVirtualMonitor *virtual_monitor;

#define TEST_CLIENT_TITLE "window-config-test-window"

static void
on_effects_completed (MetaWindowActor *window_actor,
                      gboolean        *done)
{
  g_debug ("Window effects settled");
  *done = TRUE;
}

static void
wait_for_window_added (MetaWindow *window)
{
  MetaWindowActor *window_actor;
  gboolean done = FALSE;
  gulong handler_id;

  g_debug ("Waiting for window animations to settle");
  window_actor = meta_window_actor_from_window (window);
  handler_id = g_signal_connect (window_actor, "effects-completed",
                                 G_CALLBACK (on_effects_completed), &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (window_actor, handler_id);
}

static void
on_window_created (MetaDisplay *display,
                   MetaWindow  *window,
                   gpointer     user_data)
{
  g_debug ("Window created");
  meta_window_maximize (window);
  g_signal_handlers_disconnect_by_func (display,
                                        on_window_created,
                                        NULL);
}

static void
get_window_surface_size (MetaWindow *window,
                         int        *width,
                         int        *height)
{
  MetaWaylandSurface *surface = meta_window_get_wayland_surface (window);

  *width = meta_wayland_surface_get_width (surface);
  *height = meta_wayland_surface_get_height (surface);
}

static void
test_display_window_created (MetaWindowClientType client_type)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;
  MetaWindow *window;
  MtkRectangle rect;
  int surface_width;
  int surface_height;

  g_debug ("Starting MetaWindowConfig fullscreen test");

  display = meta_context_get_display (test_context);
  g_signal_connect (display, "window-created",
                    G_CALLBACK (on_window_created),
                    NULL);

  test_client = meta_test_client_new (test_context,
                                      "window-created-test-client",
                                      client_type,
                                      &error);
  g_assert_no_error (error);

  meta_test_client_run (test_client,
                        "create " TEST_CLIENT_TITLE " csd\n"
                        "show " TEST_CLIENT_TITLE "\n");

  /* Wait for the window to be created */
  while (!(window = meta_test_client_find_window (test_client,
                                                  TEST_CLIENT_TITLE,
                                                  NULL)))
    g_main_context_iteration (NULL, TRUE);
  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *) &window);

  wait_for_window_added (window);

  /* Verify the window is fullscreen */
  g_assert_true (meta_window_is_maximized (window));

  /* Verify the frame rect covers the entire monitor */
  meta_window_get_frame_rect (window, &rect);
  g_assert_cmpint (rect.x, ==, 0);
  g_assert_cmpint (rect.y, ==, 0);
  g_assert_cmpint (rect.width, ==, 640);
  g_assert_cmpint (rect.height, ==, 480);

  /* Verify the surface size matches the entire monitor */
  get_window_surface_size (window, &surface_width, &surface_height);
  g_assert_cmpint (surface_width, ==, 640);
  g_assert_cmpint (surface_height, ==, 480);

  g_debug ("Fullscreen test passed - window is fullscreen with correct dimensions");

  meta_test_client_destroy (test_client);

  /* Wait for the window to be removed */
  while (window)
    g_main_context_iteration (NULL, TRUE);
}

static void
test_display_window_created_wayland (void)
{
  test_display_window_created (META_WINDOW_CLIENT_TYPE_WAYLAND);
}

static void
test_display_window_created_x11 (void)
{
#ifdef MUTTER_PRIVILEGED_TEST
  g_test_skip ("Running Xwayland in CI KVM doesn't work currently");
#else
  test_display_window_created (META_WINDOW_CLIENT_TYPE_WAYLAND);
#endif
}

static void
on_before_tests (void)
{
  virtual_monitor = meta_create_test_monitor (test_context,
                                              640, 480, 60.0);
}

static void
on_after_tests (void)
{
  g_clear_object (&virtual_monitor);
}

static void
init_tests (void)
{
  g_test_add_func ("/wm/display/window-created/wayland",
                   test_display_window_created_wayland);
  g_test_add_func ("/wm/window/window-created/x11",
                   test_display_window_created_x11);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
#ifdef MUTTER_PRIVILEGED_TEST
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
#endif
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
