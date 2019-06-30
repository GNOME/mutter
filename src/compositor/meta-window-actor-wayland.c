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

#include "compositor/meta-surface-actor-wayland.h"
#include "compositor/meta-window-actor-wayland.h"
#include "meta/meta-window-actor.h"
#include "wayland/meta-wayland-surface.h"

struct _MetaWindowActorWayland
{
  MetaWindowActor parent;
  gboolean surface_tree_rebuild_pending;
};

G_DEFINE_TYPE (MetaWindowActorWayland, meta_window_actor_wayland, META_TYPE_WINDOW_ACTOR)

static gboolean
remove_surface_actor_from_children (GNode    *node,
                                    gpointer  data)
{
  MetaWaylandSurface *surface;
  MetaSurfaceActor *surface_actor;
  MetaWindowActor *window_actor;

  surface = node->data;
  surface_actor = meta_wayland_surface_get_actor (surface);
  window_actor = data;

  clutter_actor_remove_child (CLUTTER_ACTOR (window_actor),
                              CLUTTER_ACTOR (surface_actor));

  return FALSE;
}

static gboolean
add_surface_actor_to_children (GNode    *node,
                               gpointer  data)
{
  MetaWaylandSurface *surface;
  MetaSurfaceActor *surface_actor;
  MetaWindowActor *window_actor;

  surface = node->data;
  surface_actor = meta_wayland_surface_get_actor (surface);
  window_actor = data;

  clutter_actor_add_child (CLUTTER_ACTOR (window_actor),
                           CLUTTER_ACTOR (surface_actor));

  return FALSE;
}

static void
rebuild_surface_tree (MetaWindowActor *actor)
{
  MetaSurfaceActor *surface_actor;
  MetaWaylandSurface *surface;
  GNode *root_node;

  surface_actor = meta_window_actor_get_surface (actor);
  surface = meta_surface_actor_wayland_get_surface (
    META_SURFACE_ACTOR_WAYLAND (surface_actor));

  meta_wayland_surface_ensure_subsurface_node (surface);
  root_node = surface->subsurface_node;

  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   remove_surface_actor_from_children,
                   actor);

  g_node_traverse (root_node,
                   G_IN_ORDER,
                   G_TRAVERSE_LEAVES,
                   -1,
                   add_surface_actor_to_children,
                   actor);
}

void
meta_window_actor_wayland_queue_surface_tree_rebuild (MetaWindowActorWayland *actor)
{
  actor->surface_tree_rebuild_pending = TRUE;
}

MetaWindowActorWayland *
meta_window_actor_wayland_from_surface (MetaWaylandSurface *surface)
{
  if (!surface)
    return NULL;
  else if (surface->window)
    return META_WINDOW_ACTOR_WAYLAND (meta_window_actor_from_window (surface->window));
  else if (surface->sub.parent)
    return meta_window_actor_wayland_from_surface (surface->sub.parent);
  else
    return NULL;
}

static void
meta_window_actor_wayland_frame_complete (MetaWindowActor  *actor,
                                          ClutterFrameInfo *frame_info,
                                          int64_t           presentation_time)
{
}

static void
meta_window_actor_wayland_queue_frame_drawn (MetaWindowActor *actor,
                                             gboolean         skip_sync_delay)
{
}

static void
meta_window_actor_wayland_pre_paint (MetaWindowActor *actor)
{
  MetaWindowActorWayland *window_actor = META_WINDOW_ACTOR_WAYLAND (actor);

  if(window_actor->surface_tree_rebuild_pending)
    {
      rebuild_surface_tree (actor);
      window_actor->surface_tree_rebuild_pending = FALSE;
    }
}

static void
meta_window_actor_wayland_post_paint (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_queue_destroy (MetaWindowActor *actor)
{
}

static void
meta_window_actor_wayland_class_init (MetaWindowActorWaylandClass *klass)
{
  MetaWindowActorClass *window_actor_class = META_WINDOW_ACTOR_CLASS (klass);

  window_actor_class->frame_complete = meta_window_actor_wayland_frame_complete;
  window_actor_class->queue_frame_drawn = meta_window_actor_wayland_queue_frame_drawn;
  window_actor_class->pre_paint = meta_window_actor_wayland_pre_paint;
  window_actor_class->post_paint = meta_window_actor_wayland_post_paint;
  window_actor_class->queue_destroy = meta_window_actor_wayland_queue_destroy;
}

static void
meta_window_actor_wayland_init (MetaWindowActorWayland *self)
{
}
