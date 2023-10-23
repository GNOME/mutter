/*
 * Copyright (C) 2023 Red Hat Inc.
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

#include "core/meta-service-channel.h"
#include "core/window-private.h"
#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "wayland/meta-wayland.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-wayland-surface-private.h"

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;

static void
meta_test_service_channel_wayland (void)
{
  MetaServiceChannel *service_channel = meta_context_get_service_channel (test_context);
  MetaWaylandTestClient *wayland_test_client;
  MetaWindow *window;
  MetaWaylandSurface *surface;
  struct wl_resource *surface_resource;
  struct wl_client *wl_client;
  MetaWaylandClient *wayland_client;

  wayland_test_client =
    meta_wayland_test_client_new (test_context, "service-client");

  window = meta_wait_for_client_window (test_context, "test service client");
  surface = meta_window_get_wayland_surface (window);
  g_assert_nonnull (surface);

  surface_resource = meta_wayland_surface_get_resource (surface);
  wl_client = wl_resource_get_client (surface_resource);

  wayland_client =
    meta_service_channel_get_service_client (service_channel,
                                             META_SERVICE_CLIENT_TYPE_PORTAL_BACKEND);
  g_assert_nonnull (wayland_client);
  g_assert_true (meta_wayland_client_matches (wayland_client, wl_client));

  meta_wayland_test_driver_emit_sync_event (test_driver, 1);
  meta_wayland_test_client_finish (wayland_test_client);
}

static void
on_before_tests (void)
{
  MetaWaylandCompositor *compositor =
    meta_context_get_wayland_compositor (test_context);

  test_driver = meta_wayland_test_driver_new (compositor);
  virtual_monitor = meta_create_test_monitor (test_context,
                                              400, 400, 60.0);
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
  g_test_add_func ("/service-channel/wayland",
                   meta_test_service_channel_wayland);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

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
                                      META_TEST_RUN_FLAG_CAN_SKIP);

  return EXIT_SUCCESS;
}
