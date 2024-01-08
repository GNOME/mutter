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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include "config.h"

#include "compositor/meta-surface-actor-wayland.h"

#include <math.h>

#include "backends/meta-backend-private.h"
#include "backends/meta-logical-monitor.h"
#include "backends/meta-screen-cast-window.h"
#include "compositor/meta-shaped-texture-private.h"
#include "compositor/meta-window-actor-private.h"
#include "wayland/meta-wayland-buffer.h"
#include "wayland/meta-wayland-private.h"
#include "wayland/meta-wayland-subsurface.h"
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

#define UNOBSCURED_THRESHOLD 0.1

gboolean
meta_surface_actor_wayland_is_view_primary (MetaSurfaceActor *actor,
                                            ClutterStageView *stage_view)
{
  ClutterStageView *current_primary_view = NULL;
  float highest_refresh_rate = 0.f;
  float biggest_unobscurred_fraction = 0.f;
  MetaWindowActor *window_actor;
  gboolean is_streaming = FALSE;
  GList *l;

  window_actor = meta_window_actor_from_actor (CLUTTER_ACTOR (actor));
  if (window_actor)
    is_streaming = meta_window_actor_is_streaming (window_actor);

  if (clutter_actor_has_mapped_clones (CLUTTER_ACTOR (actor)) || is_streaming)
    {
      ClutterStage *stage;
      ClutterStageView *fallback_view = NULL;
      float fallback_refresh_rate = 0.0;

      stage = CLUTTER_STAGE (clutter_actor_get_stage (CLUTTER_ACTOR (actor)));
      for (l = clutter_stage_peek_stage_views (stage); l; l = l->next)
        {
          ClutterStageView *view = l->data;
          float refresh_rate;

          refresh_rate = clutter_stage_view_get_refresh_rate (view);

          if (clutter_actor_is_effectively_on_stage_view (CLUTTER_ACTOR (actor),
                                                          view))
            {
              if (refresh_rate > highest_refresh_rate)
                {
                  current_primary_view = view;
                  highest_refresh_rate = refresh_rate;
                }
            }
          else
            {
              if (refresh_rate > fallback_refresh_rate)
                {
                  fallback_view = view;
                  fallback_refresh_rate = refresh_rate;
                }
            }
        }

      if (current_primary_view)
        return current_primary_view == stage_view;
      else if (is_streaming)
        return fallback_view == stage_view;
    }

  l = clutter_actor_peek_stage_views (CLUTTER_ACTOR (actor));
  if (!l)
    return FALSE;

  if (!l->next)
    {
      return !meta_surface_actor_is_obscured_on_stage_view (actor,
                                                            stage_view,
                                                            NULL);
    }

  for (; l; l = l->next)
    {
      ClutterStageView *view = l->data;
      float refresh_rate;
      float unobscurred_fraction;

      if (meta_surface_actor_is_obscured_on_stage_view (actor,
                                                        view,
                                                        &unobscurred_fraction))
        continue;

      refresh_rate = clutter_stage_view_get_refresh_rate (view);

      if ((refresh_rate > highest_refresh_rate &&
           (biggest_unobscurred_fraction < UNOBSCURED_THRESHOLD ||
            unobscurred_fraction > UNOBSCURED_THRESHOLD)) ||
          (biggest_unobscurred_fraction < UNOBSCURED_THRESHOLD &&
           unobscurred_fraction > UNOBSCURED_THRESHOLD))
        {
          current_primary_view = view;
          highest_refresh_rate = refresh_rate;
          biggest_unobscurred_fraction = unobscurred_fraction;
        }
    }

  return current_primary_view == stage_view;
}

static void
meta_surface_actor_wayland_apply_transform (ClutterActor      *actor,
                                            graphene_matrix_t *matrix)
{
  MetaSurfaceActorWayland *self = META_SURFACE_ACTOR_WAYLAND (actor);
  ClutterActorClass *parent_class =
    CLUTTER_ACTOR_CLASS (meta_surface_actor_wayland_parent_class);
  MetaWaylandSurface *surface = meta_surface_actor_wayland_get_surface (self);
  MetaWaylandSurface *root_surface;
  MetaWindow *window;
  MetaLogicalMonitor *logical_monitor;
  g_autoptr (ClutterActorBox) allocation = NULL;
  float scale;
  float actor_width, actor_height;
  float adj_actor_width, adj_actor_height;
  float adj_actor_x, adj_actor_y;
  float width_scale, height_scale;
  float x_off, y_off;

  if (!surface)
    goto out;

  root_surface = surface;
  while (root_surface->applied_state.parent)
    root_surface = root_surface->applied_state.parent;

  window = meta_wayland_surface_get_window (root_surface);
  if (!window)
    goto out;

  if (!META_IS_WINDOW_WAYLAND (window))
    goto out;

  logical_monitor = meta_window_get_highest_scale_monitor (window);
  if (!logical_monitor)
    goto out;

  scale = meta_logical_monitor_get_scale (logical_monitor);

  g_object_get (actor, "allocation", &allocation, NULL);

  actor_width = clutter_actor_box_get_width (allocation);
  actor_height = clutter_actor_box_get_height (allocation);

  if (actor_width == 0.0 || actor_height == 0.0)
    goto out;

  /* We rely on MetaSurfaceActorContainerWayland to ensure that the toplevel
   * surface on-display position is aligned to the physical pixel boundary.
   */
  if (META_IS_WAYLAND_SUBSURFACE (surface->role))
    {
      adj_actor_width =
        roundf ((surface->sub.x + actor_width) * scale) / scale -
        roundf (surface->sub.x * scale) / scale;
      adj_actor_height =
        roundf ((surface->sub.y + actor_height) * scale) / scale -
        roundf (surface->sub.y * scale) / scale;

      adj_actor_x = adj_actor_y = 0.0;

      do
        {
          adj_actor_x += roundf (surface->sub.x * scale) / scale;
          adj_actor_y += roundf (surface->sub.y * scale) / scale;

          surface = surface->applied_state.parent;
        }
      while (surface);
    }
  else
    {
      adj_actor_width = roundf (actor_width * scale) / scale;
      adj_actor_height = roundf (actor_height * scale) / scale;
      adj_actor_x = allocation->x1;
      adj_actor_y = allocation->y1;
    }

  width_scale = adj_actor_width / actor_width;
  height_scale = adj_actor_height / actor_height;

  if (!G_APPROX_VALUE (width_scale, 1.0, FLT_EPSILON) ||
      !G_APPROX_VALUE (height_scale, 1.0, FLT_EPSILON))
    graphene_matrix_scale (matrix, width_scale, height_scale, 1.0);

  parent_class->apply_transform (actor, matrix);

  x_off = adj_actor_x - allocation->x1;
  y_off = adj_actor_y - allocation->y1;

  if (!G_APPROX_VALUE (x_off, 0.0, FLT_EPSILON) ||
      !G_APPROX_VALUE (y_off, 0.0, FLT_EPSILON))
    graphene_matrix_translate (matrix, &GRAPHENE_POINT3D_INIT (x_off, y_off, 0.0));

  return;

out:
  parent_class->apply_transform (actor, matrix);
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
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  surface_actor_class->process_damage = meta_surface_actor_wayland_process_damage;
  surface_actor_class->is_opaque = meta_surface_actor_wayland_is_opaque;

  actor_class->apply_transform = meta_surface_actor_wayland_apply_transform;

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
