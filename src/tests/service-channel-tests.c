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
#include "tests/wayland-test-clients/wayland-test-client-utils.h"

#include <gio/gunixfdlist.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <wayland-client.h>

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

typedef struct
{
  const char *test_tag;
  gboolean client_terminated;
  GDBusConnection *connection;
} ServiceClientTestdata;

static gpointer
service_client_thread_func (gpointer user_data)
{
  ServiceClientTestdata *testdata = user_data;
  g_autoptr (GMainContext) thread_main_context = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusProxy) service_channel_proxy = NULL;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;
  g_auto(GVariantBuilder) options_builder =
    G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  g_autofd int fd = -1;
  struct wl_display *wayland_display;

  thread_main_context = g_main_context_new ();
  g_main_context_push_thread_default (thread_main_context);

  service_channel_proxy =
    g_dbus_proxy_new_sync (testdata->connection,
                           G_DBUS_PROXY_FLAGS_NONE,
                           NULL,
                           "org.gnome.Mutter.ServiceChannel",
                           "/org/gnome/Mutter/ServiceChannel",
                           "org.gnome.Mutter.ServiceChannel",
                           NULL,
                           &error);
  g_assert_no_error (error);
  g_assert_nonnull (service_channel_proxy);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&options_builder, "{sv}",
                         "window-tag", g_variant_new_string (testdata->test_tag));

  result =
    g_dbus_proxy_call_with_unix_fd_list_sync (service_channel_proxy,
                                              "OpenWaylandConnection",
                                              g_variant_new ("(a{sv})",
                                                             &options_builder),
                                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                              -1,
                                              NULL,
                                              &fd_list,
                                              NULL,
                                              &error);
  g_assert_no_error (error);
  g_assert_nonnull (result);
  g_assert_nonnull (fd_list);

  /* Extract the file descriptor */
  g_variant_get (result, "(@h)", &fd_variant);
  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_variant), &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, >=, 0);

  /* Test that we can connect to the Wayland display */
  wayland_display = wl_display_connect_to_fd (fd);
  g_assert_nonnull (wayland_display);

  display = wayland_display_new_full (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER,
                                      wayland_display);
  g_assert_nonnull (display);

  surface = wayland_surface_new (display, "test-tagged-window",
                  100, 100, 0xffabcdff);
  g_assert_nonnull (surface);

  wl_surface_commit (surface->wl_surface);
  wait_for_sync_event (display, 0);
  g_object_unref (display);

  g_atomic_int_set (&testdata->client_terminated, TRUE);

  return NULL;
}

static void
meta_test_service_channel_open_wayland_connection (void)
{
  ServiceClientTestdata testdata = {};
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GThread) thread = NULL;
  MetaWindow *window;
  const char *applied_tag;

  /* Connect to the session bus to call the service channel */
  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (connection);

  testdata = (ServiceClientTestdata) {
    .test_tag = "test-window-tag",
    .client_terminated = FALSE,
    .connection = connection,
  };

  thread = g_thread_new ("service-client-thread",
                         service_client_thread_func,
                         &testdata);

  /* Wait for the window to be created by mutter */
  window = meta_wait_for_client_window (test_context, "test-tagged-window");
  g_assert_nonnull (window);

  /* Check that the window tag was correctly applied */
  applied_tag = meta_window_get_tag (window);
  g_assert_nonnull (applied_tag);
  g_assert_cmpstr (applied_tag, ==, testdata.test_tag);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  g_debug ("Waiting for client to disconnect");
  while (!g_atomic_int_get (&testdata.client_terminated))
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Waiting for thread to terminate");
  g_thread_join (thread);
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
  g_test_add_func ("/service-channel/open-wayland-connection",
                   meta_test_service_channel_open_wayland_connection);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_NO_X11);
  g_assert_true (meta_context_configure (context, &argc, &argv, NULL));

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
