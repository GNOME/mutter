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
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-window-wayland.h"
#include "wayland/meta-wayland-surface-private.h"
#include "compositor/meta-window-actor-private.h"
#include "meta/meta-window-config.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;

#define TEST_CLIENT_TITLE "window-config-test-window"

static void
on_effects_completed (MetaWindowActor *window_actor,
                      gboolean        *done)
{
  g_debug ("Window added");
  *done = TRUE;
}

static void
wait_for_window_added (MetaWindow *window)
{
  MetaWindowActor *window_actor;
  gboolean done = FALSE;
  gulong handler_id;

  g_debug ("Waiting for window to be added");
  window_actor = meta_window_actor_from_window (window);
  handler_id = g_signal_connect (window_actor, "effects-completed",
                                 G_CALLBACK (on_effects_completed), &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (window_actor, handler_id);
}

static void
on_window_created (MetaDisplay *display,
                   MetaWindow *window,
                   gpointer user_data)
{
  g_debug ("Window created");
  g_signal_connect (window, "configure",
                    G_CALLBACK (user_data),
                    NULL);
  g_signal_handlers_disconnect_by_func (display,
                                        on_window_created,
                                        user_data);
}

static void
get_window_surface_size (MetaWindow *window,
                         int        *width,
                         int        *height)
{
  *width = meta_wayland_surface_get_buffer_width (meta_window_get_wayland_surface (window));
  *height = meta_wayland_surface_get_buffer_height (meta_window_get_wayland_surface (window));
}

static void
on_configure_fullscreen (MetaWindow       *window,
                         MetaWindowConfig *window_config,
                         gpointer          user_data)
{
  g_debug ("Configure signal received for fullscreen test");

  if (!meta_window_config_get_is_initial (window_config))
    {
      g_debug ("Not the initial configure, skipping");
      return;
    }

  /* Set the window to be fullscreen in the initial configuration */
  g_debug ("Set fullscreen to TRUE in window config");
  meta_window_config_set_is_fullscreen (window_config, TRUE);

  g_signal_handlers_disconnect_by_func (window,
                                        on_configure_fullscreen,
                                        user_data);
}

static void
test_meta_window_config_fullscreen (MetaWindowClientType client_type)
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
                    on_configure_fullscreen);

  test_client = meta_test_client_new (test_context,
                                      "window-config-test-client",
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
  g_assert_true (meta_window_is_fullscreen (window));

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
test_meta_window_config_fullscreen_wayland (void)
{
  test_meta_window_config_fullscreen (META_WINDOW_CLIENT_TYPE_WAYLAND);
}

static void
test_meta_window_config_fullscreen_x11 (void)
{
#ifdef MUTTER_PRIVILEGED_TEST
  g_test_skip ("Running Xwayland in CI KVM doesn't work currently");
#else
  test_meta_window_config_fullscreen (META_WINDOW_CLIENT_TYPE_X11);
#endif
}

static void
on_configure_position_size (MetaWindow       *window,
                             MetaWindowConfig *window_config,
                             gpointer          user_data)
{
  g_debug ("Configure signal received for position/size test");

  if (!meta_window_config_get_is_initial (window_config))
    {
      g_debug ("Not the initial configure, skipping");
      return;
    }

  /* Set the window to be not fullscreen in the initial configuration */
  meta_window_config_set_is_fullscreen (window_config, FALSE);

  /* Set specific position and size for the window */
  meta_window_config_set_position (window_config, 50, 75);
  meta_window_config_set_size (window_config, 300, 200);

  g_debug ("Set position to (50, 75) and size to (300, 200) in window config");
}

static void
test_meta_window_config_position_and_size (MetaWindowClientType client_type)
{
  MetaDisplay *display = meta_context_get_display (test_context);
  g_autoptr (GError) error = NULL;
  MetaTestClient *test_client;
  MetaWindow *window;
  MtkRectangle rect;
  int surface_width;
  int surface_height;

  g_debug ("Starting MetaWindowConfig position/size test");

  g_signal_connect (display, "window-created",
                    G_CALLBACK (on_window_created),
                    on_configure_position_size);

  test_client = meta_test_client_new (test_context,
                                      "window-config-test-client",
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

  /* Verify the window is not fullscreen */
  g_assert_false (meta_window_is_fullscreen (window));

  /* Verify the window has the correct position and size */
  meta_window_get_buffer_rect (window, &rect);
  g_assert_cmpint (rect.x, ==, 50);
  g_assert_cmpint (rect.y, ==, 75);
  g_assert_cmpint (rect.width, ==, 300);
  g_assert_cmpint (rect.height, ==, 200);

  /* Verify the surface size matches the expected size */
  get_window_surface_size (window, &surface_width, &surface_height);
  g_assert_cmpint (surface_width, ==, 300);
  g_assert_cmpint (surface_height, ==, 200);

  g_debug ("Position/size test passed - window has correct position (%d, %d) and size (%d, %d)",
           rect.x, rect.y, rect.width, rect.height);

  meta_test_client_destroy (test_client);

  /* Wait for the window to be removed */
  while (window)
    g_main_context_iteration (NULL, TRUE);
}

static void
test_meta_window_config_position_and_size_wayland (void)
{
  test_meta_window_config_position_and_size (META_WINDOW_CLIENT_TYPE_WAYLAND);
}

static void
test_meta_window_config_position_and_size_x11 (void)
{
#ifdef MUTTER_PRIVILEGED_TEST
  g_test_skip ("Running Xwayland in CI KVM doesn't work currently");
#else
  test_meta_window_config_position_and_size (META_WINDOW_CLIENT_TYPE_X11);
#endif
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);

  test_driver = meta_wayland_test_driver_new (compositor);
  virtual_monitor = meta_create_test_monitor (test_context,
                                              640, 480, 60.0);
}

static void
on_after_tests (void)
{
  g_clear_object (&test_driver);
  g_clear_object (&virtual_monitor);
}

static void
init_tests (void)
{
  g_test_add_func ("/wm/window/window-config/fullscreen/wayland",
                   test_meta_window_config_fullscreen_wayland);
  g_test_add_func ("/wm/window/window-config/fullscreen/x11",
                   test_meta_window_config_fullscreen_x11);
  g_test_add_func ("/wm/window/window-config/position-and-size/wayland",
                   test_meta_window_config_position_and_size_wayland);
  g_test_add_func ("/wm/window/window-config/position-and-size/x11",
                   test_meta_window_config_position_and_size_x11);
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
