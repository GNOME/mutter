/*
 * Copyright (C) 2018 Endless, Inc.
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
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#pragma once

#include "compositor/meta-window-actor-private.h"

#define META_TYPE_WINDOW_ACTOR_WAYLAND (meta_window_actor_wayland_get_type())
G_DECLARE_FINAL_TYPE (MetaWindowActorWayland,
                      meta_window_actor_wayland,
                      META, WINDOW_ACTOR_WAYLAND,
                      MetaWindowActor)

#define META_TYPE_SURFACE_CONTAINER_ACTOR_WAYLAND (meta_surface_container_actor_wayland_get_type())
G_DECLARE_FINAL_TYPE (MetaSurfaceContainerActorWayland,
                      meta_surface_container_actor_wayland,
                      META, SURFACE_CONTAINER_ACTOR_WAYLAND,
                      ClutterActor)

void meta_window_actor_wayland_rebuild_surface_tree (MetaWindowActor *actor);
