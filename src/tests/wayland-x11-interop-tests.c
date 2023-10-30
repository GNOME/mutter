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

#include <wayland-client.h>

#include "meta-test/meta-context-test.h"
#include "tests/meta-test-utils.h"
#include "tests/meta-wayland-test-driver.h"
#include "tests/meta-wayland-test-utils.h"
#include "tests/wayland-test-clients/wayland-test-client-utils.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-window-wayland.h"
#include "x11/window-x11.h"

#include "mutter-x11-interop-client-protocol.h"
#include "mutter-x11-interop-server-protocol.h"

typedef enum _ServiceClientType
{
  SERVICE_CLIENT_TYPE_NONE,
  SERVICE_CLIENT_TYPE_PORTAL_BACKEND,
} ServiceClientType;

static MetaContext *test_context;
static MetaWaylandTestDriver *test_driver;
static MetaVirtualMonitor *virtual_monitor;

static void
handle_registry_global (void               *user_data,
                        struct wl_registry *registry,
                        uint32_t            id,
                        const char         *interface,
                        uint32_t            version)
{
  struct mutter_x11_interop **x11_interop = user_data;

  if (strcmp (interface, mutter_x11_interop_interface.name) == 0)
    {
      *x11_interop = wl_registry_bind (registry, id,
                                       &mutter_x11_interop_interface, 1);
    }
}

static void
handle_registry_global_remove (void               *user_data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
}

static const struct wl_registry_listener registry_listener = {
  handle_registry_global,
  handle_registry_global_remove
};

static struct mutter_x11_interop *
get_x11_interop (WaylandDisplay *display)
{
  struct wl_registry *registry;
  struct mutter_x11_interop *x11_interop = NULL;

  registry = wl_display_get_registry (display->display);
  wl_registry_add_listener (registry, &registry_listener, &x11_interop);
  wl_display_roundtrip (display->display);

  return x11_interop;
}

static gpointer
regular_client_thread_func (gpointer user_data)
{
  gboolean *client_terminated = user_data;
  WaylandDisplay *display;
  struct mutter_x11_interop *x11_interop = NULL;

  display = wayland_display_new (WAYLAND_DISPLAY_CAPABILITY_NONE);

  x11_interop = get_x11_interop (display);
  g_assert_null (x11_interop);

  g_object_unref (display);

  g_atomic_int_set (client_terminated, TRUE);
  return NULL;
}

static void
meta_test_wayland_client_x11_interop_hidden_by_default (void)
{
  g_autoptr (GThread) thread = NULL;
  gboolean client_terminated = FALSE;

  thread = g_thread_new ("regular client thread",
                         regular_client_thread_func,
                         &client_terminated);

  g_debug ("Waiting for client to disconnect itself");

  while (!client_terminated)
    g_main_context_iteration (NULL, TRUE);

  g_debug ("Waiting for thread to terminate");
  g_thread_join (thread);
}

typedef struct
{
  Window xwindow;
  gboolean client_terminated;
} X11ParentTestdata;

