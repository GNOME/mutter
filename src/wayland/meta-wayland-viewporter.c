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

  if(!surface)
    return;

  surface->viewport_resource = NULL;

  /* FIXME: The reset  is currently broken as of git master (3.29.90 + ~21)
   * on program close, the pending state seems to get deleted without unsetting
   * the pointer

  if(!surface->pending)
    return;
  surface->pending->buffer_viewport.buffer.src_rect = (cairo_rectangle_int_t) { 0 };
  surface->pending->buffer_viewport.surface.width = 0;
  surface->pending->buffer_viewport.surface.height = 0;
  surface->pending->has_new_buffer_viewport = TRUE;
  */
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
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  if(!surface)
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_NO_SURFACE,
                              "wl_surface for this viewport is no longer exists");
      return;
    }

  int x = floor(wl_fixed_to_double (src_x));
  int y = floor(wl_fixed_to_double (src_y));
  int width = ceil(wl_fixed_to_double (src_width));
  int height = ceil(wl_fixed_to_double (src_height));

  if(x >= 0 && y >= 0 && width > 0 && height > 0)
    {
      surface->pending->buffer_viewport.buffer.src_rect.x = x;
      surface->pending->buffer_viewport.buffer.src_rect.y = y;
      surface->pending->buffer_viewport.buffer.src_rect.width = width;
      surface->pending->buffer_viewport.buffer.src_rect.height = height;
      surface->pending->has_new_buffer_viewport = TRUE;
    }
  else if(x == -1 && y == -1 && width == -1 && height == -1)
    {
      surface->pending->buffer_viewport.buffer.src_rect.x = 0;
      surface->pending->buffer_viewport.buffer.src_rect.y = 0;
      surface->pending->buffer_viewport.buffer.src_rect.width = 0;
      surface->pending->buffer_viewport.buffer.src_rect.height = 0;
      surface->pending->has_new_buffer_viewport = TRUE;
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
  MetaWaylandSurface *surface = wl_resource_get_user_data (resource);
  if(!surface)
    {
      wl_resource_post_error (resource,
                              WP_VIEWPORT_ERROR_NO_SURFACE,
                              "wl_surface for this viewport is no longer exists");
      return;
    }

  if(dst_width > 0 && dst_height > 0)
    {
      surface->pending->buffer_viewport.surface.width = dst_width;
      surface->pending->buffer_viewport.surface.height = dst_height;
      surface->pending->has_new_buffer_viewport = TRUE;
    }
  else if(dst_width == -1 && dst_height == -1)
    {
      surface->pending->buffer_viewport.surface.width = 0;
      surface->pending->buffer_viewport.surface.height = 0;
      surface->pending->has_new_buffer_viewport = TRUE;
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
