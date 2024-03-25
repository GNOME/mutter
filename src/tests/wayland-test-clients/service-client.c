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

static WaylandDisplay *display;
static WaylandSurface *surface;

static void
on_sync_event (WaylandDisplay *display,
               uint32_t        serial,
               uint32_t       *last_sync_event)
{
  *last_sync_event = serial;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (MetaDBusServiceChannel) service_channel = NULL;
  g_autoptr (GVariant) fd_variant = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  int fd;
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

  wayland_display = wl_display_connect_to_fd (fd);

  display = wayland_display_new_full (WAYLAND_DISPLAY_CAPABILITY_TEST_DRIVER,
                                      wayland_display);
  g_signal_connect (display, "sync-event",
                    G_CALLBACK (on_sync_event), &last_sync_event);

  surface = wayland_surface_new (display, "test service client",
                                 100, 100, 0xffabcdff);
  wl_surface_commit (surface->wl_surface);

  while (last_sync_event != 1)
    wayland_display_dispatch (display);

  g_object_unref (surface);
  g_object_unref (display);
}
