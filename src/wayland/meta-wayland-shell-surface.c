/*
 * Copyright (C) 2012,2013 Intel Corporation
 * Copyright (C) 2013-2017 Red Hat, Inc.
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

#include "config.h"

#include "wayland/meta-wayland-shell-surface.h"

#include "compositor/meta-surface-actor-wayland.h"
#include "wayland/meta-wayland-actor-surface.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-subsurface.h"
#include "wayland/meta-wayland-surface.h"
#include "wayland/meta-window-wayland.h"

G_DEFINE_ABSTRACT_TYPE (MetaWaylandShellSurface,
                        meta_wayland_shell_surface,
                        META_TYPE_WAYLAND_ACTOR_SURFACE)

void
meta_wayland_shell_surface_calculate_geometry (MetaWaylandShellSurface *shell_surface,
                                               MetaRectangle           *out_geometry)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaRectangle geometry;
  GList *l;

  geometry = (MetaRectangle) {
    .width = meta_wayland_surface_get_width (surface),
    .height = meta_wayland_surface_get_height (surface),
  };

  for (l = surface->subsurfaces; l; l = l->next)
    {
      MetaWaylandSurface *subsurface_surface = l->data;
      MetaWaylandSubsurface *subsurface =
        META_WAYLAND_SUBSURFACE (subsurface_surface->role);

      meta_wayland_subsurface_union_geometry (subsurface,
                                              0, 0,
                                              &geometry);
    }

  *out_geometry = geometry;
}

void
meta_wayland_shell_surface_determine_geometry (MetaWaylandShellSurface *shell_surface,
                                               MetaRectangle           *set_geometry,
                                               MetaRectangle           *out_geometry)
{
  MetaRectangle bounding_geometry = { 0 };
  MetaRectangle intersected_geometry = { 0 };

  meta_wayland_shell_surface_calculate_geometry (shell_surface,
                                                 &bounding_geometry);

  meta_rectangle_intersect (set_geometry, &bounding_geometry,
                            &intersected_geometry);

  *out_geometry = intersected_geometry;
}

void
meta_wayland_shell_surface_set_window (MetaWaylandShellSurface *shell_surface,
                                       MetaWindow              *window)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (shell_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  meta_wayland_surface_set_window (surface, window);
  meta_window_update_monitor (window, META_WINDOW_UPDATE_MONITOR_FLAGS_NONE);
}

void
meta_wayland_shell_surface_configure (MetaWaylandShellSurface *shell_surface,
                                      int                      new_x,
                                      int                      new_y,
                                      int                      new_width,
                                      int                      new_height,
                                      MetaWaylandSerial       *sent_serial)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->configure (shell_surface,
                                  new_x,
                                  new_y,
                                  new_width,
                                  new_height,
                                  sent_serial);
}

void
meta_wayland_shell_surface_ping (MetaWaylandShellSurface *shell_surface,
                                 uint32_t                 serial)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->ping (shell_surface, serial);
}

void
meta_wayland_shell_surface_close (MetaWaylandShellSurface *shell_surface)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->close (shell_surface);
}

void
meta_wayland_shell_surface_managed (MetaWaylandShellSurface *shell_surface,
                                    MetaWindow              *window)
{
  MetaWaylandShellSurfaceClass *shell_surface_class =
    META_WAYLAND_SHELL_SURFACE_GET_CLASS (shell_surface);

  shell_surface_class->managed (shell_surface, window);
}

static void
meta_wayland_shell_surface_surface_commit (MetaWaylandSurfaceRole  *surface_role,
                                           MetaWaylandPendingState *pending)
{
  MetaWaylandActorSurface *actor_surface =
    META_WAYLAND_ACTOR_SURFACE (surface_role);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class;
  MetaWindow *window;
  MetaWaylandBuffer *buffer;
  CoglTexture *texture;
  double scale;

  surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_shell_surface_parent_class);
  surface_role_class->commit (surface_role, pending);

  buffer = surface->buffer_ref.buffer;
  if (!buffer)
    return;

  window = surface->window;
  if (!window)
    return;

  scale = meta_wayland_actor_surface_calculate_scale (actor_surface);
  texture = meta_wayland_surface_get_texture (surface);

  window->buffer_rect.width = cogl_texture_get_width (texture) * scale;
  window->buffer_rect.height = cogl_texture_get_height (texture) * scale;
}

static void
meta_wayland_shell_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_shell_surface_parent_class);
  MetaWaylandSurface *toplevel_surface;

  toplevel_surface = meta_wayland_surface_get_toplevel (surface);
  if (toplevel_surface && toplevel_surface->window)
    actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_wayland_shell_surface_init (MetaWaylandShellSurface *role)
{
}

static void
meta_wayland_shell_surface_class_init (MetaWaylandShellSurfaceClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  surface_role_class->commit = meta_wayland_shell_surface_surface_commit;
  actor_surface_class->sync_actor_state =
    meta_wayland_shell_surface_sync_actor_state;
}
