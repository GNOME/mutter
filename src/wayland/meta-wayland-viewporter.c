/*
 * Wayland Support
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include <glib.h>
#include "meta-wayland-viewporter.h"
#include "meta-wayland-versions.h"
#include "meta-wayland-surface.h"
#include "meta-wayland-subsurface.h"
#include "meta-wayland-private.h"
#include "viewporter-server-protocol.h"

static void
destroy_wl_viewport (struct wl_resource *resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);

  if (!surface)
    return;

  g_clear_object (&surface->viewport_resource);

  if (!surface->pending)
    return;

  surface->pending->viewport_src_width = -1;
  surface->pending->viewport_dest_width = -1;
  surface->pending->has_new_viewport_src_rect = TRUE;
  surface->pending->has_new_viewport_dest = TRUE;
}

static void
viewport_destroy (struct wl_client *client,
                     struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
viewport_set_source (struct wl_client *client,
                     struct wl_resource *resource,
                     wl_fixed_t src_x,
                     wl_fixed_t src_y,
                     wl_fixed_t src_width,
                     wl_fixed_t src_height)
{
  MetaWaylandSurface *surface;
  float new_x;
  float new_y;
  float new_width;
  float new_height;

  surface = wl_resource_get_user_data (resource);
  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_NO_SURFACE,
                              "wl_surface for this viewport is no longer exists");
      return;
    }

  new_x = wl_fixed_to_double (src_x);
  new_y = wl_fixed_to_double (src_y);
  new_width = wl_fixed_to_double (src_width);
  new_height = wl_fixed_to_double (src_height);

  if ((new_x >= 0 && new_y >= 0 &&
      new_width > 0 && new_height > 0) ||
      (new_x == -1 && new_y == -1 &&
      new_width == -1 && new_height == -1))
    {
      if (new_x != surface->viewport_src_x ||
          new_y != surface->viewport_src_y ||
          new_width != surface->viewport_src_width ||
          new_height != surface->viewport_src_height)
        {
          surface->pending->viewport_src_x = new_x;
          surface->pending->viewport_src_y = new_y;
          surface->pending->viewport_src_width = new_width;
          surface->pending->viewport_src_height = new_height;
          surface->pending->has_new_viewport_src_rect = TRUE;
        }
      else
        {
          surface->pending->has_new_viewport_src_rect = FALSE;
        }
    }
  else
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_BAD_VALUE,
                              "all values must be either positive or -1");
    }
}

static void
viewport_set_destination (struct wl_client *client,
                          struct wl_resource *resource,
                          int dst_width,
                          int dst_height)
{
  MetaWaylandSurface *surface;

  surface = wl_resource_get_user_data (resource);
  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_NO_SURFACE,
                              "wl_surface for this viewport is no longer exists");
      return;
    }

  if ((dst_width > 0 && dst_height > 0) || (dst_width == -1 && dst_height == -1))
    {
      if (surface->viewport_dest_width != dst_width ||
          surface->viewport_dest_height != dst_height)
        {
          surface->pending->viewport_dest_width = dst_width;
          surface->pending->viewport_dest_height = dst_height;
          surface->pending->has_new_viewport_dest = TRUE;
        }
      else
        {
          surface->pending->has_new_viewport_dest = FALSE;
        }
    }
  else
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_BAD_VALUE,
                              "all values must be either positive or -1");
    }
}

static const struct wp_viewport_interface meta_wayland_viewport_interface = {
  viewport_destroy,
  viewport_set_source,
  viewport_set_destination,
};

static void
viewporter_destroy (struct wl_client *client,
                    struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
viewporter_get_viewport (struct wl_client *client,
                         struct wl_resource *master_resource,
                         uint32_t viewport_id,
                         struct wl_resource *surface_resource)
{
  struct wl_resource *resource;
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);

  if (surface->viewport_resource)
    {
      wl_resource_post_error (master_resource,
                              WP_VIEWPORTER_ERROR_VIEWPORT_EXISTS,
                              "viewport already exists on surface");
      return;
    }

  resource = wl_resource_create (client,
                                 &wp_viewport_interface,
                                 wl_resource_get_version (master_resource),
                                 viewport_id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_viewport_interface,
                                  surface,
                                  destroy_wl_viewport);

  surface->viewport_resource = resource;
}

static const struct wp_viewporter_interface meta_wayland_viewporter_interface = {
  viewporter_destroy,
  viewporter_get_viewport,
};

static void
bind_viewporter (struct wl_client *client,
                 void             *data,
                 guint32           version,
                 guint32           id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_viewporter_interface,
                                 version,
                                 id);
  wl_resource_set_implementation (resource,
                                  &meta_wayland_viewporter_interface,
                                  data,
                                  NULL);
}

void
meta_wayland_viewporter_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wp_viewporter_interface,
                        META_WP_VIEWPORTER_VERSION,
                        compositor, bind_viewporter) == NULL)
  g_error ("Failed to register a global wl-subcompositor object");
}
