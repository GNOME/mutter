/*
 * Copyright (C) 2023 Red Hat, Inc.
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

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "tests/wayland-test-clients/wayland-test-client-utils.h"

#include "meta-dbus-service-channel.h"

typedef enum _ServiceClientType
{
  SERVICE_CLIENT_TYPE_NONE,
  SERVICE_CLIENT_TYPE_PORTAL_BACKEND,
} ServiceClientType;

typedef enum _MetaWaylandClientCaps
{
  META_WAYLAND_CLIENT_CAPS_X11_INTEROP = (1 << 0),
} MetaWaylandClientCaps;

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial,
               uint32_t       *last_sync_event)
{
  *last_sync_event = serial;
}

static void
test_open_wayland_service_connection (void)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaDBusServiceChannel) service_channel = NULL;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;

  g_autofd int fd = -1;
  struct wl_display *wayland_display;
  uint32_t last_sync_event = UINT32_MAX;

  service_channel =
    meta_dbus_service_channel_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                      G_DBUS_PROXY_FLAGS_NONE,
                                                      "org.gnome.Mutter.ServiceChannel",
                                                      "/org/gnome/Mutter/ServiceChannel",
                                                      NULL,
                                                      &error);
  g_assert_nonnull (service_channel);

  if (!meta_dbus_service_channel_call_open_wayland_service_connection_sync (
         service_channel,
         SERVICE_CLIENT_TYPE_PORTAL_BACKEND,
         NULL,
         &fd_variant,
         &fd_list,
         NULL, &error))
    g_error ("Failed to open Wayland service connection: %s", error->message);

  fd = g_unix_fd_list_get (fd_list, g_variant_get_handle (fd_variant), &error);
  g_assert_cmpint (fd, >=, 0);

  wayland_display = wl_display_connect_to_fd (g_steal_fd (&fd));

  display = wayland_display_new_full (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER,
                                      wayland_display);
  g_signal_connect (display, "sync-event",
                    G_CALLBACK (on_sync_event), &last_sync_event);

  surface = wayland_surface_new (display, "test service client",
                                 100, 100, 0xffabcdff);
  wl_surface_commit (surface->wl_surface);

  while (last_sync_event != 1)
    wayland_display_dispatch (display);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  gboolean *acquired = user_data;

  *acquired = TRUE;
}

static void
own_dbus_name (const char *name)
{
  gboolean name_acquired = FALSE;

  g_bus_own_name (G_BUS_TYPE_SESSION,
                  name,
                  G_BUS_NAME_OWNER_FLAGS_REPLACE |
                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
                  NULL,
                  on_name_acquired,
                  NULL,
                  &name_acquired,
                  NULL);

  while (!name_acquired)
    g_main_context_iteration (NULL, TRUE);
}

static gboolean
open_wayland_connection (GVariantBuilder  *options_builder,
                         int              *fd_out,
                         GError          **error)
{
  g_autoptr (MetaDBusServiceChannel) service_channel = NULL;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autofd int fd = -1;

  service_channel = meta_dbus_service_channel_proxy_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_NONE,
      "org.gnome.Mutter.ServiceChannel",
      "/org/gnome/Mutter/ServiceChannel",
      NULL,
      error);

  g_assert_no_error (*error);
  g_assert_nonnull (service_channel);

  if (!meta_dbus_service_channel_call_open_wayland_connection_sync (
         service_channel,
         g_variant_builder_end (options_builder),
         NULL,
         &fd_variant,
         &fd_list,
         NULL, error))
    return FALSE;

  fd = g_unix_fd_list_get (fd_list,
                           g_variant_get_handle (fd_variant),
                           error);
  g_assert_no_error (*error);
  g_assert_cmpint (fd, >=, 0);

  if (fd_out)
    *fd_out = g_steal_fd (&fd);

  return TRUE;
}

static void
test_window_tag (void)
{
  g_autoptr (GError) error = NULL;
  g_auto (GVariantBuilder) options_builder =
    G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
  g_autoptr (WaylandDisplay) display = NULL;
  g_autoptr (WaylandSurface) surface = NULL;
  struct wl_display *wayland_display;
  uint32_t last_sync_event = UINT32_MAX;
  g_autofd int fd = -1;

  g_variant_builder_add (&options_builder, "{sv}",
                         "window-tag",
                         g_variant_new_string ("test-window-tag"));

  g_assert_true (open_wayland_connection (&options_builder, &fd, &error));

  wayland_display = wl_display_connect_to_fd (g_steal_fd (&fd));

  display = wayland_display_new_full (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER,
                                      wayland_display);
  g_signal_connect (display, "sync-event",
                    G_CALLBACK (on_sync_event), &last_sync_event);

  surface = wayland_surface_new (display, "test service client",
                                 100, 100, 0xffabcdff);
  wl_surface_commit (surface->wl_surface);

  while (last_sync_event != 1)
    wayland_display_dispatch (display);
}

static void
test_caps_no_permission (void)
{
  g_autoptr (GError) error = NULL;
  g_auto (GVariantBuilder) options_builder =
    G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  own_dbus_name ("org.gnome.Example.AppWithoutCaps");

  g_variant_builder_add (&options_builder, "{sv}",
                         "capabilities",
                         g_variant_new_uint64 (META_WAYLAND_CLIENT_CAPS_X11_INTEROP));

  g_assert_false (open_wayland_connection (&options_builder, NULL, &error));
  g_assert_true (g_error_matches (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED));
}

static void
test_caps_granted (void)
{
  g_autoptr (GError) error = NULL;
  g_auto (GVariantBuilder) options_builder =
    G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);

  own_dbus_name ("org.freedesktop.impl.portal.desktop.gnome");

  g_variant_builder_add (&options_builder, "{sv}",
                         "capabilities",
                         g_variant_new_uint64 (META_WAYLAND_CLIENT_CAPS_X11_INTEROP));

  g_assert_true (open_wayland_connection (&options_builder, NULL, &error));
  g_assert_no_error (error);
}

int
main (int    argc,
      char **argv)
{
  if (g_strcmp0 (argv[1], "OpenWaylandServiceConnection") == 0)
    test_open_wayland_service_connection ();
  else if (g_strcmp0 (argv[1], "WindowTag") == 0)
    test_window_tag ();
  else if (g_strcmp0 (argv[1], "CapsNoPermission") == 0)
    test_caps_no_permission ();
  else if (g_strcmp0 (argv[1], "CapsGranted") == 0)
    test_caps_granted ();
  else
    g_assert_not_reached ();

  return 0;
}
