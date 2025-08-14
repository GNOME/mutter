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

#include "meta-test/meta-context-test.h"
#include "tests/meta-monitor-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;

static void
test_wayland_viewport_buffer_less (void)
{
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MtkRectangle rect;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "buffer-less-viewport-client");

  while (!(window = meta_find_window_from_title (test_context,
                                                 "buffer-less-viewport")))
    g_main_context_iteration (NULL, TRUE);

  while (meta_window_is_hidden (window))
    g_main_context_iteration (NULL, TRUE);

  meta_window_get_frame_rect (window, &rect);
  g_assert_cmpint (rect.width, ==, 200);
  g_assert_cmpint (rect.height, ==, 200);
  g_assert_cmpint (rect.x, ==, 400);
  g_assert_cmpint (rect.y, ==, 400);

  meta_wayland_test_driver_terminate (test_driver);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);

  test_driver = meta_wayland_test_driver_new (compositor);
  virtual_monitor = meta_create_test_monitor (test_context,
                                              1000, 1000, 60.0);
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
  g_test_add_func ("/wayland/viewport/buffer-less",
                   test_wayland_viewport_buffer_less);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11 |
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
