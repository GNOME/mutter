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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "wayland/meta-wayland-surface-private.h"

#define META_TYPE_WAYLAND_ACTOR_SURFACE (meta_wayland_actor_surface_get_type ())
G_DECLARE_DERIVABLE_TYPE (MetaWaylandActorSurface,
                          meta_wayland_actor_surface,
                          META, WAYLAND_ACTOR_SURFACE,
                          MetaWaylandSurfaceRole)

struct _MetaWaylandActorSurfaceClass
{
  MetaWaylandSurfaceRoleClass parent_class;

  int (* get_geometry_scale) (MetaWaylandActorSurface *actor_surface);
  void (* sync_actor_state) (MetaWaylandActorSurface *actor_surface);
};

void meta_wayland_actor_surface_sync_actor_state (MetaWaylandActorSurface *actor_surface);
int meta_wayland_actor_surface_get_geometry_scale (MetaWaylandActorSurface *actor_surface);

META_EXPORT_TEST
MetaSurfaceActor * meta_wayland_actor_surface_get_actor (MetaWaylandActorSurface *actor_surface);

void meta_wayland_actor_surface_reset_actor (MetaWaylandActorSurface *actor_surface);

void meta_wayland_actor_surface_queue_frame_callbacks (MetaWaylandActorSurface *actor_surface,
                                                       MetaWaylandSurfaceState *pending);

void meta_wayland_actor_surface_emit_frame_callbacks (MetaWaylandActorSurface *actor_surface,
                                                      uint32_t                 timestamp_ms);
