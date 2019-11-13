/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Copyright (C) 2013 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "compositor/meta-surface-actor-wayland.h"

#include <math.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "cogl/cogl-wayland-server.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/region-utils.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-window-wayland.h"

struct _MetaSurfaceActorWayland
{
  MetaSurfaceActor parent;

  MetaWaylandSurface *surface;
  struct wl_list frame_callback_list;
};

G_DEFINE_TYPE (MetaSurfaceActorWayland,
               meta_surface_actor_wayland,
               META_TYPE_SURFACE_ACTOR)

static void
meta_surface_actor_wayland_process_damage (MetaSurfaceActor *actor,
                                           int               x,
                                           int               y,
                                           int               width,
                                           int               height)
{
}

static void
meta_surface_actor_wayland_pre_paint (MetaSurfaceActor *actor)
{
}

static gboolean
meta_surface_actor_wayland_is_visible (MetaSurfaceActor *actor)
{
  /* TODO: ensure that the buffer isn't NULL, implement
   * wayland mapping semantics */
  return TRUE;
}

static gboolean
meta_surface_actor_wayland_is_opaque (MetaSurfaceActor *actor)
{
  MetaShapedTexture *stex = meta_surface_actor_get_texture (actor);

  return meta_shaped_texture_is_opaque (stex);
}

void
meta_surface_actor_wayland_add_frame_callbacks (MetaSurfaceActorWayland *self,
                                                struct wl_list *frame_callbacks)
{
  wl_list_insert_list (&self->frame_callback_list, frame_callbacks);
}

static MetaWindow *
meta_surface_actor_wayland_get_window (MetaSurfaceActor *actor)
{
  MetaSurfaceActorWayland *self = META_SURFACE_ACTOR_WAYLAND (actor);
  MetaWaylandSurface *surface = meta_surface_actor_wayland_get_surface (self);

  if (!surface)
    return NULL;

  return surface->window;
}

static void
meta_surface_actor_wayland_paint (ClutterActor        *actor,
                                  ClutterPaintContext *paint_context)
{
  MetaSurfaceActorWayland *self = META_SURFACE_ACTOR_WAYLAND (actor);

  if (self->surface &&
      !meta_surface_actor_is_obscured (META_SURFACE_ACTOR (actor)))
    {
      MetaWaylandCompositor *compositor = self->surface->compositor;

      wl_list_insert_list (&compositor->frame_callbacks, &self->frame_callback_list);
      wl_list_init (&self->frame_callback_list);
    }

  CLUTTER_ACTOR_CLASS (meta_surface_actor_wayland_parent_class)->paint (actor,
                                                                        paint_context);
}

static void
meta_surface_actor_wayland_dispose (GObject *object)
{
  MetaSurfaceActorWayland *self = META_SURFACE_ACTOR_WAYLAND (object);
  MetaWaylandFrameCallback *cb, *next;
  MetaShapedTexture *stex;

  stex = meta_surface_actor_get_texture (META_SURFACE_ACTOR (self));
  if (stex)
    meta_shaped_texture_set_texture (stex, NULL);

  if (self->surface)
    {
      g_object_remove_weak_pointer (G_OBJECT (self->surface),
                                    (gpointer *) &self->surface);
      self->surface = NULL;
    }

  wl_list_for_each_safe (cb, next, &self->frame_callback_list, link)
    wl_resource_destroy (cb->resource);

  G_OBJECT_CLASS (meta_surface_actor_wayland_parent_class)->dispose (object);
}

static void
meta_surface_actor_wayland_class_init (MetaSurfaceActorWaylandClass *klass)
{
  MetaSurfaceActorClass *surface_actor_class = META_SURFACE_ACTOR_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  actor_class->paint = meta_surface_actor_wayland_paint;

  surface_actor_class->process_damage = meta_surface_actor_wayland_process_damage;
  surface_actor_class->pre_paint = meta_surface_actor_wayland_pre_paint;
  surface_actor_class->is_visible = meta_surface_actor_wayland_is_visible;
  surface_actor_class->is_opaque = meta_surface_actor_wayland_is_opaque;

  surface_actor_class->get_window = meta_surface_actor_wayland_get_window;

  object_class->dispose = meta_surface_actor_wayland_dispose;
}

static void
meta_surface_actor_wayland_init (MetaSurfaceActorWayland *self)
{
}

MetaSurfaceActor *
meta_surface_actor_wayland_new (MetaWaylandSurface *surface)
{
  MetaSurfaceActorWayland *self = g_object_new (META_TYPE_SURFACE_ACTOR_WAYLAND, NULL);

  g_assert (meta_is_wayland_compositor ());

  wl_list_init (&self->frame_callback_list);
  self->surface = surface;
  g_object_add_weak_pointer (G_OBJECT (self->surface),
                             (gpointer *) &self->surface);

  return META_SURFACE_ACTOR (self);
}

MetaWaylandSurface *
meta_surface_actor_wayland_get_surface (MetaSurfaceActorWayland *self)
{
  return self->surface;
}
