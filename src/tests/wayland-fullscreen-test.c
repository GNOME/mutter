/*
 * Copyright (C) 2022 Red Hat, Inc.
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
#include "compositor/meta-window-actor-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;

static void
on_first_frame (MetaWindowActor *window_actor,
                gboolean        *done)
{
  *done = TRUE;
}

static void
wait_for_first_frame (MetaWindow *window)
{
  MetaWindowActor *window_actor;
  gboolean done = FALSE;
  glong handler_id;

  window_actor = meta_window_actor_from_window (window);
  handler_id = g_signal_connect (window_actor, "first-frame",
                                 G_CALLBACK (on_first_frame), &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  g_signal_handler_disconnect (window_actor, handler_id);
}

static void
toplevel_fullscreen (void)
{
  g_autoptr (MetaVirtualMonitor) virtual_monitor = NULL;
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window = NULL;
  MetaRectangle rect;

  virtual_monitor = meta_create_test_monitor (test_context, 100, 100, 10.0);

  wayland_test_client = meta_wayland_test_client_new ("fullscreen");

  while (!(window = meta_find_window_from_title (test_context, "fullscreen")))
    g_main_context_iteration (NULL, TRUE);

  wait_for_first_frame (window);

  meta_window_get_frame_rect (window, &rect);
  g_assert_cmpint (rect.width, ==, 100);
  g_assert_cmpint (rect.height, ==, 100);
  g_assert_cmpint (rect.x, ==, 0);
  g_assert_cmpint (rect.y, ==, 0);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  meta_wayland_test_client_finish (wayland_test_client);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);

  test_driver = meta_wayland_test_driver_new (compositor);
}

static void
on_after_tests (void)
{
  g_clear_object (&test_driver);
}

static void
init_tests (void)
{
  g_test_add_func ("/wayland/toplevel/fullscreen",
                   toplevel_fullscreen);
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_NONE);
}
