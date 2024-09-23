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

#include "commit-timing-v1-server-protocol.h"
#include "wayland/meta-wayland-commit-timing.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-versions.h"

typedef struct _MetaWaylandCommitTimerSurface
{
  MetaWaylandSurface *surface;
  gulong destroy_handler_id;
} MetaWaylandCommitTimerSurface;

static void
commit_timer_destructor (struct wl_resource *resource)
{
  MetaWaylandCommitTimerSurface *timer = wl_resource_get_user_data (resource);

  if (timer->surface)
    {
      g_object_set_data (G_OBJECT (timer->surface),
                         "-meta-wayland-commit-timer", NULL);

      g_clear_signal_handler (&timer->destroy_handler_id, timer->surface);
    }

  g_free (timer);
}

static void
commit_timer_destroy (struct wl_client   *client,
                      struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
commit_timer_set_target_time (struct wl_client   *client,
                              struct wl_resource *resource,
                              uint32_t            sec_hi,
                              uint32_t            sec_lo,
                              uint32_t            nsec)
{
  MetaWaylandCommitTimerSurface *timer = wl_resource_get_user_data (resource);
  MetaWaylandSurface *surface = timer->surface;
  MetaWaylandSurfaceState *pending;
  uint64_t target_time_us;

  if (!surface)
    {
      wl_resource_post_error (resource,
                              WP_COMMIT_TIMER_V1_ERROR_SURFACE_DESTROYED,
                              "Surface destroyed");
      return;
    }

  pending = meta_wayland_surface_get_pending_state (surface);

  if (pending->has_target_time)
    {
      wl_resource_post_error (resource,
                              WP_COMMIT_TIMER_V1_ERROR_TIMESTAMP_EXISTS,
                              "Commit already has timestamp");
      return;
    }

  if (nsec > 999999999)
    {
      wl_resource_post_error (resource,
                              WP_COMMIT_TIMER_V1_ERROR_INVALID_TIMESTAMP,
                              "Timestamp is invalid");
      return;
    }

  target_time_us = (((uint64_t) sec_hi << 32) + sec_lo) * 1000000;
  target_time_us += nsec / 1000;

  pending->has_target_time = TRUE;
  pending->target_time_us = target_time_us;
}

static const struct wp_commit_timer_v1_interface meta_wayland_commit_timer_interface =
{
  commit_timer_set_target_time,
  commit_timer_destroy,
};

static void
commit_timing_manager_destroy (struct wl_client   *client,
                               struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static void
on_surface_destroyed (MetaWaylandSurface            *surface,
                      MetaWaylandCommitTimerSurface *timer)
{
  timer->surface = NULL;
}

static void
commit_timing_manager_get_timer (struct wl_client   *client,
                                 struct wl_resource *resource,
                                 uint32_t            id,
                                 struct wl_resource *surface_resource)
{
  MetaWaylandSurface *surface = wl_resource_get_user_data (surface_resource);
  MetaWaylandCommitTimerSurface *timer;
  struct wl_resource *timer_resource;

  timer = g_object_get_data (G_OBJECT (surface), "-meta-wayland-commit-timer");

  if (timer)
    {
      wl_resource_post_error (resource,
                              WP_COMMIT_TIMING_MANAGER_V1_ERROR_COMMIT_TIMER_EXISTS,
                              "Commit timing resource already exists on surface");
      return;
    }

  timer_resource = wl_resource_create (client,
                                       &wp_commit_timer_v1_interface,
                                       wl_resource_get_version (resource),
                                       id);

  timer = g_new0 (MetaWaylandCommitTimerSurface, 1);
  timer->surface = surface;

  timer->destroy_handler_id =
    g_signal_connect (surface,
                      "destroy",
                      G_CALLBACK (on_surface_destroyed),
                      timer);

  g_object_set_data (G_OBJECT (surface), "-meta-wayland-commit-timer", timer);

  wl_resource_set_implementation (timer_resource,
                                  &meta_wayland_commit_timer_interface,
                                  timer,
                                  commit_timer_destructor);
}

static const struct wp_commit_timing_manager_v1_interface meta_wayland_commit_timing_manager_interface =
{
  commit_timing_manager_destroy,
  commit_timing_manager_get_timer,
};

static void
bind_commit_timing (struct wl_client *client,
                    void             *data,
                    uint32_t          version,
                    uint32_t          id)
{
  struct wl_resource *resource;

  resource = wl_resource_create (client,
                                 &wp_commit_timing_manager_v1_interface,
                                 version, id);

  wl_resource_set_implementation (resource,
                                  &meta_wayland_commit_timing_manager_interface,
                                  NULL, NULL);
}

void
meta_wayland_commit_timing_init (MetaWaylandCompositor *compositor)
{
  if (wl_global_create (compositor->wayland_display,
                        &wp_commit_timing_manager_v1_interface,
                        META_WP_COMMIT_TIMING_V1_VERSION,
                        NULL,
                        bind_commit_timing) == NULL)
    g_error ("Failed to register a global commit_timing_manager object");
}
