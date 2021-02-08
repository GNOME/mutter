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
  meta_surface_actor_update_area (actor, x, y, width, height);
}

static gboolean
meta_surface_actor_wayland_is_opaque (MetaSurfaceActor *actor)
{
  MetaShapedTexture *stex = meta_surface_actor_get_texture (actor);

  return meta_shaped_texture_is_opaque (stex);
}

CoglScanout *
meta_surface_actor_wayland_try_acquire_scanout (MetaSurfaceActorWayland *self,
                                                CoglOnscreen            *onscreen)
{
  MetaWaylandSurface *surface;
  CoglScanout *scanout;

  surface = meta_surface_actor_wayland_get_surface (self);
  if (!surface)
    return NULL;

  scanout = meta_wayland_surface_try_acquire_scanout (surface, onscreen);
  if (!scanout)
    return NULL;

  return scanout;
}

#define UNOBSCURED_TRESHOLD 0.1

ClutterStageView *
meta_surface_actor_wayland_get_current_primary_view (MetaSurfaceActor *actor,
                                                     ClutterStage     *stage)
{
  ClutterStageView *current_primary_view = NULL;
  float highest_refresh_rate = 0.f;
  float biggest_unobscurred_fraction = 0.f;
  GList *l;

  for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
    {
      ClutterStageView *stage_view = l->data;
      float refresh_rate;
      float unobscurred_fraction = 1.f;

      if (clutter_actor_has_mapped_clones (CLUTTER_ACTOR (actor)))
        {
          if (!clutter_actor_is_effectively_on_stage_view (CLUTTER_ACTOR (actor),
                                                           stage_view))
            continue;
        }
      else
        {
          if (l->next || biggest_unobscurred_fraction > 0.f)
            {
              if (meta_surface_actor_is_obscured_on_stage_view (actor,
                                                                stage_view,
                                                                &unobscurred_fraction))
                continue;
            }
          else
            {
              if (meta_surface_actor_is_obscured (actor))
                continue;
            }
        }

      refresh_rate = clutter_stage_view_get_refresh_rate (stage_view);

      if ((refresh_rate > highest_refresh_rate &&
           (unobscurred_fraction > UNOBSCURED_TRESHOLD ||
            biggest_unobscurred_fraction < UNOBSCURED_TRESHOLD)) ||
          (biggest_unobscurred_fraction < UNOBSCURED_TRESHOLD &&
           unobscurred_fraction > UNOBSCURED_TRESHOLD))
        {
          current_primary_view = stage_view;
          highest_refresh_rate = refresh_rate;
          biggest_unobscurred_fraction = unobscurred_fraction;
        }
    }

  return current_primary_view;
}

static void
meta_surface_actor_wayland_dispose (GObject *object)
{
  MetaSurfaceActorWayland *self = META_SURFACE_ACTOR_WAYLAND (object);
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

  G_OBJECT_CLASS (meta_surface_actor_wayland_parent_class)->dispose (object);
}

static void
meta_surface_actor_wayland_class_init (MetaSurfaceActorWaylandClass *klass)
{
  MetaSurfaceActorClass *surface_actor_class = META_SURFACE_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  surface_actor_class->process_damage = meta_surface_actor_wayland_process_damage;
  surface_actor_class->is_opaque = meta_surface_actor_wayland_is_opaque;

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
