/*
 * Copyright (C) 2015-2019 Red Hat, Inc.
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
 */

#include "config.h"

#include "wayland/meta-wayland-dnd-surface.h"

struct _MetaWaylandSurfaceRoleDND
{
  MetaWaylandActorSurface parent;
};

G_DEFINE_TYPE (MetaWaylandSurfaceRoleDND,
               meta_wayland_surface_role_dnd,
               META_TYPE_WAYLAND_ACTOR_SURFACE)

static void
dnd_surface_assigned (MetaWaylandSurfaceRole *surface_role)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);

  meta_wayland_surface_queue_pending_frame_callbacks (surface);
}

static void
dnd_surface_commit (MetaWaylandSurfaceRole  *surface_role,
                    MetaWaylandPendingState *pending)
{
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (meta_wayland_surface_role_dnd_parent_class);

  meta_wayland_surface_queue_pending_state_frame_callbacks (surface, pending);

  surface_role_class->commit (surface_role, pending);
}

static void
dnd_subsurface_sync_actor_state (MetaWaylandActorSurface *actor_surface)
{
  MetaSurfaceActor *surface_actor =
    meta_wayland_actor_surface_get_actor (actor_surface);
  MetaWaylandSurfaceRole *surface_role =
    META_WAYLAND_SURFACE_ROLE (actor_surface);
  MetaWaylandSurface *surface =
    meta_wayland_surface_role_get_surface (surface_role);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (meta_wayland_surface_role_dnd_parent_class);
  float actor_scale;
  float actor_pos_x;
  float actor_pos_y;

  actor_scale = meta_wayland_actor_surface_calculate_scale (actor_surface);

  clutter_actor_get_position (CLUTTER_ACTOR (surface_actor),
                              &actor_pos_x,
                              &actor_pos_y);
  clutter_actor_set_position (CLUTTER_ACTOR (surface_actor),
                              actor_pos_x + surface->offset_x / actor_scale,
                              actor_pos_y + surface->offset_y / actor_scale);

  meta_wayland_surface_clear_offset (surface);

  actor_surface_class->sync_actor_state (actor_surface);
}

static void
meta_wayland_surface_role_dnd_init (MetaWaylandSurfaceRoleDND *role)
{
}

static void
meta_wayland_surface_role_dnd_class_init (MetaWaylandSurfaceRoleDNDClass *klass)
{
  MetaWaylandSurfaceRoleClass *surface_role_class =
    META_WAYLAND_SURFACE_ROLE_CLASS (klass);
  MetaWaylandActorSurfaceClass *actor_surface_class =
    META_WAYLAND_ACTOR_SURFACE_CLASS (klass);

  surface_role_class->assigned = dnd_surface_assigned;
  surface_role_class->commit = dnd_surface_commit;

  actor_surface_class->sync_actor_state = dnd_subsurface_sync_actor_state;
}