static gpointer
service_client_thread_func (gpointer user_data)
{
  X11ParentTestdata *data = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusProxy) service_channel = NULL;
  GVariant *service_client_type_variant;
  g_autoptr (GVariant) result = NULL;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd;
  struct wl_display *wayland_display;
  WaylandDisplay *display;
  WaylandSurface *surface;
  struct mutter_x11_interop *x11_interop;

  service_channel = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                   G_DBUS_PROXY_FLAGS_NONE,
                                                   NULL,
                                                   "org.gnome.Mutter.ServiceChannel",
                                                   "/org/gnome/Mutter/ServiceChannel",
                                                   "org.gnome.Mutter.ServiceChannel",
                                                   NULL,
                                                   &error);
  g_assert_nonnull (service_channel);

  service_client_type_variant =
    g_variant_new ("(u)", SERVICE_CLIENT_TYPE_PORTAL_BACKEND);
  result =
    g_dbus_proxy_call_with_unix_fd_list_sync (service_channel,
                                              "OpenWaylandServiceConnection",
                                              service_client_type_variant,
                                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                              -1,
                                              NULL,
                                              &fd_list,
                                              NULL, &error);
  g_assert_nonnull (result);

  g_variant_get (result, "(@h)", &fd_variant);
  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_variant), &error);
  g_assert_cmpint (fd, >=, 0);

  wayland_display = wl_display_connect_to_fd (fd);
  g_assert_nonnull (wayland_display);

  display = wayland_display_new_full (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER,
                                      wayland_display);

  x11_interop = get_x11_interop (display);
  g_assert_nonnull (x11_interop);

  surface = wayland_surface_new (display, "test service window",
                                 100, 100, 0xffabcdff);
  mutter_x11_interop_set_x11_parent (x11_interop,
                                     surface->wl_surface,
                                     data->xwindow);
  wl_surface_commit (surface->wl_surface);

  wait_for_sync_event (display, 0);

  mutter_x11_interop_destroy (x11_interop);
  g_object_unref (display);

  g_atomic_int_set (&data->client_terminated, TRUE);

  return NULL;
}

static void
meta_test_wayland_client_x11_interop_x11_parent (void)
{
  X11ParentTestdata data = {};
  g_autoptr (GThread) thread = NULL;
  g_autoptr (GError) error = NULL;
  MetaTestClient *x11_client;
  MetaWindow *x11_window;
  MetaWindow *wayland_window;

  x11_client = meta_test_client_new (test_context,
                                     "x11-client",
                                     META_WINDOW_CLIENT_TYPE_X11,
                                     &error);
  g_assert_nonnull (x11_client);
  meta_test_client_run (x11_client,
                        "create win\n"
                        "show win\n");
  x11_window = meta_wait_for_client_window (test_context, "test/x11-client/win");
  g_assert_true (META_IS_WINDOW_X11 (x11_window));

  g_debug ("Spawning Wayland client");
  data = (X11ParentTestdata) {
    .xwindow = meta_window_x11_get_toplevel_xwindow (x11_window),
    .client_terminated = FALSE,
  };
  thread = g_thread_new ("service client thread",
                         service_client_thread_func,
                         &data);
  wayland_window = meta_wait_for_client_window (test_context,
                                                "test service window");
  g_assert_true (META_IS_WINDOW_WAYLAND (wayland_window));

  g_assert_true (meta_window_get_transient_for (wayland_window) ==
                 x11_window);

  meta_wayland_test_driver_emit_sync_event (test_driver, 0);

  g_debug ("Waiting for client to disconnect");
  while (!g_atomic_int_get (&data.client_terminated))
    g_main_context_iteration (NULL, TRUE);

  meta_test_client_destroy (x11_client);

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
  g_test_add_func ("/wayland/client/x11-interop/hidden-by-default",
                   meta_test_wayland_client_x11_interop_hidden_by_default);
  g_test_add_func ("/wayland/client/x11-interop/x11-parent",
                   meta_test_wayland_client_x11_interop_x11_parent);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (MetaContext) context = NULL;
  g_autoptr (GError) error = NULL;

  context = meta_create_test_context (META_CONTEXT_TEST_TYPE_HEADLESS,
                                      META_CONTEXT_TEST_FLAG_TEST_CLIENT);
  g_assert (meta_context_configure (context, &argc, &argv, NULL));

  test_context = context;

  init_tests ();

  g_signal_connect (context, "before-tests",
                    G_CALLBACK (on_before_tests), NULL);
  g_signal_connect (context, "after-tests",
                    G_CALLBACK (on_after_tests), NULL);

  return meta_context_test_run_tests (META_CONTEXT_TEST (context),
                                      META_TEST_RUN_FLAG_CAN_SKIP);
}
