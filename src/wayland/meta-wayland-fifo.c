/*
 * Copyright (C) 2023 Valve Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <wayland-server.h>

#include "fifo-v1-server-protocol.h"
#include "wayland/meta-wayland-fifo.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

typedef struct _MetaWaylandFifoSurface
{
  MetaWaylandSurface *surface;
  gulong destroy_handler_id;
} MetaWaylandFifoSurface;

static void
fifo_destructor (struct wl_resource *resource)
{
  MetaWaylandFifoSurface *fifo = wl_resource_get_user_data (resource);

  if (fifo->surface)
    {
      g_object_set_data (G_OBJECT (fifo->surface), "-meta-wayland-fifo", NULL);

      g_clear_signal_handler (&fifo->destroy_handler_id, fifo->surface);
    }

  g_free (fifo);
}

static void
fifo_destroy (struct wl_client   *client,
              struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
set_barrier (struct wl_client   *client,
             struct wl_resource *resource)
{
  MetaWaylandFifoSurface *fifo = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = fifo->surface;
  MetaWaylandSurfaceState *pending;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_FIFO_V1_ERROR_SURFACE_DESTROYED,
                              "surface destroyed");
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->fifo_barrier = TRUE;
}

static void
wait_barrier (struct wl_client   *client,
              struct wl_resource *resource)
{
  MetaWaylandFifoSurface *fifo = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = fifo->surface;
  MetaWaylandSurfaceState *pending;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_FIFO_V1_ERROR_SURFACE_DESTROYED,
                              "surface destroyed");
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);
  pending->fifo_wait = TRUE;
}

static const struct wp_fifo_v1_interface meta_wayland_fifo_interface =
{
  set_barrier,
  wait_barrier,
  fifo_destroy,
};

static void
fifo_manager_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
on_surface_destroyed (MetaWaylandSurface     *surface,
                      MetaWaylandFifoSurface *fifo)
{
  fifo->surface = NULL;
}

static void
fifo_manager_get_queue (struct wl_client   *client,
                        struct wl_resource *resource,
                        uint32_t            id,
                        struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandFifoSurface *fifo;
  struct wl_resource *fifo_resource;

  fifo = g_object_get_data (G_OBJECT (surface), "-meta-wayland-fifo");

  if (fifo)
    {
      wl_resource_post_error (resource,
                              WP_FIFO_MANAGER_V1_ERROR_ALREADY_EXISTS,
                              "Fifo resource already exists on surface");
      return;
    }

  fifo_resource = wl_resource_create (client,
                                      &wp_fifo_v1_interface,
                                      wl_resource_get_version (resource),
                                      id);

  fifo = g_new0 (MetaWaylandFifoSurface, 1);
  fifo->surface = surface;

  fifo->destroy_handler_id =
    g_signal_connect (surface,
                      "destroy",
                      G_CALLBACK (on_surface_destroyed),
                      fifo);

  g_object_set_data (G_OBJECT (surface), "-meta-wayland-fifo", fifo);

  wl_resource_set_implementation (fifo_resource,
                                  &meta_wayland_fifo_interface,
                                  fifo,
                                  fifo_destructor);
}

static const struct wp_fifo_manager_v1_interface meta_wayland_fifo_manager_interface =
{
  fifo_manager_destroy,
  fifo_manager_get_queue,
};

static void
bind_fifo (struct wl_client *client,
           void             *data,
           uint32_t          version,
           uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_fifo_manager_v1_interface,
                                 version, id);

  wl_resource_set_implementation (resource,
                                  &meta_wayland_fifo_manager_interface,
                                  NULL, NULL);
}

void
meta_wayland_fifo_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wp_fifo_manager_v1_interface,
                        META_WP_FIFO_V1_VERSION,
                        NULL,
                        bind_fifo) == NULL)
    g_error ("Failed to register a global fifo object");
}
