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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Georges Basile Stavracas Neto <gbsneto@gnome.org>
 */

#include "compositor/meta-window-actor-wayland.h"
#include "meta/meta-window-actor.h"

struct _MetaWindowActorWayland
{
  MetaWindowActor parent;
};

G_DEFINE_TYPE (MetaWindowActorWayland, meta_window_actor_wayland, META_TYPE_WINDOW_ACTOR)

static void
meta_window_actor_wayland_class_init (MetaWindowActorWaylandClass *klass)
{
}

static void
meta_window_actor_wayland_init (MetaWindowActorWayland *self)
{
}
