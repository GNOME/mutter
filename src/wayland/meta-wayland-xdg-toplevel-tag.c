/*
 * Copyright (C) 2024 Red Hat
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
 * Written by:
 *     Bilal Elmoussaoui <belmouss@redhat.com>
 */

#include "config.h"

#include "wayland/meta-wayland-xdg-toplevel-tag.h"

#include <wayland-server.h>

#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

#include "xdg-toplevel-tag-v1-server-protocol.h"

static void
xdg_toplevel_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
xdg_toplevel_set_toplevel_tag (struct wl_client   *client,
                               struct wl_resource *resource,
                               struct wl_resource *toplevel,
                               const char         *tag)
{
  MetaWaylandSurfaceRole *surface_role = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface;
  MetaWindow *window;

  if (!META_IS_WAYLAND_SURFACE_ROLE (surface_role))
    return;

  surface = meta_wayland_surface_role_get_surface (surface_role);
  window = meta_wayland_surface_get_window (surface);

  if (!window)
    return;

  if (window->mapped)
    {
      wl_resource_post_error (resource,
                              XDG_TOPLEVEL_TAG_MANAGER_V1_ERROR_TAG_ON_MAPPED,
                              "Window is already mapped");
      return;
    }

  window->toplevel_tag = g_strdup (tag);
}

static const struct xdg_toplevel_tag_manager_v1_interface meta_xdg_toplevel_tag_interface = {
  xdg_toplevel_destroy,
  xdg_toplevel_set_toplevel_tag,
};


static void
bind_xdg_toplevel_tag (struct wl_client *client,
                       void             *data,
                       uint32_t          version,
                       uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &xdg_toplevel_tag_manager_v1_interface,
                                 META_XDG_TOPLEVEL_TAG_V1_VERSION,
                                 id);

  if (resource == NULL)
    {
      wl_client_post_no_memory (client);
      return;
    }

  wl_resource_set_implementation (resource,
                                  &meta_xdg_toplevel_tag_interface,
                                  NULL, NULL);
}

gboolean
meta_wayland_xdg_toplevel_tag_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &xdg_toplevel_tag_manager_v1_interface,
                        META_XDG_TOPLEVEL_TAG_V1_VERSION,
                        NULL,
                        bind_xdg_toplevel_tag) == NULL)
    return FALSE;

  return TRUE;
}
