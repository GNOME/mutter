/*
 * Copyright 2023 Red Hat
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

#include "wayland/meta-wayland-x11-interop.h"

#include "core/meta-service-channel.h"
#include "core/window-private.h"
#include "wayland/meta-wayland-client-private.h"
#include "wayland/meta-wayland-filter-manager.h"
#include "wayland/meta-wayland-surface-private.h"
#include "wayland/meta-wayland-versions.h"
#include "wayland/meta-wayland.h"
#include "x11/meta-x11-display-private.h"

#include "mutter-x11-interop-server-protocol.h"

static void
mutter_x11_interop_destroy (struct wl_client   *client,
                            struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
mutter_x11_interop_set_x11_parent (struct wl_client   *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *surface_resource,
                                   uint32_t            xwindow_id)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandCompositor *compositor =
    meta_wayland_surface_get_compositor (surface);
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaDisplay *display = meta_context_get_display (context);
  MetaX11Display *x11_display = meta_display_get_x11_display (display);
  MetaWindow *x11_window;
  MetaWindow *wayland_window;

  if (!x11_display)
    return;

  x11_window = meta_x11_display_lookup_x_window (x11_display, xwindow_id);
  if (!x11_window)
    return;

  wayland_window = meta_wayland_surface_get_window (surface);
  if (!wayland_window)
    return;

  meta_window_set_transient_for (wayland_window, x11_window);
}

static const struct mutter_x11_interop_interface meta_wayland_x11_interop_interface = {
  mutter_x11_interop_destroy,
  mutter_x11_interop_set_x11_parent,
};

static void
bind_x11_interop (struct wl_client *client,
                  void             *user_data,
                  uint32_t          version,
                  uint32_t          id)
{
  MetaWaylandCompositor *compositor = user_data;
  struct wl_resource *resource;

  resource = wl_resource_create (client, &mutter_x11_interop_interface,
                                 version, id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_x11_interop_interface,
                                  compositor, NULL);
}

static MetaWaylandAccess
x11_interop_filter (const struct wl_client *client,
                    const struct wl_global *global,
                    gpointer                user_data)
{
  MetaWaylandCompositor *compositor = user_data;
  MetaContext *context = meta_wayland_compositor_get_context (compositor);
  MetaServiceChannel *service_channel =
    meta_context_get_service_channel (context);
  MetaWaylandClient *service_client;

  service_client =
    meta_service_channel_get_service_client (service_channel,
                                             META_SERVICE_CLIENT_TYPE_PORTAL_BACKEND);
  if (!service_client)
    return META_WAYLAND_ACCESS_DENIED;

  if (meta_wayland_client_matches (service_client, client))
    return META_WAYLAND_ACCESS_ALLOWED;
  else
    return META_WAYLAND_ACCESS_DENIED;
}

void
meta_wayland_x11_interop_init (MetaWaylandCompositor *compositor)
{
  MetaWaylandFilterManager *filter_manager =
    meta_wayland_compositor_get_filter_manager (compositor);
  struct wl_display *wayland_display =
    meta_wayland_compositor_get_wayland_display (compositor);
  struct wl_global *global;

  global = wl_global_create (wayland_display,
                             &mutter_x11_interop_interface,
                             META_MUTTER_X11_INTEROP_VERSION,
                             compositor, bind_x11_interop);

  meta_wayland_filter_manager_add_global (filter_manager,
                                          global,
                                          x11_interop_filter,
                                          compositor);
}
